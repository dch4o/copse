// BDD tests for SearchKernel<Dim>: brute-force agreement on random inputs across D ∈ {2, 3, 4}.

#include "copse/impl/leaf_bucket.hpp"
#include "copse/impl/point_store.hpp"
#include "copse/impl/search_kernel.hpp"
#include "copse/impl/tree_builder.hpp"
#include "copse/impl/tree_node.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <random>
#include <utility>
#include <vector>

namespace copse::internal {

namespace {

template <int Dim>
std::vector<std::pair<std::uint32_t, float>> brute_force_knn(
    const std::vector<detail::PointType<Dim>>& points, const detail::PointType<Dim>& query, std::size_t k) {
    std::vector<std::pair<std::uint32_t, float>> pairs;
    pairs.reserve(points.size());
    for (std::uint32_t i = 0; i < points.size(); ++i) {
        pairs.emplace_back(i, (points[i] - query).squaredNorm());
    }
    std::sort(
        pairs.begin(), pairs.end(), [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });
    if (pairs.size() > k) {
        pairs.resize(k);
    }
    return pairs;
}

template <int Dim>
std::vector<float> brute_force_radius(const std::vector<detail::PointType<Dim>>& points,
                                      const detail::PointType<Dim>&              query,
                                      float                                      sq_radius) {
    std::vector<float> matches;
    for (const auto& point : points) {
        const float sq_dist = (point - query).squaredNorm();
        if (sq_dist < sq_radius) {
            matches.push_back(sq_dist);
        }
    }
    std::sort(matches.begin(), matches.end());
    return matches;
}

} // namespace

SCENARIO("SearchKernel returns empty when the root is INVALID", "[search_kernel][empty]") {
    GIVEN("an empty node pool and an INVALID root") {
        PointStore<3>         points{4};
        LeafBucket            leaf_buckets{8};
        std::vector<TreeNode> nodes;
        SearchKernel<3>       kernel{nodes, leaf_buckets, points};

        WHEN("search is invoked") {
            const auto result = kernel.search(
                TreeNode::INVALID, detail::PointType<3>{0, 0, 0}, 5, std::numeric_limits<float>::infinity());
            THEN("the result is empty") {
                REQUIRE(result.empty());
            }
        }
    }
}

TEMPLATE_TEST_CASE_SIG("SearchKernel agrees with the brute-force oracle for varying D and k",
                       "[search_kernel][oracle]",
                       ((int Dim, int K), Dim, K),
                       (2, 1),
                       (3, 1),
                       (4, 1),
                       (2, 10),
                       (3, 10),
                       (4, 10)) {
    GIVEN("a tree built from 256 uniform random points in D-space") {
        using P = detail::PointType<Dim>;

        constexpr std::size_t N = 256;

        std::mt19937 rng{0xC0FFEEu + static_cast<unsigned>(Dim) * 17u + static_cast<unsigned>(K)};
        std::uniform_real_distribution<float> coord{-10.0f, 10.0f};

        PointStore<Dim>        points{N};
        LeafBucket             leaf_buckets{N * 2};
        std::vector<TreeNode>  nodes;
        std::vector<BBox<Dim>> leaf_bboxes;
        BBox<Dim>              root_bbox{};
        TreeBuilder<Dim>       builder{nodes,
                                 leaf_buckets,
                                 leaf_bboxes,
                                 root_bbox,
                                 points,
                                 /*leaf_bucket_size=*/16,
                                 0.7f,
                                 0.25f};

        std::vector<P>             coords;
        std::vector<std::uint32_t> live_indices;
        coords.reserve(N);
        live_indices.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            P point;
            for (int d = 0; d < Dim; ++d) {
                point[d] = coord(rng);
            }
            coords.push_back(point);
            live_indices.push_back(points.acquire(point));
        }
        const auto root = builder.rebuild(live_indices);

        SearchKernel<Dim> kernel{nodes, leaf_buckets, points};

        WHEN("knn search runs against 16 random queries with k=K") {
            THEN("each result matches the brute-force oracle and is ascending") {
                for (int trial = 0; trial < 16; ++trial) {
                    P query;
                    for (int d = 0; d < Dim; ++d) {
                        query[d] = coord(rng);
                    }
                    const auto result = kernel.search(root, query, K, std::numeric_limits<float>::infinity());
                    const auto expected = brute_force_knn<Dim>(coords, query, K);
                    REQUIRE(result.size() == expected.size());
                    for (std::size_t i = 0; i < result.size(); ++i) {
                        REQUIRE(result[i].sq_dist == expected[i].second);
                    }
                    for (std::size_t i = 1; i < result.size(); ++i) {
                        REQUIRE(result[i - 1].sq_dist <= result[i].sq_dist);
                    }
                }
            }
        }
    }
}

TEMPLATE_TEST_CASE_SIG("SearchKernel radius variant agrees with the brute-force oracle",
                       "[search_kernel][radius][oracle]",
                       ((int Dim), Dim),
                       (2),
                       (3),
                       (4)) {
    GIVEN("a tree built from 256 uniform random points in D-space") {
        using P = detail::PointType<Dim>;

        constexpr std::size_t N = 256;

        std::mt19937                          rng{0xBADF00Du + static_cast<unsigned>(Dim) * 31u};
        std::uniform_real_distribution<float> coord{-10.0f, 10.0f};

        PointStore<Dim>        points{N};
        LeafBucket             leaf_buckets{N * 2};
        std::vector<TreeNode>  nodes;
        std::vector<BBox<Dim>> leaf_bboxes;
        BBox<Dim>              root_bbox{};
        TreeBuilder<Dim>       builder{
            nodes, leaf_buckets, leaf_bboxes, root_bbox, points, /*leaf_bucket_size=*/16, 0.7f, 0.25f};

        std::vector<P>             coords;
        std::vector<std::uint32_t> live_indices;
        coords.reserve(N);
        live_indices.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            P point;
            for (int d = 0; d < Dim; ++d) {
                point[d] = coord(rng);
            }
            coords.push_back(point);
            live_indices.push_back(points.acquire(point));
        }
        const auto root = builder.rebuild(live_indices);

        SearchKernel<Dim> kernel{nodes, leaf_buckets, points};

        WHEN("radius search runs against 16 random queries across several radii") {
            THEN("each result matches the brute-force oracle and is ascending") {
                for (float radius : {1.0f, 3.0f, 8.0f}) {
                    const float sq_radius = radius * radius;
                    for (int trial = 0; trial < 16; ++trial) {
                        P query;
                        for (int d = 0; d < Dim; ++d) {
                            query[d] = coord(rng);
                        }
                        const auto result =
                            kernel.search(root, query, std::numeric_limits<std::size_t>::max(), sq_radius);
                        const auto expected = brute_force_radius<Dim>(coords, query, sq_radius);
                        REQUIRE(result.size() == expected.size());
                        for (std::size_t i = 0; i < result.size(); ++i) {
                            REQUIRE(result[i].sq_dist == expected[i]);
                            REQUIRE(result[i].sq_dist < sq_radius);
                        }
                        for (std::size_t i = 1; i < result.size(); ++i) {
                            REQUIRE(result[i - 1].sq_dist <= result[i].sq_dist);
                        }
                    }
                }
            }
        }
    }
}

SCENARIO("SearchKernel hybrid variant respects both k and radius bounds", "[search_kernel][hybrid][oracle]") {
    GIVEN("a D=3 tree built from 512 uniform random points") {
        using P = detail::PointType<3>;

        constexpr std::size_t N = 512;

        std::mt19937                          rng{0x5A5A5A5Au};
        std::uniform_real_distribution<float> coord{-10.0f, 10.0f};

        PointStore<3>         points{N};
        LeafBucket            leaf_buckets{N * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{
            nodes, leaf_buckets, leaf_bboxes, root_bbox, points, /*leaf_bucket_size=*/16, 0.7f, 0.25f};

        std::vector<P>             coords;
        std::vector<std::uint32_t> live_indices;
        coords.reserve(N);
        live_indices.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            P point;
            for (int d = 0; d < 3; ++d) {
                point[d] = coord(rng);
            }
            coords.push_back(point);
            live_indices.push_back(points.acquire(point));
        }
        const auto root = builder.rebuild(live_indices);

        SearchKernel<3> kernel{nodes, leaf_buckets, points};

        WHEN("hybrid search runs with k=10 and r=4 across 32 queries") {
            constexpr std::size_t K         = 10;
            constexpr float       R         = 4.0f;
            const float           sq_radius = R * R;
            THEN("results obey both bounds and match the brute-force oracle") {
                int k_limited = 0;
                int r_limited = 0;
                for (int trial = 0; trial < 32; ++trial) {
                    P query;
                    for (int d = 0; d < 3; ++d) {
                        query[d] = coord(rng);
                    }
                    auto expected = brute_force_radius<3>(coords, query, sq_radius);
                    if (expected.size() >= K) {
                        ++k_limited;
                    } else {
                        ++r_limited;
                    }
                    if (expected.size() > K) {
                        expected.resize(K);
                    }
                    const auto result = kernel.search(root, query, K, sq_radius);
                    REQUIRE(result.size() == expected.size());
                    REQUIRE(result.size() <= K);
                    for (std::size_t i = 0; i < result.size(); ++i) {
                        REQUIRE(result[i].sq_dist < sq_radius);
                        REQUIRE(result[i].sq_dist == expected[i]);
                    }
                }
                // Both regimes must occur; bump trial count or radius if a future change drops one.
                REQUIRE(k_limited > 0);
                REQUIRE(r_limited > 0);
            }
        }
    }
}

SCENARIO("SearchKernel radius=0 returns empty (strict-inside semantics)", "[search_kernel][radius][edge]") {
    GIVEN("a small tree built from 64 uniform random D=3 points") {
        using P = detail::PointType<3>;

        constexpr std::size_t N = 64;

        std::mt19937                          rng{0x0F0F0F0Fu};
        std::uniform_real_distribution<float> coord{-10.0f, 10.0f};

        PointStore<3>         points{N};
        LeafBucket            leaf_buckets{N * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{
            nodes, leaf_buckets, leaf_bboxes, root_bbox, points, /*leaf_bucket_size=*/16, 0.7f, 0.25f};

        std::vector<P>             coords;
        std::vector<std::uint32_t> live_indices;
        coords.reserve(N);
        live_indices.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            P point;
            for (int d = 0; d < 3; ++d) {
                point[d] = coord(rng);
            }
            coords.push_back(point);
            live_indices.push_back(points.acquire(point));
        }
        const auto root = builder.rebuild(live_indices);

        SearchKernel<3> kernel{nodes, leaf_buckets, points};

        WHEN("search runs with k_max=SIZE_MAX, initial_radius_sq=0 and q equal to a stored point") {
            const auto result =
                kernel.search(root, coords.front(), std::numeric_limits<std::size_t>::max(), 0.0f);
            THEN("the result is empty (sq < 0 is never satisfied, even for the coincident point)") {
                REQUIRE(result.empty());
            }
        }
    }
}

SCENARIO("SearchKernel with very large radius covers all live points", "[search_kernel][radius]") {
    GIVEN("a small tree built from 64 uniform random D=3 points") {
        using P = detail::PointType<3>;

        constexpr std::size_t N = 64;

        std::mt19937                          rng{0xA5A5A5A5u};
        std::uniform_real_distribution<float> coord{-10.0f, 10.0f};

        PointStore<3>         points{N};
        LeafBucket            leaf_buckets{N * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{
            nodes, leaf_buckets, leaf_bboxes, root_bbox, points, /*leaf_bucket_size=*/16, 0.7f, 0.25f};

        std::vector<P>             coords;
        std::vector<std::uint32_t> live_indices;
        coords.reserve(N);
        live_indices.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            P point;
            for (int d = 0; d < 3; ++d) {
                point[d] = coord(rng);
            }
            coords.push_back(point);
            live_indices.push_back(points.acquire(point));
        }
        const auto root = builder.rebuild(live_indices);

        SearchKernel<3> kernel{nodes, leaf_buckets, points};

        WHEN("search runs with k_max=SIZE_MAX and a huge radius") {
            const P     query{coord(rng), coord(rng), coord(rng)};
            const float huge_sq_radius = 1.0e10f;
            const auto  result_radius =
                kernel.search(root, query, std::numeric_limits<std::size_t>::max(), huge_sq_radius);
            const auto result_knn = kernel.search(root, query, N, std::numeric_limits<float>::infinity());
            THEN("all N live points are returned in ascending order, matching unbounded knn(N)") {
                REQUIRE(result_radius.size() == N);
                REQUIRE(result_knn.size() == N);
                for (std::size_t i = 1; i < result_radius.size(); ++i) {
                    REQUIRE(result_radius[i - 1].sq_dist <= result_radius[i].sq_dist);
                }
                for (std::size_t i = 0; i < N; ++i) {
                    REQUIRE(result_radius[i].sq_dist == result_knn[i].sq_dist);
                }
            }
        }
    }
}

SCENARIO("SearchKernel hybrid with k > N falls into the radius-bound regime", "[search_kernel][hybrid]") {
    GIVEN("a D=3 tree built from 64 uniform random points") {
        using P = detail::PointType<3>;

        constexpr std::size_t N = 64;

        std::mt19937                          rng{0x12345678u};
        std::uniform_real_distribution<float> coord{-10.0f, 10.0f};

        PointStore<3>         points{N};
        LeafBucket            leaf_buckets{N * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{
            nodes, leaf_buckets, leaf_bboxes, root_bbox, points, /*leaf_bucket_size=*/16, 0.7f, 0.25f};

        std::vector<P>             coords;
        std::vector<std::uint32_t> live_indices;
        coords.reserve(N);
        live_indices.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            P point;
            for (int d = 0; d < 3; ++d) {
                point[d] = coord(rng);
            }
            coords.push_back(point);
            live_indices.push_back(points.acquire(point));
        }
        const auto root = builder.rebuild(live_indices);

        SearchKernel<3> kernel{nodes, leaf_buckets, points};

        WHEN("hybrid search runs with k=1000 and a moderate radius") {
            const P     query{0.0f, 0.0f, 0.0f};
            const float radius    = 5.0f;
            const float sq_radius = radius * radius;
            const auto  result    = kernel.search(root, query, /*k=*/1000, sq_radius);
            const auto  expected  = brute_force_radius<3>(coords, query, sq_radius);
            THEN("the result equals brute-force radius (k cap never bites because k > N)") {
                REQUIRE(result.size() == expected.size());
                for (std::size_t i = 0; i < result.size(); ++i) {
                    REQUIRE(result[i].sq_dist == expected[i]);
                    REQUIRE(result[i].sq_dist < sq_radius);
                }
            }
        }
    }
}

SCENARIO("SearchKernel::collect_indices_within returns every live in-radius index",
         "[search_kernel][collect]") {
    GIVEN("a small D=3 tree built from 64 uniform random points") {
        using P = detail::PointType<3>;

        constexpr std::size_t N = 64;

        std::mt19937                          rng{0x1337C0DEu};
        std::uniform_real_distribution<float> coord{-10.0f, 10.0f};

        PointStore<3>         points{N};
        LeafBucket            leaf_buckets{N * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{
            nodes, leaf_buckets, leaf_bboxes, root_bbox, points, /*leaf_bucket_size=*/8, 0.7f, 0.25f};

        std::vector<P>             coords;
        std::vector<std::uint32_t> live_indices;
        coords.reserve(N);
        live_indices.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            P point;
            for (int d = 0; d < 3; ++d) {
                point[d] = coord(rng);
            }
            coords.push_back(point);
            live_indices.push_back(points.acquire(point));
        }
        const auto root = builder.rebuild(live_indices);

        SearchKernel<3> kernel{nodes, leaf_buckets, points};

        WHEN("collect_indices_within runs across several queries and radii") {
            THEN("the returned index set matches brute force") {
                for (float radius : {1.0f, 3.0f, 8.0f}) {
                    const float sq_radius = radius * radius;
                    for (int trial = 0; trial < 8; ++trial) {
                        P query;
                        for (int d = 0; d < 3; ++d) {
                            query[d] = coord(rng);
                        }
                        std::vector<std::uint32_t> expected;
                        for (std::uint32_t i = 0; i < N; ++i) {
                            if ((coords[i] - query).squaredNorm() < sq_radius) {
                                expected.push_back(i);
                            }
                        }
                        auto result = kernel.collect_indices_within(root, query, sq_radius);
                        std::sort(result.begin(), result.end());
                        std::sort(expected.begin(), expected.end());
                        REQUIRE(result == expected);
                    }
                }
            }
        }

        WHEN("collect_indices_within runs after a few indices are released") {
            const std::vector<std::uint32_t> released{live_indices[1], live_indices[9], live_indices[33]};
            for (auto idx : released) {
                points.release(idx);
            }
            THEN("released indices are excluded from the result") {
                const auto result = kernel.collect_indices_within(root, P{0.0f, 0.0f, 0.0f}, 1.0e10f);
                REQUIRE(result.size() == N - released.size());
                for (auto idx : released) {
                    REQUIRE(std::find(result.begin(), result.end(), idx) == result.end());
                }
            }
        }
    }
}

SCENARIO("SearchKernel::collect_indices_within on an INVALID root returns empty",
         "[search_kernel][collect][empty]") {
    GIVEN("an empty node pool and an INVALID root") {
        PointStore<3>         points{4};
        LeafBucket            leaf_buckets{8};
        std::vector<TreeNode> nodes;
        SearchKernel<3>       kernel{nodes, leaf_buckets, points};

        WHEN("collect_indices_within is invoked") {
            const auto result =
                kernel.collect_indices_within(TreeNode::INVALID, detail::PointType<3>{0, 0, 0}, 1.0f);
            THEN("the result is empty") {
                REQUIRE(result.empty());
            }
        }
    }
}

SCENARIO("SearchKernel skips released indices that leaf_buckets still reference",
         "[search_kernel][liveness]") {
    GIVEN("a D=3 tree built from 32 points, then a few indices released without rebuild") {
        using P = detail::PointType<3>;

        constexpr std::size_t N = 32;

        std::mt19937                          rng{0xBEEFCAFEu};
        std::uniform_real_distribution<float> coord{-10.0f, 10.0f};

        PointStore<3>         points{N};
        LeafBucket            leaf_buckets{N * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{
            nodes, leaf_buckets, leaf_bboxes, root_bbox, points, /*leaf_bucket_size=*/8, 0.7f, 0.25f};

        std::vector<P>             coords;
        std::vector<std::uint32_t> live_indices;
        coords.reserve(N);
        live_indices.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            P point;
            for (int d = 0; d < 3; ++d) {
                point[d] = coord(rng);
            }
            coords.push_back(point);
            live_indices.push_back(points.acquire(point));
        }
        const auto root = builder.rebuild(live_indices);

        const std::vector<std::uint32_t> released{live_indices[0], live_indices[7], live_indices[15]};
        for (auto idx : released) {
            points.release(idx);
        }

        SearchKernel<3> kernel{nodes, leaf_buckets, points};

        WHEN("a wide radius search runs against the partially-released tree") {
            const P    query{0.0f, 0.0f, 0.0f};
            const auto result = kernel.search(root, query, std::numeric_limits<std::size_t>::max(), 1.0e10f);
            THEN("the result excludes every released index's coordinate") {
                REQUIRE(result.size() == N - released.size());
                for (auto idx : released) {
                    const P& gone = coords[idx];
                    for (const auto& neighbor : result) {
                        REQUIRE((neighbor.coord - gone).squaredNorm() != 0.0f);
                    }
                }
            }
        }
    }
}

SCENARIO("SearchKernel::any_within on an INVALID root returns false", "[search_kernel][any_within][empty]") {
    GIVEN("an empty node pool and an INVALID root") {
        PointStore<3>         points{4};
        LeafBucket            leaf_buckets{8};
        std::vector<TreeNode> nodes;
        SearchKernel<3>       kernel{nodes, leaf_buckets, points};

        WHEN("any_within is invoked") {
            const bool result = kernel.any_within(TreeNode::INVALID, detail::PointType<3>{0, 0, 0}, 1.0f);
            THEN("the predicate is false") {
                REQUIRE_FALSE(result);
            }
        }
    }
}

SCENARIO("SearchKernel::any_within on a single-point tree", "[search_kernel][any_within]") {
    GIVEN("a tree containing a single point at the origin") {
        using P = detail::PointType<3>;

        PointStore<3>         points{4};
        LeafBucket            leaf_buckets{8};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{
            nodes, leaf_buckets, leaf_bboxes, root_bbox, points, /*leaf_bucket_size=*/4, 0.7f, 0.25f};

        std::vector<std::uint32_t> live_indices{points.acquire(P{0.0f, 0.0f, 0.0f})};
        const auto                 root = builder.rebuild(live_indices);
        SearchKernel<3>            kernel{nodes, leaf_buckets, points};

        WHEN("the query lies inside sq_radius") {
            const bool result = kernel.any_within(root, P{0.5f, 0.0f, 0.0f}, /*sq_radius=*/1.0f);
            THEN("the predicate is true") {
                REQUIRE(result);
            }
        }

        WHEN("the query lies outside sq_radius") {
            const bool result = kernel.any_within(root, P{5.0f, 0.0f, 0.0f}, /*sq_radius=*/1.0f);
            THEN("the predicate is false") {
                REQUIRE_FALSE(result);
            }
        }

        WHEN("the query lies exactly at sq_radius (strict-inside semantics)") {
            const bool result = kernel.any_within(root, P{1.0f, 0.0f, 0.0f}, /*sq_radius=*/1.0f);
            THEN("the predicate is false because sq_dist == sq_radius is not strictly inside") {
                REQUIRE_FALSE(result);
            }
        }
    }
}

SCENARIO("SearchKernel::any_within across 10 random points", "[search_kernel][any_within]") {
    GIVEN("a tree built from 10 uniform random D=3 points") {
        using P = detail::PointType<3>;

        constexpr std::size_t N = 10;

        std::mt19937                          rng{0xA11CE7u};
        std::uniform_real_distribution<float> coord{-10.0f, 10.0f};

        PointStore<3>         points{N};
        LeafBucket            leaf_buckets{N * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{
            nodes, leaf_buckets, leaf_bboxes, root_bbox, points, /*leaf_bucket_size=*/4, 0.7f, 0.25f};

        std::vector<P>             coords;
        std::vector<std::uint32_t> live_indices;
        coords.reserve(N);
        live_indices.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            P point;
            for (int d = 0; d < 3; ++d) {
                point[d] = coord(rng);
            }
            coords.push_back(point);
            live_indices.push_back(points.acquire(point));
        }
        const auto root = builder.rebuild(live_indices);

        SearchKernel<3> kernel{nodes, leaf_buckets, points};

        WHEN("the query coincides with a stored point") {
            const bool result = kernel.any_within(root, coords[3], /*sq_radius=*/1.0e-3f);
            THEN("the predicate is true") {
                REQUIRE(result);
            }
        }

        WHEN("the query is far from every stored point") {
            const bool result = kernel.any_within(root, P{1.0e6f, 1.0e6f, 1.0e6f}, /*sq_radius=*/1.0f);
            THEN("the predicate is false") {
                REQUIRE_FALSE(result);
            }
        }
    }
}

SCENARIO("SearchKernel::any_within skips released indices", "[search_kernel][any_within][liveness]") {
    GIVEN("a D=3 tree with the only in-radius candidate released after build") {
        using P = detail::PointType<3>;

        PointStore<3>         points{8};
        LeafBucket            leaf_buckets{16};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{
            nodes, leaf_buckets, leaf_bboxes, root_bbox, points, /*leaf_bucket_size=*/4, 0.7f, 0.25f};

        const std::vector<P> coords{
            P{0.1f, 0.0f, 0.0f}, P{5.0f, 0.0f, 0.0f}, P{0.0f, 5.0f, 0.0f}, P{0.0f, 0.0f, 5.0f}};
        std::vector<std::uint32_t> live_indices;
        live_indices.reserve(coords.size());
        for (const auto& point : coords) {
            live_indices.push_back(points.acquire(point));
        }
        const auto root = builder.rebuild(live_indices);

        SearchKernel<3> kernel{nodes, leaf_buckets, points};
        const P         query{0.0f, 0.0f, 0.0f};
        const float     sq_radius = 1.0f;

        WHEN("the lone in-radius index is still live") {
            THEN("the predicate is true") {
                REQUIRE(kernel.any_within(root, query, sq_radius));
            }
        }

        WHEN("the lone in-radius index is released") {
            points.release(live_indices[0]);
            THEN("the predicate is false") {
                REQUIRE_FALSE(kernel.any_within(root, query, sq_radius));
            }
        }
    }
}

SCENARIO("SearchKernel::any_within agrees with collect_indices_within on 100 random queries",
         "[search_kernel][any_within][oracle]") {
    GIVEN("a D=3 tree built from 256 uniform random points") {
        using P = detail::PointType<3>;

        constexpr std::size_t N = 256;

        std::mt19937                          rng{0xDEADBEEFu};
        std::uniform_real_distribution<float> coord{-10.0f, 10.0f};

        PointStore<3>         points{N};
        LeafBucket            leaf_buckets{N * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{
            nodes, leaf_buckets, leaf_bboxes, root_bbox, points, /*leaf_bucket_size=*/16, 0.7f, 0.25f};

        std::vector<P>             coords;
        std::vector<std::uint32_t> live_indices;
        coords.reserve(N);
        live_indices.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            P point;
            for (int d = 0; d < 3; ++d) {
                point[d] = coord(rng);
            }
            coords.push_back(point);
            live_indices.push_back(points.acquire(point));
        }
        const auto root = builder.rebuild(live_indices);

        SearchKernel<3> kernel{nodes, leaf_buckets, points};

        WHEN("any_within is cross-checked against collect_indices_within for 100 random queries") {
            THEN("any_within == !collect_indices_within.empty() for every (query, radius)") {
                for (float radius : {0.5f, 2.0f, 6.0f}) {
                    const float sq_radius = radius * radius;
                    for (int trial = 0; trial < 100; ++trial) {
                        P query;
                        for (int d = 0; d < 3; ++d) {
                            query[d] = coord(rng);
                        }
                        const auto matches  = kernel.collect_indices_within(root, query, sq_radius);
                        const bool expected = !matches.empty();
                        const bool result   = kernel.any_within(root, query, sq_radius);
                        REQUIRE(result == expected);
                    }
                }
            }
        }
    }
}

} // namespace copse::internal
