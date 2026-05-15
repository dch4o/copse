// Microbenchmark: insert per-call cost across capacities, batch sizes, and
// FIFO-eviction regime. TEST_CASEs slice the matrix differently — batch-size
// sweep, full-tree warm path, capacity sweep × cold/warm regime.

#include "topiary/topiary.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace topiary {

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

} // namespace

TEST_CASE("Bench: batched insert into KDTree3", "[!benchmark][insert]") {
    // Cold-tree insert at two headline batch sizes. Reported time covers fresh
    // tree construction plus the single insert call.

    constexpr std::uint64_t kSeed = 0xB47C4500'1A5E1ULL;

    const auto batch_1k  = make_points(1'000, kSeed);
    const auto batch_10k = make_points(10'000, kSeed ^ 0x1u);

    BENCHMARK("insert batch of 1000 (3)") {
        Tree tree{make_config(100'000)};
        return tree.insert(batch_1k);
    };

    BENCHMARK("insert batch of 10000 (3)") {
        Tree tree{make_config(100'000)};
        return tree.insert(batch_10k);
    };
}

TEST_CASE("Bench: insert batch-size sweep, cold tree", "[!benchmark][insert][sweep]") {
    // Sweep batch ∈ {100, 1k, 10k} at capacity 100k and 1M. Each iteration
    // constructs a fresh empty tree and inserts the batch once, so timings
    // include construction cost.

    constexpr std::uint64_t kSeed = 0xB47C4500'1A5E1ULL;

    const auto batch_100 = make_points(100, kSeed);
    const auto batch_1k  = make_points(1'000, kSeed ^ 0x1u);
    const auto batch_10k = make_points(10'000, kSeed ^ 0x2u);

    BENCHMARK("cold insert: capacity 100k, batch 100") {
        Tree tree{make_config(100'000)};
        return tree.insert(batch_100);
    };

    BENCHMARK("cold insert: capacity 100k, batch 1k") {
        Tree tree{make_config(100'000)};
        return tree.insert(batch_1k);
    };

    BENCHMARK("cold insert: capacity 100k, batch 10k") {
        Tree tree{make_config(100'000)};
        return tree.insert(batch_10k);
    };

    BENCHMARK("cold insert: capacity 1M, batch 100") {
        Tree tree{make_config(1'000'000)};
        return tree.insert(batch_100);
    };

    BENCHMARK("cold insert: capacity 1M, batch 1k") {
        Tree tree{make_config(1'000'000)};
        return tree.insert(batch_1k);
    };

    BENCHMARK("cold insert: capacity 1M, batch 10k") {
        Tree tree{make_config(1'000'000)};
        return tree.insert(batch_10k);
    };
}

TEST_CASE("Bench: insert into a full 1M-capacity tree (FIFO eviction)", "[!benchmark][insert][fifo]") {
    // Warm path: capacity-1M tree pre-filled to full, then a single 1k batch
    // measured. Every inserted point evicts the FIFO head.

    constexpr std::uint64_t kSeed = 0xB47C4500'1A5E1ULL;

    BENCHMARK_ADVANCED("insert into full 1M tree (3)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill  = make_points(1'000'000, kSeed ^ 0xF0u);
        const auto batch = make_points(1'000, kSeed ^ 0xF1u);
        Tree       tree{make_config(1'000'000)};
        tree.insert(fill);
        meter.measure([&] { return tree.insert(batch); });
    };
}

TEST_CASE("Bench: cold vs warm insert across capacity (D=3, batch=10k)", "[!benchmark][insert][capacity]") {
    // Capacity sweep × cold/warm regime. Cold rows construct a fresh tree per
    // iteration (time dominated by per-tree construction at large N); warm rows
    // pre-fill the tree to capacity, so every measured batch insert evicts
    // batch_size FIFO-head occupants.

    constexpr std::uint64_t kSeed  = 0xA11CE0FFEEULL;
    constexpr std::size_t   kBatch = 10'000;

    const auto batch = make_points(kBatch, kSeed);

    BENCHMARK("cold insert: capacity 50k, batch 10k") {
        Tree tree{make_config(50'000)};
        return tree.insert(batch);
    };

    BENCHMARK_ADVANCED("warm insert (FIFO eviction): capacity 50k, batch 10k")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill = make_points(50'000, kSeed ^ 0x1u);
        Tree       tree{make_config(50'000)};
        tree.insert(fill);
        meter.measure([&] { return tree.insert(batch); });
    };

    BENCHMARK("cold insert: capacity 100k, batch 10k") {
        Tree tree{make_config(100'000)};
        return tree.insert(batch);
    };

    BENCHMARK_ADVANCED("warm insert (FIFO eviction): capacity 100k, batch 10k")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill = make_points(100'000, kSeed ^ 0x1u);
        Tree       tree{make_config(100'000)};
        tree.insert(fill);
        meter.measure([&] { return tree.insert(batch); });
    };

    BENCHMARK("cold insert: capacity 500k, batch 10k") {
        Tree tree{make_config(500'000)};
        return tree.insert(batch);
    };

    BENCHMARK_ADVANCED("warm insert (FIFO eviction): capacity 500k, batch 10k")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill = make_points(500'000, kSeed ^ 0x1u);
        Tree       tree{make_config(500'000)};
        tree.insert(fill);
        meter.measure([&] { return tree.insert(batch); });
    };

    BENCHMARK("cold insert: capacity 1M, batch 10k") {
        Tree tree{make_config(1'000'000)};
        return tree.insert(batch);
    };

    BENCHMARK_ADVANCED("warm insert (FIFO eviction): capacity 1M, batch 10k")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill = make_points(1'000'000, kSeed ^ 0x1u);
        Tree       tree{make_config(1'000'000)};
        tree.insert(fill);
        meter.measure([&] { return tree.insert(batch); });
    };
}

} // namespace topiary
