#pragma once

#include "ops/softmax_attention/common/head_mapping.cuh"

namespace ninfer::ops {

template <int QHeadsValue, int KVHeadsValue, int SmallTSplitScaleValue>
struct CausalAttentionGeometry : AttentionHeadMapping<QHeadsValue, KVHeadsValue> {
    static_assert(SmallTSplitScaleValue > 0);

    static constexpr int SmallTSplitScale    = SmallTSplitScaleValue;
    static constexpr int SmallTMaximumSplits = 85 * SmallTSplitScale;
};

using CausalD256H24Kv4 = CausalAttentionGeometry<24, 4, 1>;
using CausalD256H16Kv2 = CausalAttentionGeometry<16, 2, 2>;

// Most dispatch sites in this directory select a geometry from the QUERY head count alone and
// reach the remaining one by unconditional fallthrough, e.g. prompt.cu:80-86:
//
//     if (q.ne[1] == CausalD256H24Kv4::QHeads) { launch_for<CausalD256H24Kv4>(...); return; }
//     launch_for<CausalD256H16Kv2>(...);              // <- takes everything else
//
// That is sound only while the registered geometries have PAIRWISE DISTINCT QHeads, which 24
// and 16 are. It stops being sound the moment a geometry is added that collides on QHeads
// while differing in KVHeads — Qwen3.5-9B and -4B are [D,Hq,Hkv] = [256,16,4], which collides
// with CausalD256H16Kv2's 16 — because require_causal_geometry (causal_softmax_attention.cpp:37)
// would admit it and the fallthrough would then execute it with KVHeads = 2. Wrong numerics, no
// throw, no warning: the guard that would have objected is the one you just widened.
//
// So adding such a geometry must break the build HERE, not produce silent output at runtime.
// When it does: give every dispatch in this directory the KV head count as well
// (cache.num_kv_heads is in scope at each one) and make the final arm a throw rather than a
// fallthrough — small_t.cu:225-252 already has that shape and is the model to copy.
static_assert(CausalD256H24Kv4::QHeads != CausalD256H16Kv2::QHeads,
              "registered causal geometries must be discriminable by QHeads alone, because the "
              "dispatch sites in this directory select on QHeads and fall through. Before adding "
              "a geometry that collides on QHeads, convert those sites to select on the "
              "(QHeads, KVHeads) pair and to throw on no match.");

} // namespace ninfer::ops
