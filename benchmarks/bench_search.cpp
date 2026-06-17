// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
// Microbenchmark: search-path latency. Single-query measurements against a
// pre-built tree (knn k=1/8/32, radius, hybrid) and mixed-cycle throughput
// (1 small insert batch followed by a query burst) at two live-point scales.

#include "copse/kd_tree.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace copse {

namespace {

constexpr std::uint64_t kSeed        = 0x5EA2C400'BEEFULL;
constexpr std::uint64_t kQuerySeed   = 0xC0FFEE00'9527ULL;
constexpr float         kResolution  = 1e-6f;
constexpr float         kCoordExtent = 100.0f; // spread queries out so density isn't artificially high.
constexpr std::size_t   kPrefill     = 100'000;
constexpr std::size_t   kQueryPool   = 256;
constexpr float         kRadius      = 5.0f; // ~10–50 hits on uniform 100k points in [0,100)^3.

using Tree  = KDTree3;
using Point = Tree::Point;

std::vector<Point> make_points(std::size_t count, std::uint64_t seed, float extent) {
    std::mt19937_64                       rng{seed};
    std::uniform_real_distribution<float> dist{0.0f, extent};
    std::vector<Point>                    out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(Point{dist(rng), dist(rng), dist(rng)});
    }
    return out;
}

Tree::Config make_config(std::size_t capacity) {
    Tree::Config cfg;
    cfg.capacity   = capacity;
    cfg.resolution = kResolution;
    return cfg;
}

Tree build_prefilled_tree() {
    Tree       tree{make_config(1'000'000)};
    const auto fill = make_points(kPrefill, kSeed, kCoordExtent);
    tree.insert(fill);
    return tree;
}

Tree build_prefilled_tree_n(std::size_t n) {
    Tree       tree{make_config(n)};
    const auto fill = make_points(n, kSeed, kCoordExtent);
    tree.insert(fill);
    return tree;
}

} // namespace

TEST_CASE("Bench: knn_search at varying k", "[!benchmark][search][knn]") {
    // Each BENCHMARK_ADVANCED builds the tree and a fixed query pool once, then
    // the timed inner action picks a query by atomic-counter round-robin so
    // every iteration sees a different point without paying RNG cost in the hot
    // path.

    BENCHMARK_ADVANCED("knn k=1 (3)")(Catch::Benchmark::Chronometer meter) {
        Tree                     tree    = build_prefilled_tree();
        const auto               queries = make_points(kQueryPool, kQuerySeed, kCoordExtent);
        std::atomic<std::size_t> cursor{0};
        meter.measure([&] {
            const auto& q = queries[cursor.fetch_add(1, std::memory_order_relaxed) % queries.size()];
            return tree.knn_search(q, 1);
        });
    };

    BENCHMARK_ADVANCED("knn k=8 (3)")(Catch::Benchmark::Chronometer meter) {
        Tree                     tree    = build_prefilled_tree();
        const auto               queries = make_points(kQueryPool, kQuerySeed, kCoordExtent);
        std::atomic<std::size_t> cursor{0};
        meter.measure([&] {
            const auto& q = queries[cursor.fetch_add(1, std::memory_order_relaxed) % queries.size()];
            return tree.knn_search(q, 8);
        });
    };

    BENCHMARK_ADVANCED("knn k=32 (3)")(Catch::Benchmark::Chronometer meter) {
        Tree                     tree    = build_prefilled_tree();
        const auto               queries = make_points(kQueryPool, kQuerySeed, kCoordExtent);
        std::atomic<std::size_t> cursor{0};
        meter.measure([&] {
            const auto& q = queries[cursor.fetch_add(1, std::memory_order_relaxed) % queries.size()];
            return tree.knn_search(q, 32);
        });
    };
}

TEST_CASE("Bench: radius_search", "[!benchmark][search][radius]") {
    // r=5 on uniform 100k points in [0,100)^3 yields O(10) expected hits per
    // query; small enough to keep the result vector cheap, large enough to
    // exercise non-trivial branch traversal.

    BENCHMARK_ADVANCED("radius_search r=5.0 (3)")(Catch::Benchmark::Chronometer meter) {
        Tree                     tree    = build_prefilled_tree();
        const auto               queries = make_points(kQueryPool, kQuerySeed, kCoordExtent);
        std::atomic<std::size_t> cursor{0};
        meter.measure([&] {
            const auto& q = queries[cursor.fetch_add(1, std::memory_order_relaxed) % queries.size()];
            return tree.radius_search(q, kRadius);
        });
    };
}

TEST_CASE("Bench: hybrid_search", "[!benchmark][search][hybrid]") {
    // hybrid combines a knn ceiling with a radius cap; at (k=32, r=5.0) on a
    // 100k-point uniform set the radius typically dominates so the result size
    // stays below k.

    BENCHMARK_ADVANCED("hybrid_search k=32 r=5.0 (3)")(Catch::Benchmark::Chronometer meter) {
        Tree                     tree    = build_prefilled_tree();
        const auto               queries = make_points(kQueryPool, kQuerySeed, kCoordExtent);
        std::atomic<std::size_t> cursor{0};
        meter.measure([&] {
            const auto& q = queries[cursor.fetch_add(1, std::memory_order_relaxed) % queries.size()];
            return tree.hybrid_search(q, 32, kRadius);
        });
    };
}

TEST_CASE("Bench: search throughput in mixed insert/query cycle", "[!benchmark][search][mixed]") {
    // One cycle = one 1k insert batch followed by 10k single-point knn_search
    // queries (k=8). Cycle cost is dominated by the query burst; the insert
    // contribution is small. Reported time is the whole cycle.

    constexpr std::uint64_t kMixedSeed      = 0x14820000'A1B5CULL;
    constexpr std::uint64_t kMixedQuerySeed = 0xDEADBEEF'CAFEULL;
    constexpr std::size_t   kInsertBatch    = 1'000;
    constexpr std::size_t   kSearchCount    = 10'000;
    constexpr std::size_t   kKnnK           = 8;

    BENCHMARK_ADVANCED("1k insert + 10k searches (3, N=10k)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill    = make_points(10'000, kMixedSeed ^ 0x10u, kCoordExtent);
        const auto batch   = make_points(kInsertBatch, kMixedSeed ^ 0x11u, kCoordExtent);
        const auto queries = make_points(kSearchCount, kMixedQuerySeed, kCoordExtent);
        Tree       tree{make_config(100'000)};
        tree.insert(fill);
        meter.measure([&] {
            tree.insert(batch);
            std::size_t accum = 0;
            for (const auto& q : queries) {
                accum += tree.knn_search(q, kKnnK).size();
            }
            return accum;
        });
    };

    BENCHMARK_ADVANCED("1k insert + 10k searches (3, N=500k)")
    (Catch::Benchmark::Chronometer meter) {
        const auto fill    = make_points(500'000, kMixedSeed ^ 0x20u, kCoordExtent);
        const auto batch   = make_points(kInsertBatch, kMixedSeed ^ 0x21u, kCoordExtent);
        const auto queries = make_points(kSearchCount, kMixedQuerySeed ^ 0x1u, kCoordExtent);
        Tree       tree{make_config(1'000'000)};
        tree.insert(fill);
        meter.measure([&] {
            tree.insert(batch);
            std::size_t accum = 0;
            for (const auto& q : queries) {
                accum += tree.knn_search(q, kKnnK).size();
            }
            return accum;
        });
    };
}

TEST_CASE("Bench: knn_search N sweep (k=8)", "[!benchmark][search][knn][sweep]") {
    // Live-point sweep at fixed k=8. Each row builds a tree at capacity matching
    // its prefill so growth is paid once, outside the timed inner action. Query
    // pool is 256 points indexed by atomic round-robin, mirroring the per-k
    // benchmarks above.

    BENCHMARK_ADVANCED("knn k=8 (3, N=50k)")(Catch::Benchmark::Chronometer meter) {
        Tree                     tree    = build_prefilled_tree_n(50'000);
        const auto               queries = make_points(kQueryPool, kQuerySeed, kCoordExtent);
        std::atomic<std::size_t> cursor{0};
        meter.measure([&] {
            const auto& q = queries[cursor.fetch_add(1, std::memory_order_relaxed) % queries.size()];
            return tree.knn_search(q, 8);
        });
    };

    BENCHMARK_ADVANCED("knn k=8 (3, N=100k)")(Catch::Benchmark::Chronometer meter) {
        Tree                     tree    = build_prefilled_tree_n(100'000);
        const auto               queries = make_points(kQueryPool, kQuerySeed, kCoordExtent);
        std::atomic<std::size_t> cursor{0};
        meter.measure([&] {
            const auto& q = queries[cursor.fetch_add(1, std::memory_order_relaxed) % queries.size()];
            return tree.knn_search(q, 8);
        });
    };

    BENCHMARK_ADVANCED("knn k=8 (3, N=500k)")(Catch::Benchmark::Chronometer meter) {
        Tree                     tree    = build_prefilled_tree_n(500'000);
        const auto               queries = make_points(kQueryPool, kQuerySeed, kCoordExtent);
        std::atomic<std::size_t> cursor{0};
        meter.measure([&] {
            const auto& q = queries[cursor.fetch_add(1, std::memory_order_relaxed) % queries.size()];
            return tree.knn_search(q, 8);
        });
    };

    BENCHMARK_ADVANCED("knn k=8 (3, N=1M)")(Catch::Benchmark::Chronometer meter) {
        Tree                     tree    = build_prefilled_tree_n(1'000'000);
        const auto               queries = make_points(kQueryPool, kQuerySeed, kCoordExtent);
        std::atomic<std::size_t> cursor{0};
        meter.measure([&] {
            const auto& q = queries[cursor.fetch_add(1, std::memory_order_relaxed) % queries.size()];
            return tree.knn_search(q, 8);
        });
    };
}

TEST_CASE("Bench: radius_search radius sweep", "[!benchmark][search][radius][sweep]") {
    // Radius sweep at fixed N=100k. r=5.0 row duplicates the standalone radius
    // TEST_CASE so all four points appear on one chart. Expected hit counts at
    // 100k uniform in [0,100)^3: r=0.5 -> ~0 hits, r=2.0 -> ~3 hits, r=5.0 -> ~50,
    // r=10.0 -> ~400.

    BENCHMARK_ADVANCED("radius_search r=0.5 (3, N=100k)")(Catch::Benchmark::Chronometer meter) {
        Tree                     tree    = build_prefilled_tree();
        const auto               queries = make_points(kQueryPool, kQuerySeed, kCoordExtent);
        std::atomic<std::size_t> cursor{0};
        meter.measure([&] {
            const auto& q = queries[cursor.fetch_add(1, std::memory_order_relaxed) % queries.size()];
            return tree.radius_search(q, 0.5f);
        });
    };

    BENCHMARK_ADVANCED("radius_search r=2.0 (3, N=100k)")(Catch::Benchmark::Chronometer meter) {
        Tree                     tree    = build_prefilled_tree();
        const auto               queries = make_points(kQueryPool, kQuerySeed, kCoordExtent);
        std::atomic<std::size_t> cursor{0};
        meter.measure([&] {
            const auto& q = queries[cursor.fetch_add(1, std::memory_order_relaxed) % queries.size()];
            return tree.radius_search(q, 2.0f);
        });
    };

    BENCHMARK_ADVANCED("radius_search r=5.0 (3, N=100k)")(Catch::Benchmark::Chronometer meter) {
        Tree                     tree    = build_prefilled_tree();
        const auto               queries = make_points(kQueryPool, kQuerySeed, kCoordExtent);
        std::atomic<std::size_t> cursor{0};
        meter.measure([&] {
            const auto& q = queries[cursor.fetch_add(1, std::memory_order_relaxed) % queries.size()];
            return tree.radius_search(q, 5.0f);
        });
    };

    BENCHMARK_ADVANCED("radius_search r=10.0 (3, N=100k)")(Catch::Benchmark::Chronometer meter) {
        Tree                     tree    = build_prefilled_tree();
        const auto               queries = make_points(kQueryPool, kQuerySeed, kCoordExtent);
        std::atomic<std::size_t> cursor{0};
        meter.measure([&] {
            const auto& q = queries[cursor.fetch_add(1, std::memory_order_relaxed) % queries.size()];
            return tree.radius_search(q, 10.0f);
        });
    };
}

} // namespace copse
