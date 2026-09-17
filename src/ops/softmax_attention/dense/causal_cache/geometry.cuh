#pragma once

#include "ops/softmax_attention/common/head_mapping.cuh"

#include <cstdint>
#include <stdexcept>

namespace ninfer::ops {

template <int QHeadsValue, int KVHeadsValue, int SmallTSplitScaleValue>
struct CausalAttentionGeometry : AttentionHeadMapping<QHeadsValue, KVHeadsValue> {
    static_assert(SmallTSplitScaleValue > 0);

    static constexpr int SmallTSplitScale    = SmallTSplitScaleValue;
    static constexpr int SmallTMaximumSplits = 85 * SmallTSplitScale;
};

using CausalD256H24Kv4 = CausalAttentionGeometry<24, 4, 1>;
using CausalD256H16Kv2 = CausalAttentionGeometry<16, 2, 2>;
// Qwen3.5-0.8B. SmallTSplitScale is 2, matching CausalD256H16Kv2 — its nearest neighbour by KV
// head count — and NOT 4, which the (24,4)->1, (16,2)->2 progression invites. The consumers in
// small_t.cu handle exactly the values 1 and 2: :48 is an `if constexpr (== 1)` special case and
// :61 is the binary `SmallTSplitScale == 2 ? 17 : 24`, so a 4 would compile, pass the
// `> 0` static_assert in CausalAttentionGeometry, and silently receive the scale-1 tuning.
// Treat this as a value to sweep and measure, never to derive; going above 2 means extending
// those two sites first.
using CausalD256H8Kv2 = CausalAttentionGeometry<8, 2, 2>;

// Selecting a geometry from the QUERY head count alone and reaching the rest by unconditional
// fallthrough — the shape every dispatch in this directory used to have — is unsafe for two
// independent reasons, and only the first is caught by checking that QHeads are distinct:
//
//   1. COLLISION. Qwen3.5-9B/-4B are [D,Hq,Hkv] = [256,16,4]; QHeads 16 collides with
//      CausalD256H16Kv2 and would execute with KVHeads = 2.
//   2. A MISSING ARM. Even with pairwise-distinct QHeads, a fallthrough gives the LAST geometry
//      everything it does not explicitly test. Adding CausalD256H8Kv2 (QHeads 8, distinct from
//      24 and 16) to require_causal_geometry without touching the sites would have routed the
//      0.8B straight into the CausalD256H16Kv2 arm. Distinctness is necessary and NOT sufficient.
//
// So the selection lives here, once, keyed on the (QHeads, KVHeads) PAIR, and ends in a throw
// rather than a fallthrough. Adding a geometry means adding one line below and nothing else;
// every call site is correct by construction. cache.num_kv_heads is in scope at each of them.
template <class Launch>
void dispatch_causal_geometry(std::int32_t query_heads, std::int32_t kv_heads, Launch&& launch) {
    if (query_heads == CausalD256H24Kv4::QHeads && kv_heads == CausalD256H24Kv4::KVHeads) {
        launch(CausalD256H24Kv4{});
        return;
    }
    if (query_heads == CausalD256H16Kv2::QHeads && kv_heads == CausalD256H16Kv2::KVHeads) {
        launch(CausalD256H16Kv2{});
        return;
    }
    if (query_heads == CausalD256H8Kv2::QHeads && kv_heads == CausalD256H8Kv2::KVHeads) {
        launch(CausalD256H8Kv2{});
        return;
    }
    throw std::invalid_argument(
        "causal attention: unsupported (QHeads, KVHeads); add the geometry to "
        "dispatch_causal_geometry in geometry.cuh");
}

} // namespace ninfer::ops
