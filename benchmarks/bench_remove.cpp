// Microbenchmark: remove per-call cost across query-list sizes at fixed N.
// Covers the remove path (collect_indices_within + tombstone_index + release
// per match + end-of-batch maybe_partial_rebuild). Query coordinates are
// sampled from the prefill so every query matches at least one live point,
// avoiding the "uniform queries miss everything" trap.

#include "copse/copse.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace copse {

namespace {

constexpr float kResolution = 1e-6f; // small enough that random points never dedup-collide.

using Tree  = KDTree3;
using Point = Tree::Point;

std::vector<Point> make_points(std::size_t count, std::uint64_t seed) {
    std::mt19937_64                       rng{seed};
    std::uniform_real_distribution<float> dist{0.0f, 1.0f};
    std::vector<Point>                    out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.emplace_back(dist(rng), dist(rng), dist(rng));
    }
    return out;
}

Tree::Config make_config(std::size_t capacity) {
    Tree::Config cfg;
    cfg.capacity   = capacity;
    cfg.resolution = kResolution;
    return cfg;
}

std::vector<Point> sample_queries(const std::vector<Point>& fill, std::size_t count, std::uint64_t seed) {
    std::mt19937_64                            rng{seed};
    std::uniform_int_distribution<std::size_t> pick{0, fill.size() - 1};
    std::vector<Point>                         queries;
    queries.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        queries.emplace_back(fill[pick(rng)]);
    }
    return queries;
}

} // namespace

TEST_CASE("Bench: remove query-count sweep, 1M points", "[!benchmark][remove][sweep]") {
    // Pre-fill 1M, then run remove() against query lists of varying size. Every
    // query is a coordinate drawn from the prefill, so each match is guaranteed.

    constexpr std::uint64_t kSeed     = 0xD5E2C400'BEEFULL;
    constexpr std::size_t   kCapacity = 1'000'000;

    BENCHMARK_ADVANCED("remove 1k queries (3, N=1M)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill    = make_points(kCapacity, kSeed ^ 0x1u);
        const auto queries = sample_queries(fill, 1'000, kSeed ^ 0x3u);
        Tree       tree{make_config(kCapacity)};
        tree.insert(fill);
        meter.measure([&] { return tree.remove(queries); });
    };

    BENCHMARK_ADVANCED("remove 10k queries (3, N=1M)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill    = make_points(kCapacity, kSeed ^ 0x1u);
        const auto queries = sample_queries(fill, 10'000, kSeed ^ 0x4u);
        Tree       tree{make_config(kCapacity)};
        tree.insert(fill);
        meter.measure([&] { return tree.remove(queries); });
    };
}

} // namespace copse
