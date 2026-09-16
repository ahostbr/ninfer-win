// attention_pairs() accumulates a 128-bit value in two 64-bit limbs so it compiles under MSVC,
// which has no __int128. Where __int128 exists it is the independent oracle: the saturating
// 128-bit expression this function replaced. Under MSVC there is no such type, so the test falls
// back to fixed expectations computed from that oracle on a platform that has it.

#include "runtime/contract/resources.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

int failures = 0;

void expect_eq(std::uint64_t actual, std::uint64_t expected, std::uint64_t prefix,
               std::uint64_t suffix) {
    if (actual == expected) { return; }
    std::cerr << "attention_pairs(" << prefix << ", " << suffix << ") = " << actual
              << ", expected " << expected << '\n';
    ++failures;
}

#if defined(__SIZEOF_INT128__)

// The pre-port expression, kept only here as the oracle.
std::uint64_t oracle(std::uint64_t prefix_tokens, std::uint64_t suffix_tokens) {
    const unsigned __int128 suffix      = suffix_tokens;
    const unsigned __int128 linear      = static_cast<unsigned __int128>(prefix_tokens) * suffix;
    const unsigned __int128 triangular  = suffix * (suffix + 1U) / 2U;
    constexpr unsigned __int128 maximum = ~static_cast<unsigned __int128>(0);
    const unsigned __int128 attention =
        triangular > maximum - linear ? maximum : linear + triangular;
    return attention > kMax ? kMax : static_cast<std::uint64_t>(attention);
}

void compare_against_oracle() {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> cases{
        {0, 0},        {0, 1},        {1, 0},          {1, 1},        {10, 4},
        {0, 3},        {0, kMax},     {kMax, kMax},    {kMax, 0},     {kMax, 1},
        {1, kMax},     {0, kMax - 1}, {kMax, kMax - 1}, {1ULL << 32U, 1ULL << 32U},
        {1ULL << 63U, 2},            {262144, 262144}, {0, 6074001000ULL},
        {0, (1ULL << 32U) - 1},      {0, 1ULL << 32U},
    };
    std::mt19937_64 rng(20260916);
    for (int i = 0; i < 20000; ++i) { cases.emplace_back(rng(), rng()); }
    // Realistic token counts, where nothing saturates and a wrong halving would still show.
    std::uniform_int_distribution<std::uint64_t> small(0, 10'000'000);
    for (int i = 0; i < 20000; ++i) { cases.emplace_back(small(rng), small(rng)); }

    for (const auto& [prefix, suffix] : cases) {
        expect_eq(ninfer::runtime::attention_pairs(prefix, suffix), oracle(prefix, suffix), prefix,
                  suffix);
    }
    std::cout << "compared " << cases.size() << " cases against the __int128 oracle\n";
}

#else

// MSVC: the oracle type does not exist. These expectations were produced by the oracle above.
void compare_against_oracle() {
    expect_eq(ninfer::runtime::attention_pairs(0, 3), 6, 0, 3);
    expect_eq(ninfer::runtime::attention_pairs(10, 4), 50, 10, 4);
    expect_eq(ninfer::runtime::attention_pairs(0, 0), 0, 0, 0);
    expect_eq(ninfer::runtime::attention_pairs(1, 1), 2, 1, 1);
    // 262144*262144 + 262144*262145/2 = 68'719'476'736 + 34'359'869'440
    expect_eq(ninfer::runtime::attention_pairs(262144, 262144), 103'079'346'176ULL, 262144, 262144);
    // Saturation: both terms overflow 64 bits.
    expect_eq(ninfer::runtime::attention_pairs(0, kMax), kMax, 0, kMax);
    expect_eq(ninfer::runtime::attention_pairs(kMax, kMax), kMax, kMax, kMax);
    expect_eq(ninfer::runtime::attention_pairs(kMax, 1), kMax, kMax, 1);
    // An odd suffix, where the halving must come from the 128-bit product and not the operand.
    expect_eq(ninfer::runtime::attention_pairs(0, (1ULL << 32U) - 1),
              9'223'372'034'707'292'160ULL, 0, (1ULL << 32U) - 1);
    // The only shape that exercises the carry-in: suffix*(suffix+1) needs 65 bits (high limb 1),
    // but halving brings it back under 64, so the shifted-in bit is the whole answer. Without
    // "| (triangular_high << 63)" this returns 2^31 instead.
    expect_eq(ninfer::runtime::attention_pairs(0, 1ULL << 32U), 9'223'372'039'002'259'456ULL, 0,
              1ULL << 32U);
    expect_eq(ninfer::runtime::attention_pairs(1, 1ULL << 32U), 9'223'372'043'297'226'752ULL, 1,
              1ULL << 32U);
    std::cout << "compared fixed oracle-derived expectations (no __int128 on this compiler)\n";
}

#endif

// make_prefill_work must report exactly what attention_pairs returns.
void prefill_work_uses_it() {
    const auto work = ninfer::runtime::make_prefill_work(1000, 500, 0, 0, 128);
    expect_eq(work.attention_pairs, ninfer::runtime::attention_pairs(1000, 500), 1000, 500);
    expect_eq(work.tokens, 500, 1000, 500);
    expect_eq(work.chunks, 4, 1000, 500);
}

} // namespace

int main() {
    compare_against_oracle();
    prefill_work_uses_it();
    if (failures != 0) {
        std::cerr << failures << " attention_pairs failure(s)\n";
        return 1;
    }
    std::cout << "attention_pairs: ok\n";
    return 0;
}
