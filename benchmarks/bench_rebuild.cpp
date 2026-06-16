// Microbenchmark: rebuild tail latency at N=1M. Pairs partial-rebuild
// steady-state cost (insert that may trigger a scapegoat rebuild) against
// the explicit full rebuild_all() reference.

#include "copse/kd_tree.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace copse {

namespace {

constexpr std::uint64_t kSeed        = 0x7E8C1100'2EBD1ULL;
constexpr float         kResolution  = 1e-6f;
constexpr float         kCoordExtent = 100.0f;
constexpr std::size_t   kCapacity    = 1'000'000;
constexpr std::size_t   kSteadyBatch = 5'000;

using Tree  = KDTree3;
using Point = Tree::Point;

std::vector<Point> make_points(std::size_t count, std::uint64_t seed, float extent) {
    std::mt19937_64                       rng{seed};
    std::uniform_real_distribution<float> dist{0.0f, extent};
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

TEST_CASE("Bench: partial rebuild tail latency, 1M points", "[!benchmark][rebuild][partial]") {
    // Steady-state proxy: a capacity-1M tree pre-filled to full, then a
    // moderately sized batch (5k) is inserted per measurement iteration.
    // FIFO eviction plus α-imbalance bookkeeping will fire one or more
    // partial rebuilds across the batch; the reported per-iteration time is
    // the end-to-end batch insert cost, which folds those rebuilds in.
    // (See bench_rebuild.md companion report for the rebuild-frequency
    // accounting.)

    BENCHMARK_ADVANCED("partial rebuild (3, N=1M)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill  = make_points(kCapacity, kSeed ^ 0x1u, kCoordExtent);
        const auto batch = make_points(kSteadyBatch, kSeed ^ 0x2u, kCoordExtent);
        Tree       tree{make_config(kCapacity)};
        tree.insert(fill);
        meter.measure([&] { return tree.insert(batch); });
    };
}

TEST_CASE("Bench: rebuild_all reference, 1M points", "[!benchmark][rebuild][full]") {
    // Reference upper bound: explicit full rebuild over 1M live points.
    // Useful as a cap for "how bad can the worst rebuild be" when comparing
    // against the partial-rebuild row above.

    BENCHMARK_ADVANCED("rebuild_all (3, N=1M)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill = make_points(kCapacity, kSeed ^ 0x1u, kCoordExtent);
        Tree       tree{make_config(kCapacity)};
        tree.insert(fill);
        meter.measure([&] { tree.rebuild_all(); });
    };
}

TEST_CASE("Bench: rebuild N sweep (partial + full)", "[!benchmark][rebuild][sweep]") {
    // N sweep of both ops: partial-rebuild steady-state batch-insert proxy
    // (same shape as the 1M case above) and rebuild_all reference. Each
    // BENCHMARK_ADVANCED owns its `fill` so two large vectors never coexist.

    BENCHMARK_ADVANCED("partial rebuild (3, N=50k)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill  = make_points(50'000, kSeed ^ 0x01u, kCoordExtent);
        const auto batch = make_points(kSteadyBatch, kSeed ^ 0x02u, kCoordExtent);
        Tree       tree{make_config(50'000)};
        tree.insert(fill);
        meter.measure([&] { return tree.insert(batch); });
    };

    BENCHMARK_ADVANCED("rebuild_all (3, N=50k)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill = make_points(50'000, kSeed ^ 0x01u, kCoordExtent);
        Tree       tree{make_config(50'000)};
        tree.insert(fill);
        meter.measure([&] { tree.rebuild_all(); });
    };

    BENCHMARK_ADVANCED("partial rebuild (3, N=100k)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill  = make_points(100'000, kSeed ^ 0x11u, kCoordExtent);
        const auto batch = make_points(kSteadyBatch, kSeed ^ 0x12u, kCoordExtent);
        Tree       tree{make_config(100'000)};
        tree.insert(fill);
        meter.measure([&] { return tree.insert(batch); });
    };

    BENCHMARK_ADVANCED("rebuild_all (3, N=100k)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill = make_points(100'000, kSeed ^ 0x11u, kCoordExtent);
        Tree       tree{make_config(100'000)};
        tree.insert(fill);
        meter.measure([&] { tree.rebuild_all(); });
    };

    BENCHMARK_ADVANCED("partial rebuild (3, N=500k)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill  = make_points(500'000, kSeed ^ 0x18u, kCoordExtent);
        const auto batch = make_points(kSteadyBatch, kSeed ^ 0x19u, kCoordExtent);
        Tree       tree{make_config(500'000)};
        tree.insert(fill);
        meter.measure([&] { return tree.insert(batch); });
    };

    BENCHMARK_ADVANCED("rebuild_all (3, N=500k)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill = make_points(500'000, kSeed ^ 0x18u, kCoordExtent);
        Tree       tree{make_config(500'000)};
        tree.insert(fill);
        meter.measure([&] { tree.rebuild_all(); });
    };

    BENCHMARK_ADVANCED("partial rebuild (3, N=1M)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill  = make_points(1'000'000, kSeed ^ 0x21u, kCoordExtent);
        const auto batch = make_points(kSteadyBatch, kSeed ^ 0x22u, kCoordExtent);
        Tree       tree{make_config(1'000'000)};
        tree.insert(fill);
        meter.measure([&] { return tree.insert(batch); });
    };

    BENCHMARK_ADVANCED("rebuild_all (3, N=1M)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill = make_points(1'000'000, kSeed ^ 0x21u, kCoordExtent);
        Tree       tree{make_config(1'000'000)};
        tree.insert(fill);
        meter.measure([&] { tree.rebuild_all(); });
    };
}

TEST_CASE("Bench: degenerate cluster insert (LeafBucket overflow path), 1M points",
          "[!benchmark][rebuild][degenerate]") {
    // 1k points jittered around a single center by ±kResolution * 1000 (well
    // below the 1e-3 cluster cap). Dedup-safe because jitter > resolution per
    // axis. The cluster routes every point through the same root-side path,
    // forcing repeated LeafBucket::push overflow → eager-split work.

    constexpr std::size_t kDegenerateBatch  = 1'000;
    constexpr float       kClusterCenter    = 50.0f;
    constexpr float       kClusterJitterMax = 1e-3f;

    BENCHMARK_ADVANCED("degenerate cluster insert (3, N=1M, 1k batch)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill = make_points(kCapacity, kSeed ^ 0x41u, kCoordExtent);

        std::vector<Point> batch;
        batch.reserve(kDegenerateBatch);
        std::mt19937_64                       rng{kSeed ^ 0x42u};
        std::uniform_real_distribution<float> jitter{-kClusterJitterMax, kClusterJitterMax};
        for (std::size_t i = 0; i < kDegenerateBatch; ++i) {
            batch.emplace_back(
                kClusterCenter + jitter(rng), kClusterCenter + jitter(rng), kClusterCenter + jitter(rng));
        }

        Tree tree{make_config(kCapacity)};
        tree.insert(fill);
        meter.measure([&] { return tree.insert(batch); });
    };
}

TEST_CASE("Bench: tombstone-triggered partial rebuild, 1M points", "[!benchmark][rebuild][tombstone]") {
    // Best-effort tombstone-fraction trigger. Pre-fill 1M, remove ~30% of the
    // inserted points (uniformly drawn from the fill), then measure a small
    // batch insert. The expectation is that at least one subtree crosses
    // Config::tombstone_threshold = 0.25 and the next insert fires a partial
    // rebuild. Single row; flagged as best-effort in the label.

    constexpr std::size_t kRemoveFraction = 300'000; // ~30% of 1M
    constexpr std::size_t kSmallBatch     = 1'000;

    BENCHMARK_ADVANCED("tombstone-triggered partial rebuild (3, N=1M, best-effort)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill  = make_points(kCapacity, kSeed ^ 0x51u, kCoordExtent);
        const auto batch = make_points(kSmallBatch, kSeed ^ 0x52u, kCoordExtent);

        // Sample remove-targets directly from the prefill so every query matches
        // a live point. Fixed seed keeps the sample reproducible per iteration.
        std::vector<Point> remove_queries;
        remove_queries.reserve(kRemoveFraction);
        std::mt19937_64                            rng{kSeed ^ 0x53u};
        std::uniform_int_distribution<std::size_t> pick{0, fill.size() - 1};
        for (std::size_t i = 0; i < kRemoveFraction; ++i) {
            remove_queries.emplace_back(fill[pick(rng)]);
        }

        Tree tree{make_config(kCapacity)};
        tree.insert(fill);
        tree.remove(remove_queries);
        meter.measure([&] { return tree.insert(batch); });
    };
}

} // namespace copse
