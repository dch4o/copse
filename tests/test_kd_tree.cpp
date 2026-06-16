// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
// BDD tests for the public KDTree<Dim> facade.
// Black-box behavior only — internal helpers have their own test files.

#include "copse/kd_tree.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace copse {

SCENARIO("KDTree3 construction validates Config", "[kd_tree][ctor]") {
    GIVEN("a valid Config") {
        KDTree<3>::Config cfg{.capacity = 16, .resolution = 0.01f};
        WHEN("the tree is constructed") {
            KDTree<3> tree{cfg};
            THEN("size is zero and capacity is the configured value") {
                REQUIRE(tree.size() == 0);
                REQUIRE(tree.capacity() == 16);
            }
        }
    }

    GIVEN("a Config with capacity == 0") {
        KDTree<3>::Config cfg{.capacity = 0, .resolution = 0.01f};
        THEN("the constructor throws std::invalid_argument") {
            REQUIRE_THROWS_AS(KDTree<3>{cfg}, std::invalid_argument);
        }
    }

    GIVEN("a Config with non-positive resolution") {
        KDTree<3>::Config cfg_zero{.capacity = 4, .resolution = 0.0f};
        KDTree<3>::Config cfg_neg{.capacity = 4, .resolution = -1.0f};
        THEN("the constructor throws std::invalid_argument") {
            REQUIRE_THROWS_AS(KDTree<3>{cfg_zero}, std::invalid_argument);
            REQUIRE_THROWS_AS(KDTree<3>{cfg_neg}, std::invalid_argument);
        }
    }

    GIVEN("a Config with leaf_bucket_size == 0") {
        KDTree<3>::Config cfg{.capacity = 4, .resolution = 0.01f, .leaf_bucket_size = 0};
        THEN("the constructor throws std::invalid_argument") {
            REQUIRE_THROWS_AS(KDTree<3>{cfg}, std::invalid_argument);
        }
    }

    GIVEN("a Config with alpha outside (0.5, 1.0)") {
        KDTree<3>::Config cfg_low{.capacity = 4, .resolution = 0.01f, .alpha = 0.5f};
        KDTree<3>::Config cfg_high{.capacity = 4, .resolution = 0.01f, .alpha = 1.0f};
        THEN("the constructor throws std::invalid_argument") {
            REQUIRE_THROWS_AS(KDTree<3>{cfg_low}, std::invalid_argument);
            REQUIRE_THROWS_AS(KDTree<3>{cfg_high}, std::invalid_argument);
        }
    }

    GIVEN("a Config with tombstone_threshold outside [0, 1]") {
        KDTree<3>::Config cfg_neg{.capacity = 4, .resolution = 0.01f, .tombstone_threshold = -0.1f};
        KDTree<3>::Config cfg_big{.capacity = 4, .resolution = 0.01f, .tombstone_threshold = 1.1f};
        THEN("the constructor throws std::invalid_argument") {
            REQUIRE_THROWS_AS(KDTree<3>{cfg_neg}, std::invalid_argument);
            REQUIRE_THROWS_AS(KDTree<3>{cfg_big}, std::invalid_argument);
        }
    }
}

SCENARIO("KDTree3::insert returns the count of points written", "[kd_tree][insert]") {
    GIVEN("a tree of capacity 8 and a single-point batch") {
        KDTree<3> tree{{.capacity = 8, .resolution = 0.01f}};
        using P = KDTree<3>::Point;
        const std::vector<P> batch{P{1.0f, 2.0f, 3.0f}};
        WHEN("insert is called") {
            const auto inserted = tree.insert(batch);
            THEN("the return value is the number of points written and size reflects it") {
                REQUIRE(inserted == 1);
                REQUIRE(tree.size() == 1);
            }
        }
    }
}

SCENARIO("KDTree3::insert collapses intra-batch duplicates within resolution", "[kd_tree][insert][dedup]") {
    GIVEN("an empty tree with resolution=0.5") {
        KDTree<3> tree{{.capacity = 8, .resolution = 0.5f}};
        using P = KDTree<3>::Point;
        const std::vector<P> batch{P{0.0f, 0.0f, 0.0f}, P{0.1f, 0.0f, 0.0f}};
        WHEN("the two near-duplicate points are inserted in one batch") {
            const auto inserted = tree.insert(batch);
            THEN("only the first-seen point is kept") {
                REQUIRE(inserted == 1);
                REQUIRE(tree.size() == 1);
            }
        }
    }
}

SCENARIO("KDTree3::insert dedup is first-seen-wins across input reorderings", "[kd_tree][insert][dedup]") {
    GIVEN("two trees with identical Config(resolution=0.5)") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;
        Tree::Config cfg{.capacity = 8, .resolution = 0.5f};
        Tree         tree_a{cfg};
        Tree         tree_b{cfg};
        WHEN("tree A receives [{0,0,0},{0.1,0,0}] and tree B receives [{0.1,0,0},{0,0,0}]") {
            const std::vector<P> batch_a{P{0.0f, 0.0f, 0.0f}, P{0.1f, 0.0f, 0.0f}};
            const std::vector<P> batch_b{P{0.1f, 0.0f, 0.0f}, P{0.0f, 0.0f, 0.0f}};
            REQUIRE(tree_a.insert(batch_a) == 1);
            REQUIRE(tree_b.insert(batch_b) == 1);
            THEN("each tree's surviving point is the one seen first in its own batch") {
                const auto result_a = tree_a.knn_search(P{0.0f, 0.0f, 0.0f}, 1);
                const auto result_b = tree_b.knn_search(P{0.0f, 0.0f, 0.0f}, 1);
                REQUIRE(result_a.size() == 1);
                REQUIRE(result_b.size() == 1);
                REQUIRE(result_a[0].coord == P{0.0f, 0.0f, 0.0f});
                REQUIRE(result_b[0].coord == P{0.1f, 0.0f, 0.0f});
            }
        }
    }
}

SCENARIO("KDTree3::insert rejects points within resolution of the live tree", "[kd_tree][insert][dedup]") {
    GIVEN("a tree with {0,0,0} already inserted at resolution=0.5") {
        KDTree<3> tree{{.capacity = 8, .resolution = 0.5f}};
        using P = KDTree<3>::Point;
        REQUIRE(tree.insert(std::vector<P>{P{0.0f, 0.0f, 0.0f}}) == 1);
        WHEN("a near-duplicate {0.1,0,0} is inserted in a later batch") {
            const auto inserted = tree.insert(std::vector<P>{P{0.1f, 0.0f, 0.0f}});
            THEN("the insert is rejected by the tree-side check") {
                REQUIRE(inserted == 0);
                REQUIRE(tree.size() == 1);
            }
        }
    }
}

SCENARIO("KDTree3::insert return count reflects accepted-after-dedup", "[kd_tree][insert][dedup]") {
    GIVEN("an empty tree at resolution=0.5 and a 6-point batch with 3 intra-cluster duplicates") {
        KDTree<3> tree{{.capacity = 16, .resolution = 0.5f}};
        using P = KDTree<3>::Point;
        const std::vector<P> batch{
            P{0.0f, 0.0f, 0.0f},
            P{0.1f, 0.0f, 0.0f}, // dup of point 0
            P{10.0f, 0.0f, 0.0f},
            P{10.0f, 0.1f, 0.0f}, // dup of point 2
            P{20.0f, 0.0f, 0.0f},
            P{20.0f, 0.0f, 0.1f}, // dup of point 4
        };
        WHEN("the batch is inserted") {
            const auto inserted = tree.insert(batch);
            THEN("exactly 3 points are accepted") {
                REQUIRE(inserted == 3);
                REQUIRE(tree.size() == 3);
            }
        }
    }
}

SCENARIO("KDTree3::insert keeps points at exactly resolution distance", "[kd_tree][insert][dedup]") {
    GIVEN("an empty tree with resolution=0.5") {
        KDTree<3> tree{{.capacity = 8, .resolution = 0.5f}};
        using P = KDTree<3>::Point;
        const std::vector<P> batch{P{0.0f, 0.0f, 0.0f}, P{0.5f, 0.0f, 0.0f}};
        WHEN("the boundary pair is inserted") {
            const auto inserted = tree.insert(batch);
            THEN("strict-less-than semantics keep both") {
                REQUIRE(inserted == 2);
                REQUIRE(tree.size() == 2);
            }
        }
    }
}

SCENARIO("KDTree3::knn_search agrees with a brute-force oracle on 1000 points",
         "[kd_tree][search][knn][smoke]") {
    GIVEN("a tree of capacity 1000 populated with uniform random D=3 points") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N = 1000;
        constexpr std::size_t K = 5;

        std::mt19937                          rng{0xDEADBEEFu};
        std::uniform_real_distribution<float> coord{-100.0f, 100.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(P{coord(rng), coord(rng), coord(rng)});
        }

        Tree tree{{.capacity = N, .resolution = 1e-9f}};
        REQUIRE(tree.insert(coords) == N);
        REQUIRE(tree.size() == N);

        WHEN("knn_search is called for 20 random queries with k=5") {
            THEN("results are sorted ascending and match the brute-force oracle exactly") {
                for (int trial = 0; trial < 20; ++trial) {
                    const P query{coord(rng), coord(rng), coord(rng)};

                    std::vector<std::pair<std::uint32_t, float>> expected;
                    expected.reserve(N);
                    for (std::uint32_t i = 0; i < N; ++i) {
                        expected.emplace_back(i, coords[i].sq_dist(query));
                    }
                    std::sort(expected.begin(), expected.end(), [](const auto& lhs, const auto& rhs) {
                        return lhs.second < rhs.second;
                    });
                    expected.resize(K);

                    const auto result = tree.knn_search(query, K);
                    REQUIRE(result.size() == K);
                    for (std::size_t i = 1; i < result.size(); ++i) {
                        REQUIRE(result[i - 1].sq_dist <= result[i].sq_dist);
                    }
                    for (std::size_t i = 0; i < K; ++i) {
                        REQUIRE(result[i].sq_dist == expected[i].second);
                    }
                }
            }
        }
    }
}

SCENARIO("KDTree3::knn_search returns empty when k == 0", "[kd_tree][search][knn]") {
    GIVEN("a tree containing one point") {
        KDTree<3> tree{{.capacity = 4, .resolution = 0.01f}};
        using P = KDTree<3>::Point;
        const std::vector<P> one{P{0, 0, 0}};
        (void)tree.insert(one);

        WHEN("knn_search is called with k == 0") {
            THEN("the result is empty") {
                REQUIRE(tree.knn_search(P{0, 0, 0}, 0).empty());
            }
        }
    }
}

SCENARIO("KDTree3 silently evicts oldest points when capacity is exceeded", "[kd_tree][insert][fifo]") {
    GIVEN("a tree of capacity 10 and 15 distinct random D=3 points") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t Cap = 10;
        constexpr std::size_t N   = 15;

        std::mt19937                          rng{0xCAFEBABEu};
        std::uniform_real_distribution<float> coord{-100.0f, 100.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(P{coord(rng), coord(rng), coord(rng)});
        }

        Tree tree{{.capacity = Cap, .resolution = 1e-9f}};
        REQUIRE(tree.insert(coords) == N);

        WHEN("knn_search returns the 5 nearest neighbors of a query") {
            THEN("size is bounded at capacity and no evicted point appears in the result") {
                REQUIRE(tree.size() == Cap);

                const P    query{coord(rng), coord(rng), coord(rng)};
                const auto result = tree.knn_search(query, 5);
                REQUIRE(result.size() == 5);

                for (const auto& neighbor : result) {
                    for (std::size_t i = 0; i < N - Cap; ++i) {
                        REQUIRE(neighbor.coord.sq_dist(coords[i]) != 0.0f);
                    }
                }
            }
        }
    }
}

SCENARIO("KDTree3::knn_search on an empty tree returns empty", "[kd_tree][search][knn]") {
    GIVEN("a freshly constructed tree with no inserts") {
        KDTree<3> tree{{.capacity = 4, .resolution = 0.01f}};
        using P = KDTree<3>::Point;
        WHEN("knn_search is called") {
            const auto result = tree.knn_search(P{0, 0, 0}, 3);
            THEN("the result is empty") {
                REQUIRE(result.empty());
            }
        }
    }
}

SCENARIO("KDTree3::radius_search returns all points within radius", "[kd_tree][search][radius]") {
    GIVEN("a tree of capacity 1000 populated with uniform random D=3 points") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N = 1000;

        std::mt19937                          rng{0xABCDEF01u};
        std::uniform_real_distribution<float> coord{-100.0f, 100.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(P{coord(rng), coord(rng), coord(rng)});
        }

        Tree tree{{.capacity = N, .resolution = 1e-9f}};
        REQUIRE(tree.insert(coords) == N);

        WHEN("radius_search is called across several queries and radii") {
            THEN("results match the brute-force oracle and are ascending") {
                for (float radius : {5.0f, 25.0f, 75.0f}) {
                    const float sq_radius = radius * radius;
                    for (int trial = 0; trial < 5; ++trial) {
                        const P query{coord(rng), coord(rng), coord(rng)};

                        std::vector<float> expected;
                        expected.reserve(N);
                        for (std::size_t i = 0; i < N; ++i) {
                            const float sq_dist = coords[i].sq_dist(query);
                            if (sq_dist < sq_radius) {
                                expected.push_back(sq_dist);
                            }
                        }
                        std::sort(expected.begin(), expected.end());

                        const auto result = tree.radius_search(query, radius);
                        REQUIRE(result.size() == expected.size());
                        for (std::size_t i = 0; i < result.size(); ++i) {
                            REQUIRE(result[i].sq_dist == expected[i]);
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

SCENARIO("KDTree3::radius_search returns empty when no points within radius", "[kd_tree][search][radius]") {
    GIVEN("a small tree with a few clustered points") {
        KDTree<3> tree{{.capacity = 8, .resolution = 1e-9f}};
        using P = KDTree<3>::Point;
        const std::vector<P> batch{P{0, 0, 0}, P{1, 0, 0}, P{0, 1, 0}};
        (void)tree.insert(batch);

        WHEN("radius_search is called from a far point with a tiny radius") {
            const auto result = tree.radius_search(P{1000, 1000, 1000}, 0.01f);
            THEN("the result is empty") {
                REQUIRE(result.empty());
            }
        }
    }
}

SCENARIO("KDTree3::hybrid_search returns at most k points within radius", "[kd_tree][search][hybrid]") {
    GIVEN("a tree of capacity 1000 populated with uniform random D=3 points") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N = 1000;

        std::mt19937                          rng{0xFEEDFACEu};
        std::uniform_real_distribution<float> coord{-100.0f, 100.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(P{coord(rng), coord(rng), coord(rng)});
        }

        Tree tree{{.capacity = N, .resolution = 1e-9f}};
        REQUIRE(tree.insert(coords) == N);

        WHEN("hybrid_search runs across several (k, r) combinations") {
            THEN("each result obeys both bounds and matches the brute-force oracle") {
                struct SearchParams {
                    std::size_t k;
                    float       r;
                };
                const SearchParams params[] = {{5, 10.0f}, {20, 30.0f}, {50, 80.0f}, {100, 5.0f}};

                for (const auto& param : params) {
                    const float sq_radius = param.r * param.r;
                    for (int trial = 0; trial < 5; ++trial) {
                        const P query{coord(rng), coord(rng), coord(rng)};

                        std::vector<float> expected;
                        expected.reserve(N);
                        for (std::size_t i = 0; i < N; ++i) {
                            const float sq_dist = coords[i].sq_dist(query);
                            if (sq_dist < sq_radius) {
                                expected.push_back(sq_dist);
                            }
                        }
                        std::sort(expected.begin(), expected.end());
                        if (expected.size() > param.k) {
                            expected.resize(param.k);
                        }

                        const auto result = tree.hybrid_search(query, param.k, param.r);
                        REQUIRE(result.size() <= param.k);
                        REQUIRE(result.size() == expected.size());
                        for (std::size_t i = 0; i < result.size(); ++i) {
                            REQUIRE(result[i].sq_dist < sq_radius);
                            REQUIRE(result[i].sq_dist == expected[i]);
                        }
                    }
                }
            }
        }
    }
}

SCENARIO("KDTree3::hybrid_search matches knn when k bites first", "[kd_tree][search][hybrid]") {
    GIVEN("a populated tree, a huge radius, and a small k") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N = 256;
        constexpr std::size_t K = 7;

        std::mt19937                          rng{0x11112222u};
        std::uniform_real_distribution<float> coord{-50.0f, 50.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(P{coord(rng), coord(rng), coord(rng)});
        }

        Tree tree{{.capacity = N, .resolution = 1e-9f}};
        (void)tree.insert(coords);

        WHEN("hybrid_search runs with k=K and a huge radius") {
            const P    query{coord(rng), coord(rng), coord(rng)};
            const auto result_hybrid = tree.hybrid_search(query, K, 1.0e9f);
            const auto result_knn    = tree.knn_search(query, K);
            THEN("the result equals knn for the same query and k") {
                REQUIRE(result_hybrid.size() == K);
                REQUIRE(result_hybrid.size() == result_knn.size());
                for (std::size_t i = 0; i < K; ++i) {
                    REQUIRE(result_hybrid[i].sq_dist == result_knn[i].sq_dist);
                }
            }
        }
    }
}

SCENARIO("KDTree3::hybrid_search matches radius_search when radius bites first",
         "[kd_tree][search][hybrid]") {
    GIVEN("a populated tree, a huge k, and a small radius") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N = 256;

        std::mt19937                          rng{0x33334444u};
        std::uniform_real_distribution<float> coord{-50.0f, 50.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(P{coord(rng), coord(rng), coord(rng)});
        }

        Tree tree{{.capacity = N, .resolution = 1e-9f}};
        (void)tree.insert(coords);

        WHEN("hybrid_search runs with a huge k and radius=10") {
            const P     query{coord(rng), coord(rng), coord(rng)};
            const float radius        = 10.0f;
            const auto  result_hybrid = tree.hybrid_search(query, N * 10, radius);
            const auto  result_radius = tree.radius_search(query, radius);
            THEN("the result equals radius_search for the same query and radius") {
                REQUIRE(result_hybrid.size() == result_radius.size());
                for (std::size_t i = 0; i < result_hybrid.size(); ++i) {
                    REQUIRE(result_hybrid[i].sq_dist == result_radius[i].sq_dist);
                }
            }
        }
    }
}

SCENARIO("KDTree3::hybrid_search returns empty when k == 0", "[kd_tree][search][hybrid]") {
    GIVEN("a tree containing one point") {
        KDTree<3> tree{{.capacity = 4, .resolution = 0.01f}};
        using P = KDTree<3>::Point;
        const std::vector<P> one{P{0, 0, 0}};
        (void)tree.insert(one);

        WHEN("hybrid_search is called with k == 0") {
            THEN("the result is empty") {
                REQUIRE(tree.hybrid_search(P{0, 0, 0}, 0, 1.0f).empty());
            }
        }
    }
}

SCENARIO("KDTree3::remove drops matching points from subsequent queries", "[kd_tree][remove]") {
    GIVEN("a tree of capacity 100 with 100 well-separated random points") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N          = 100;
        constexpr float       Resolution = 0.5f;

        std::mt19937                          rng{0x5EED5EEDu};
        std::uniform_real_distribution<float> coord{-1000.0f, 1000.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(P{coord(rng), coord(rng), coord(rng)});
        }

        Tree tree{{.capacity = N, .resolution = Resolution}};
        REQUIRE(tree.insert(coords) == N);

        WHEN("remove is called with 5 of the inserted coordinates") {
            const std::vector<P> targets{coords[3], coords[17], coords[42], coords[71], coords[88]};
            const auto           cleared = tree.remove(targets);
            THEN("the return count equals 5, size drops by 5, and the targets are no longer found") {
                REQUIRE(cleared == 5);
                REQUIRE(tree.size() == N - 5);
                for (const auto& target : targets) {
                    REQUIRE(tree.radius_search(target, Resolution).empty());
                }
            }
        }
    }
}

SCENARIO("KDTree3::remove returns 0 when no points match", "[kd_tree][remove]") {
    GIVEN("a tree containing three clustered points") {
        KDTree<3> tree{{.capacity = 8, .resolution = 0.01f}};
        using P = KDTree<3>::Point;
        const std::vector<P> batch{P{0, 0, 0}, P{1, 0, 0}, P{0, 1, 0}};
        REQUIRE(tree.insert(batch) == 3);

        WHEN("remove is called with queries far from every live point") {
            const std::vector<P> queries{P{1000, 1000, 1000}, P{-500, -500, -500}};
            const auto           cleared = tree.remove(queries);
            THEN("the return count is zero and size is unchanged") {
                REQUIRE(cleared == 0);
                REQUIRE(tree.size() == 3);
            }
        }
    }
}

SCENARIO("KDTree3::remove on an empty tree returns 0", "[kd_tree][remove]") {
    GIVEN("a freshly constructed tree with no inserts") {
        KDTree<3> tree{{.capacity = 4, .resolution = 0.01f}};
        using P = KDTree<3>::Point;
        WHEN("remove is called with a single query") {
            const std::vector<P> queries{P{0, 0, 0}};
            const auto           cleared = tree.remove(queries);
            THEN("the return count is zero and size remains zero") {
                REQUIRE(cleared == 0);
                REQUIRE(tree.size() == 0);
            }
        }
    }
}

SCENARIO("KDTree3::remove count matches the size delta", "[kd_tree][remove]") {
    GIVEN("a tree of capacity 200 with 200 well-separated random points") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N          = 200;
        constexpr float       Resolution = 0.5f;

        std::mt19937                          rng{0xDEC0DE01u};
        std::uniform_real_distribution<float> coord{-1000.0f, 1000.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(P{coord(rng), coord(rng), coord(rng)});
        }

        Tree tree{{.capacity = N, .resolution = Resolution}};
        REQUIRE(tree.insert(coords) == N);

        WHEN("a known subset of the inserts is removed") {
            const std::vector<P> targets{coords[0], coords[5], coords[50], coords[150], coords[199]};
            const auto           before  = tree.size();
            const auto           cleared = tree.remove(targets);
            const auto           after   = tree.size();
            THEN("before - after equals the returned count") {
                REQUIRE(cleared == targets.size());
                REQUIRE(before - after == cleared);
            }
        }
    }
}

SCENARIO("KDTree3::rebuild_all after mutations agrees with the oracle on the live set",
         "[kd_tree][rebuild]") {
    GIVEN("a tree of capacity 1000 with 1000 random points, then a quarter removed") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N          = 1000;
        constexpr std::size_t Removed    = N / 4;
        constexpr float       Resolution = 1e-9f;

        std::mt19937                          rng{0xA11BA11Au};
        std::uniform_real_distribution<float> coord{-100.0f, 100.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(P{coord(rng), coord(rng), coord(rng)});
        }

        Tree tree{{.capacity = N, .resolution = Resolution}};
        REQUIRE(tree.insert(coords) == N);

        std::vector<std::size_t> removed_indices;
        removed_indices.reserve(Removed);
        for (std::size_t i = 0; i < Removed; ++i) {
            removed_indices.push_back(i * 4); // 0, 4, 8, ...
        }
        std::vector<P> to_remove;
        to_remove.reserve(Removed);
        for (auto index : removed_indices) {
            to_remove.push_back(coords[index]);
        }
        REQUIRE(tree.remove(to_remove) == Removed);

        WHEN("rebuild_all is invoked and knn_search runs against the live set") {
            tree.rebuild_all();

            std::vector<P> live;
            live.reserve(N - Removed);
            for (std::size_t i = 0; i < N; ++i) {
                if (std::find(removed_indices.begin(), removed_indices.end(), i) == removed_indices.end()) {
                    live.push_back(coords[i]);
                }
            }

            THEN("size matches and knn_search agrees with a brute-force oracle") {
                REQUIRE(tree.size() == live.size());
                constexpr std::size_t K = 7;
                for (int trial = 0; trial < 16; ++trial) {
                    const P            query{coord(rng), coord(rng), coord(rng)};
                    std::vector<float> expected;
                    expected.reserve(live.size());
                    for (const auto& point : live) {
                        expected.push_back(point.sq_dist(query));
                    }
                    std::sort(expected.begin(), expected.end());
                    expected.resize(K);

                    const auto result = tree.knn_search(query, K);
                    REQUIRE(result.size() == K);
                    for (std::size_t i = 0; i < K; ++i) {
                        REQUIRE(result[i].sq_dist == expected[i]);
                    }
                }
            }
        }
    }
}

SCENARIO("KDTree3::rebuild_all on an empty tree leaves it empty and queryable", "[kd_tree][rebuild]") {
    GIVEN("a freshly constructed empty tree") {
        KDTree<3> tree{{.capacity = 8, .resolution = 0.01f}};
        using P = KDTree<3>::Point;
        WHEN("rebuild_all is invoked") {
            tree.rebuild_all();
            THEN("size stays zero and knn_search on the empty tree returns empty") {
                REQUIRE(tree.size() == 0);
                REQUIRE(tree.knn_search(P{0, 0, 0}, 3).empty());
            }
        }
    }
}

SCENARIO("KDTree3 stays correct under axis-sorted (worst-case) input", "[kd_tree][insert][balance]") {
    GIVEN("a tree of capacity 1000 fed 1000 points sorted along axis 0") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N          = 1000;
        constexpr float       Resolution = 1e-9f;

        std::mt19937                          rng{0x507ED01Au};
        std::uniform_real_distribution<float> coord_yz{-50.0f, 50.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            // Strictly ascending along x; random y/z keeps points well-separated in 3D.
            coords.push_back(P{static_cast<float>(i) * 0.5f, coord_yz(rng), coord_yz(rng)});
        }

        Tree tree{{.capacity = N, .resolution = Resolution}};
        REQUIRE(tree.insert(coords) == N);

        WHEN("knn_search runs against the worst-case-ordered tree") {
            THEN("results agree with a brute-force oracle") {
                std::uniform_real_distribution<float> query_coord{0.0f, static_cast<float>(N) * 0.5f};
                constexpr std::size_t                 K = 5;
                for (int trial = 0; trial < 20; ++trial) {
                    const P            query{query_coord(rng), coord_yz(rng), coord_yz(rng)};
                    std::vector<float> expected;
                    expected.reserve(N);
                    for (const auto& point : coords) {
                        expected.push_back(point.sq_dist(query));
                    }
                    std::sort(expected.begin(), expected.end());
                    expected.resize(K);

                    const auto result = tree.knn_search(query, K);
                    REQUIRE(result.size() == K);
                    for (std::size_t i = 0; i < K; ++i) {
                        REQUIRE(result[i].sq_dist == expected[i]);
                    }
                }
            }
        }
    }
}

SCENARIO("KDTree3::remove past the tombstone threshold triggers a partial rebuild silently",
         "[kd_tree][remove][rebuild]") {
    GIVEN("a tree of capacity 400 populated with 400 well-separated random points") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N          = 400;
        constexpr std::size_t Removed    = N / 2; // well above the 0.25 default threshold
        constexpr float       Resolution = 0.5f;

        std::mt19937                          rng{0x70B35701u};
        std::uniform_real_distribution<float> coord{-1000.0f, 1000.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(P{coord(rng), coord(rng), coord(rng)});
        }

        Tree tree{{.capacity = N, .resolution = Resolution}};
        REQUIRE(tree.insert(coords) == N);

        std::vector<P> to_remove;
        to_remove.reserve(Removed);
        for (std::size_t i = 0; i < Removed; ++i) {
            to_remove.push_back(coords[i]);
        }
        REQUIRE(tree.remove(to_remove) == Removed);

        WHEN("a follow-up remove with no matches runs after the threshold has fired") {
            const std::vector<P> no_match{P{1e6f, 1e6f, 1e6f}};
            const auto           cleared = tree.remove(no_match);
            THEN("nothing is cleared and knn_search agrees with the oracle on the live set") {
                REQUIRE(cleared == 0);
                REQUIRE(tree.size() == N - Removed);

                std::vector<P> live;
                live.reserve(N - Removed);
                for (std::size_t i = Removed; i < N; ++i) {
                    live.push_back(coords[i]);
                }

                constexpr std::size_t K = 5;
                for (int trial = 0; trial < 16; ++trial) {
                    const P            query{coord(rng), coord(rng), coord(rng)};
                    std::vector<float> expected;
                    expected.reserve(live.size());
                    for (const auto& point : live) {
                        expected.push_back(point.sq_dist(query));
                    }
                    std::sort(expected.begin(), expected.end());
                    expected.resize(K);

                    const auto result = tree.knn_search(query, K);
                    REQUIRE(result.size() == K);
                    for (std::size_t i = 0; i < K; ++i) {
                        REQUIRE(result[i].sq_dist == expected[i]);
                    }
                }
            }
        }
    }
}

SCENARIO("KDTree3::knn_search after remove never surfaces tombstoned coordinates", "[kd_tree][remove][gen]") {
    GIVEN("a tree of capacity 200 with 200 well-separated points and a sparse removal") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N          = 200;
        constexpr float       Resolution = 0.5f;

        std::mt19937                          rng{0x10141DEDu};
        std::uniform_real_distribution<float> coord{-1000.0f, 1000.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(P{coord(rng), coord(rng), coord(rng)});
        }

        Tree tree{{.capacity = N, .resolution = Resolution}};
        REQUIRE(tree.insert(coords) == N);

        // A sparse remove keeps the per-subtree tombstone fraction below the default
        // 0.25 trigger, so the stale bucket entries are still in place when we query.
        const std::vector<std::size_t> removed_indices{1, 23, 50, 77, 119, 160};
        std::vector<P>                 to_remove;
        to_remove.reserve(removed_indices.size());
        for (auto index : removed_indices) {
            to_remove.push_back(coords[index]);
        }
        REQUIRE(tree.remove(to_remove) == removed_indices.size());

        WHEN("knn_search runs with k equal to the live set size") {
            std::vector<P> live;
            live.reserve(N - removed_indices.size());
            for (std::size_t i = 0; i < N; ++i) {
                if (std::find(removed_indices.begin(), removed_indices.end(), i) == removed_indices.end()) {
                    live.push_back(coords[i]);
                }
            }
            THEN("results agree with the oracle and none of the removed coords show up") {
                REQUIRE(tree.size() == live.size());

                const P    query{coord(rng), coord(rng), coord(rng)};
                const auto result = tree.knn_search(query, live.size());
                REQUIRE(result.size() == live.size());

                for (const auto& neighbor : result) {
                    for (auto removed_index : removed_indices) {
                        REQUIRE(neighbor.coord.sq_dist(coords[removed_index]) != 0.0f);
                    }
                }

                std::vector<float> expected;
                expected.reserve(live.size());
                for (const auto& point : live) {
                    expected.push_back(point.sq_dist(query));
                }
                std::sort(expected.begin(), expected.end());
                for (std::size_t i = 0; i < live.size(); ++i) {
                    REQUIRE(result[i].sq_dist == expected[i]);
                }
            }
        }
    }
}

SCENARIO("KDTree3 FIFO eviction never surfaces evicted coordinates via stale leaf entries",
         "[kd_tree][insert][fifo][gen]") {
    GIVEN("a tree of capacity 10 fed 15 distinct random points") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t Cap = 10;
        constexpr std::size_t N   = 15;

        std::mt19937                          rng{0x6E14C70Du};
        std::uniform_real_distribution<float> coord{-100.0f, 100.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(P{coord(rng), coord(rng), coord(rng)});
        }

        Tree tree{{.capacity = Cap, .resolution = 1e-9f}};
        REQUIRE(tree.insert(coords) == N);

        WHEN("knn_search asks for every live point with k = capacity") {
            const P    query{coord(rng), coord(rng), coord(rng)};
            const auto result = tree.knn_search(query, Cap);
            THEN("the result contains only the most recent Cap points") {
                REQUIRE(tree.size() == Cap);
                REQUIRE(result.size() == Cap);

                std::vector<float> expected;
                expected.reserve(Cap);
                for (std::size_t i = N - Cap; i < N; ++i) {
                    expected.push_back(coords[i].sq_dist(query));
                }
                std::sort(expected.begin(), expected.end());
                for (std::size_t i = 0; i < Cap; ++i) {
                    REQUIRE(result[i].sq_dist == expected[i]);
                }

                for (const auto& neighbor : result) {
                    for (std::size_t i = 0; i < N - Cap; ++i) {
                        REQUIRE(neighbor.coord.sq_dist(coords[i]) != 0.0f);
                    }
                }
            }
        }
    }
}

SCENARIO("KDTree3::insert after remove rebuilds the tree correctly", "[kd_tree][remove][insert]") {
    GIVEN("a tree of capacity 300 populated with 100 well-separated random points") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N          = 100;
        constexpr std::size_t M          = 10;
        constexpr std::size_t Extra      = 40;
        constexpr float       Resolution = 0.5f;

        std::mt19937                          rng{0xFADEDFADu};
        std::uniform_real_distribution<float> coord{-1000.0f, 1000.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(P{coord(rng), coord(rng), coord(rng)});
        }

        Tree tree{{.capacity = N + Extra, .resolution = Resolution}};
        REQUIRE(tree.insert(coords) == N);

        const std::vector<std::size_t> removed_indices{2, 9, 13, 27, 41, 55, 68, 77, 88, 99};
        std::vector<P>                 to_remove;
        to_remove.reserve(M);
        for (auto index : removed_indices) {
            to_remove.push_back(coords[index]);
        }
        REQUIRE(tree.remove(to_remove) == M);

        std::vector<P> extra_batch;
        extra_batch.reserve(Extra);
        for (std::size_t i = 0; i < Extra; ++i) {
            extra_batch.push_back(P{coord(rng), coord(rng), coord(rng)});
        }
        REQUIRE(tree.insert(extra_batch) == Extra);

        WHEN("knn_search runs against the live set after the remove + insert sequence") {
            std::vector<P> live;
            live.reserve(N - M + Extra);
            for (std::size_t i = 0; i < N; ++i) {
                if (std::find(removed_indices.begin(), removed_indices.end(), i) == removed_indices.end()) {
                    live.push_back(coords[i]);
                }
            }
            for (const auto& point : extra_batch) {
                live.push_back(point);
            }

            THEN("results agree with a brute-force oracle on the post-mutation live set") {
                REQUIRE(tree.size() == live.size());
                constexpr std::size_t K = 5;
                for (int trial = 0; trial < 16; ++trial) {
                    const P            query{coord(rng), coord(rng), coord(rng)};
                    std::vector<float> expected;
                    expected.reserve(live.size());
                    for (const auto& point : live) {
                        expected.push_back(point.sq_dist(query));
                    }
                    std::sort(expected.begin(), expected.end());
                    expected.resize(K);

                    const auto result = tree.knn_search(query, K);
                    REQUIRE(result.size() == K);
                    for (std::size_t i = 0; i < K; ++i) {
                        REQUIRE(result[i].sq_dist == expected[i]);
                    }
                }
            }
        }
    }
}

namespace {

template <int Dim>
copse::Point<Dim> random_point(std::mt19937& rng, std::uniform_real_distribution<float>& coord) {
    copse::Point<Dim> point;
    for (int d = 0; d < Dim; ++d) {
        point[d] = coord(rng);
    }
    return point;
}

template <int Dim>
copse::Point<Dim> filled(float value) {
    copse::Point<Dim> point;
    for (int d = 0; d < Dim; ++d) {
        point[d] = value;
    }
    return point;
}

template <int Dim>
bool inside_box(const copse::Point<Dim>& point,
                const copse::Point<Dim>& min_corner,
                const copse::Point<Dim>& max_corner) {
    for (int d = 0; d < Dim; ++d) {
        if (point[d] < min_corner[d] || point[d] > max_corner[d]) {
            return false;
        }
    }
    return true;
}

} // namespace

TEMPLATE_TEST_CASE_SIG("KDTree::box_delete agrees with a linear oracle across D",
                       "[kd_tree][delete][box][oracle]",
                       ((int Dim), Dim),
                       (2),
                       (3),
                       (4)) {
    GIVEN("a tree of 500 uniform random points and a query box") {
        using Tree = KDTree<Dim>;
        using P    = typename Tree::Point;

        constexpr std::size_t N = 500;

        std::mt19937                          rng{0xD31E7E01u + static_cast<unsigned>(Dim) * 101u};
        std::uniform_real_distribution<float> coord{-100.0f, 100.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(random_point<Dim>(rng, coord));
        }

        Tree tree{{.capacity = N, .resolution = 1e-9f}};
        REQUIRE(tree.insert(coords) == N);

        const P min_corner = filled<Dim>(-40.0f);
        const P max_corner = filled<Dim>(40.0f);

        WHEN("box_delete is called with that box") {
            const auto cleared = tree.box_delete({BBox<Dim>{min_corner, max_corner}});
            THEN("the cleared count, live size, and remaining set match the oracle") {
                std::size_t    expected_cleared = 0;
                std::vector<P> survivors;
                for (const auto& point : coords) {
                    if (inside_box<Dim>(point, min_corner, max_corner)) {
                        ++expected_cleared;
                    } else {
                        survivors.push_back(point);
                    }
                }
                REQUIRE(cleared == expected_cleared);
                REQUIRE(tree.size() == survivors.size());

                // No surviving query result lands inside the deleted box.
                for (int trial = 0; trial < 16; ++trial) {
                    const P    query  = random_point<Dim>(rng, coord);
                    const auto result = tree.knn_search(query, 5);
                    for (const auto& neighbor : result) {
                        REQUIRE_FALSE(inside_box<Dim>(neighbor.coord, min_corner, max_corner));
                    }
                }

                // knn over the whole live set agrees with the oracle on survivors.
                if (!survivors.empty()) {
                    const std::size_t k = std::min<std::size_t>(7, survivors.size());
                    for (int trial = 0; trial < 16; ++trial) {
                        const P            query = random_point<Dim>(rng, coord);
                        std::vector<float> expected;
                        expected.reserve(survivors.size());
                        for (const auto& point : survivors) {
                            expected.push_back(point.sq_dist(query));
                        }
                        std::sort(expected.begin(), expected.end());
                        expected.resize(k);
                        const auto result = tree.knn_search(query, k);
                        REQUIRE(result.size() == k);
                        for (std::size_t i = 0; i < k; ++i) {
                            REQUIRE(result[i].sq_dist == expected[i]);
                        }
                    }
                }
            }
        }
    }
}

SCENARIO("KDTree3::box_delete with an empty box clears nothing", "[kd_tree][delete][box][edge]") {
    GIVEN("a tree with clustered points and an inverted (empty) box") {
        KDTree<3> tree{{.capacity = 16, .resolution = 1e-9f}};
        using P = KDTree<3>::Point;
        const std::vector<P> batch{P{0, 0, 0}, P{1, 1, 1}, P{2, 2, 2}};
        REQUIRE(tree.insert(batch) == 3);

        WHEN("box_delete is called with min_corner > max_corner") {
            // An inverted box contains no point, so nothing is inside it.
            const auto cleared = tree.box_delete({BBox<3>{P{10, 10, 10}, P{-10, -10, -10}}});
            THEN("nothing is cleared and size is unchanged") {
                REQUIRE(cleared == 0);
                REQUIRE(tree.size() == 3);
            }
        }
    }
}

SCENARIO("KDTree3::box_delete covering every live point empties the tree",
         "[kd_tree][delete][box][edge]") {
    GIVEN("a tree of 300 random points and a box covering the whole extent") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N = 300;

        std::mt19937                          rng{0xB0C57001u};
        std::uniform_real_distribution<float> coord{-100.0f, 100.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(random_point<3>(rng, coord));
        }

        Tree tree{{.capacity = N, .resolution = 1e-9f}};
        REQUIRE(tree.insert(coords) == N);

        WHEN("box_delete covers the full coordinate range") {
            const auto cleared = tree.box_delete({BBox<3>{P{-1000, -1000, -1000}, P{1000, 1000, 1000}}});
            THEN("every point is cleared and subsequent searches are empty") {
                REQUIRE(cleared == N);
                REQUIRE(tree.size() == 0);
                REQUIRE(tree.knn_search(P{0, 0, 0}, 5).empty());
                REQUIRE(tree.radius_search(P{0, 0, 0}, 1e6f).empty());
            }
        }
    }
}

SCENARIO("KDTree3::box_delete on an empty tree returns 0", "[kd_tree][delete][box][edge]") {
    GIVEN("a freshly constructed tree") {
        KDTree<3> tree{{.capacity = 8, .resolution = 0.01f}};
        using P = KDTree<3>::Point;
        WHEN("box_delete is called") {
            const auto cleared = tree.box_delete({BBox<3>{P{-1, -1, -1}, P{1, 1, 1}}});
            THEN("the count is zero and the tree stays empty") {
                REQUIRE(cleared == 0);
                REQUIRE(tree.size() == 0);
            }
        }
    }
}

TEMPLATE_TEST_CASE_SIG("KDTree::box_delete clears the union of a box batch in one rebuild",
                       "[kd_tree][delete][boxes][oracle]",
                       ((int Dim), Dim),
                       (2),
                       (3),
                       (4)) {
    GIVEN("a tree of 600 uniform random points and a batch of overlapping query boxes") {
        using Tree = KDTree<Dim>;
        using P    = typename Tree::Point;

        constexpr std::size_t N = 600;

        std::mt19937                          rng{0xB0E5B0E5u + static_cast<unsigned>(Dim) * 191u};
        std::uniform_real_distribution<float> coord{-100.0f, 100.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(random_point<Dim>(rng, coord));
        }

        Tree tree{{.capacity = N, .resolution = 1e-9f}};
        REQUIRE(tree.insert(coords) == N);

        // Three boxes; the first two overlap so a point in the intersection is counted once.
        const std::vector<BBox<Dim>> boxes{
            BBox<Dim>{filled<Dim>(-60.0f), filled<Dim>(0.0f)},
            BBox<Dim>{filled<Dim>(-20.0f), filled<Dim>(40.0f)},
            BBox<Dim>{filled<Dim>(70.0f), filled<Dim>(90.0f)},
        };

        const auto in_any_box = [&](const P& point) {
            for (const auto& box : boxes) {
                if (inside_box<Dim>(point, box.min_corner, box.max_corner)) {
                    return true;
                }
            }
            return false;
        };

        WHEN("box_delete is called with the whole batch") {
            const auto cleared = tree.box_delete(std::span<const BBox<Dim>>{boxes.data(), boxes.size()});

            THEN("the cleared count equals the union size and survivors agree with the oracle") {
                std::size_t    expected_cleared = 0;
                std::vector<P> survivors;
                for (const auto& point : coords) {
                    if (in_any_box(point)) {
                        ++expected_cleared;
                    } else {
                        survivors.push_back(point);
                    }
                }
                REQUIRE(cleared == expected_cleared);
                REQUIRE(tree.size() == survivors.size());

                // No surviving query result lands inside any deleted box.
                for (int trial = 0; trial < 16; ++trial) {
                    const P    query  = random_point<Dim>(rng, coord);
                    const auto result = tree.knn_search(query, 5);
                    for (const auto& neighbor : result) {
                        REQUIRE_FALSE(in_any_box(neighbor.coord));
                    }
                }

                // knn over the survivor set agrees with the brute-force oracle.
                if (!survivors.empty()) {
                    const std::size_t k = std::min<std::size_t>(7, survivors.size());
                    for (int trial = 0; trial < 16; ++trial) {
                        const P            query = random_point<Dim>(rng, coord);
                        std::vector<float> expected;
                        expected.reserve(survivors.size());
                        for (const auto& point : survivors) {
                            expected.push_back(point.sq_dist(query));
                        }
                        std::sort(expected.begin(), expected.end());
                        expected.resize(k);
                        const auto result = tree.knn_search(query, k);
                        REQUIRE(result.size() == k);
                        for (std::size_t i = 0; i < k; ++i) {
                            REQUIRE(result[i].sq_dist == expected[i]);
                        }
                    }
                }
            }
        }
    }
}

TEMPLATE_TEST_CASE_SIG("KDTree::radius_crop agrees with a linear oracle across D",
                       "[kd_tree][delete][radius][oracle]",
                       ((int Dim), Dim),
                       (2),
                       (3),
                       (4)) {
    GIVEN("a tree of 500 uniform random points and a sphere") {
        using Tree = KDTree<Dim>;
        using P    = typename Tree::Point;

        constexpr std::size_t N = 500;

        std::mt19937                          rng{0x5A4E5A4Eu + static_cast<unsigned>(Dim) * 131u};
        std::uniform_real_distribution<float> coord{-100.0f, 100.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(random_point<Dim>(rng, coord));
        }

        Tree tree{{.capacity = N, .resolution = 1e-9f}};
        REQUIRE(tree.insert(coords) == N);

        const P     center    = filled<Dim>(10.0f);
        const float radius    = 60.0f;
        const float sq_radius = radius * radius;

        WHEN("radius_crop is called with that sphere") {
            const auto cleared = tree.radius_crop(center, radius);
            THEN("the cleared count, live size, and remaining set match the oracle") {
                std::size_t    expected_cleared = 0;
                std::vector<P> survivors;
                for (const auto& point : coords) {
                    if (point.sq_dist(center) > sq_radius) {
                        ++expected_cleared;
                    } else {
                        survivors.push_back(point);
                    }
                }
                REQUIRE(cleared == expected_cleared);
                REQUIRE(tree.size() == survivors.size());

                // Every survivor returned by a wide search lies within the sphere.
                const auto remaining = tree.radius_search(center, 1e6f);
                REQUIRE(remaining.size() == survivors.size());
                for (const auto& neighbor : remaining) {
                    REQUIRE(neighbor.coord.sq_dist(center) <= sq_radius);
                }

                if (!survivors.empty()) {
                    const std::size_t k = std::min<std::size_t>(7, survivors.size());
                    for (int trial = 0; trial < 16; ++trial) {
                        const P            query = random_point<Dim>(rng, coord);
                        std::vector<float> expected;
                        expected.reserve(survivors.size());
                        for (const auto& point : survivors) {
                            expected.push_back(point.sq_dist(query));
                        }
                        std::sort(expected.begin(), expected.end());
                        expected.resize(k);
                        const auto result = tree.knn_search(query, k);
                        REQUIRE(result.size() == k);
                        for (std::size_t i = 0; i < k; ++i) {
                            REQUIRE(result[i].sq_dist == expected[i]);
                        }
                    }
                }
            }
        }
    }
}

SCENARIO("KDTree3::radius_crop with radius 0 clears every point",
         "[kd_tree][delete][radius][edge]") {
    GIVEN("a tree of 200 random points and a center coincident with no stored point") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N = 200;

        std::mt19937                          rng{0x2E0EAD00u};
        std::uniform_real_distribution<float> coord{-50.0f, 50.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(random_point<3>(rng, coord));
        }

        Tree tree{{.capacity = N, .resolution = 1e-9f}};
        REQUIRE(tree.insert(coords) == N);

        WHEN("radius_crop is called with r = 0 around a fresh center") {
            const auto cleared = tree.radius_crop(P{1000, 1000, 1000}, 0.0f);
            THEN("every point is strictly outside and gets cleared") {
                REQUIRE(cleared == N);
                REQUIRE(tree.size() == 0);
            }
        }
    }
}

SCENARIO("KDTree3::radius_crop keeps a point exactly on the sphere",
         "[kd_tree][delete][radius][edge]") {
    GIVEN("a tree with one point at distance 3 from the center along x") {
        KDTree<3> tree{{.capacity = 8, .resolution = 1e-9f}};
        using P = KDTree<3>::Point;
        const std::vector<P> batch{P{3, 0, 0}, P{100, 0, 0}};
        REQUIRE(tree.insert(batch) == 2);

        WHEN("radius_crop is called with center origin and r = 3") {
            const auto cleared = tree.radius_crop(P{0, 0, 0}, 3.0f);
            THEN("the on-sphere point survives and only the far point is cleared") {
                REQUIRE(cleared == 1);
                REQUIRE(tree.size() == 1);
                const auto result = tree.knn_search(P{0, 0, 0}, 1);
                REQUIRE(result.size() == 1);
                REQUIRE(result[0].coord == P{3, 0, 0});
            }
        }
    }
}

SCENARIO("KDTree3::radius_crop with radius beyond the extent clears nothing",
         "[kd_tree][delete][radius][edge]") {
    GIVEN("a tree of 200 random points and a huge radius") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N = 200;

        std::mt19937                          rng{0xFA57EE01u};
        std::uniform_real_distribution<float> coord{-50.0f, 50.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(random_point<3>(rng, coord));
        }

        Tree tree{{.capacity = N, .resolution = 1e-9f}};
        REQUIRE(tree.insert(coords) == N);

        WHEN("radius_crop runs with r larger than the whole root extent") {
            const auto cleared = tree.radius_crop(P{0, 0, 0}, 1e6f);
            THEN("nothing is cleared and the live set is intact") {
                REQUIRE(cleared == 0);
                REQUIRE(tree.size() == N);
            }
        }
    }
}

SCENARIO("KDTree3::radius_crop on an empty tree returns 0", "[kd_tree][delete][radius][edge]") {
    GIVEN("a freshly constructed tree") {
        KDTree<3> tree{{.capacity = 8, .resolution = 0.01f}};
        using P = KDTree<3>::Point;
        WHEN("radius_crop is called") {
            const auto cleared = tree.radius_crop(P{0, 0, 0}, 1.0f);
            THEN("the count is zero and the tree stays empty") {
                REQUIRE(cleared == 0);
                REQUIRE(tree.size() == 0);
            }
        }
    }
}

SCENARIO("KDTree3 large spatial delete then rebuild_all yields the correct live set",
         "[kd_tree][delete][rebuild][bookkeeping]") {
    GIVEN("a tree of 1000 random points and a box that deletes most of them") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N = 1000;

        std::mt19937                          rng{0x68001CE1u};
        std::uniform_real_distribution<float> coord{-100.0f, 100.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(random_point<3>(rng, coord));
        }

        Tree tree{{.capacity = N, .resolution = 1e-9f}};
        REQUIRE(tree.insert(coords) == N);

        // A box covering the bulk of the extent: most points fall inside.
        const P min_corner = filled<3>(-80.0f);
        const P max_corner = filled<3>(80.0f);

        std::vector<P> survivors;
        for (const auto& point : coords) {
            if (!inside_box<3>(point, min_corner, max_corner)) {
                survivors.push_back(point);
            }
        }

        WHEN("box_delete runs, then rebuild_all, then knn over the live set") {
            const auto cleared = tree.box_delete({BBox<3>{min_corner, max_corner}});
            REQUIRE(cleared == N - survivors.size());
            REQUIRE(tree.size() == survivors.size());

            tree.rebuild_all();

            THEN("the rebuilt live set matches the oracle on survivors exactly") {
                REQUIRE(tree.size() == survivors.size());
                const std::size_t k = std::min<std::size_t>(5, survivors.size());
                for (int trial = 0; trial < 16; ++trial) {
                    const P            query = random_point<3>(rng, coord);
                    std::vector<float> expected;
                    expected.reserve(survivors.size());
                    for (const auto& point : survivors) {
                        expected.push_back(point.sq_dist(query));
                    }
                    std::sort(expected.begin(), expected.end());
                    expected.resize(k);
                    const auto result = tree.knn_search(query, k);
                    REQUIRE(result.size() == k);
                    for (std::size_t i = 0; i < k; ++i) {
                        REQUIRE(result[i].sq_dist == expected[i]);
                    }
                }
            }
        }
    }
}

SCENARIO("KDTree3 spatial delete then re-insert agrees with the oracle on the live set",
         "[kd_tree][delete][insert][bookkeeping]") {
    GIVEN("a tree of 600 random points, a sphere delete, then a fresh insert batch") {
        using Tree = KDTree<3>;
        using P    = Tree::Point;

        constexpr std::size_t N     = 600;
        constexpr std::size_t Extra = 200;

        std::mt19937                          rng{0xC0DEFEEDu};
        std::uniform_real_distribution<float> coord{-100.0f, 100.0f};

        std::vector<P> coords;
        coords.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            coords.push_back(random_point<3>(rng, coord));
        }

        Tree tree{{.capacity = N + Extra, .resolution = 1e-9f}};
        REQUIRE(tree.insert(coords) == N);

        const P     center    = filled<3>(0.0f);
        const float radius    = 50.0f;
        const float sq_radius = radius * radius;

        std::vector<P> live;
        for (const auto& point : coords) {
            if (point.sq_dist(center) <= sq_radius) {
                live.push_back(point);
            }
        }

        WHEN("radius_crop runs, then a disjoint batch is inserted") {
            const auto cleared = tree.radius_crop(center, radius);
            REQUIRE(cleared == N - live.size());

            // Fresh points placed far from the surviving cluster so dedup never fires.
            std::uniform_real_distribution<float> far_coord{5000.0f, 6000.0f};
            std::vector<P>                        extra;
            extra.reserve(Extra);
            for (std::size_t i = 0; i < Extra; ++i) {
                extra.push_back(random_point<3>(rng, far_coord));
                live.push_back(extra.back());
            }
            REQUIRE(tree.insert(extra) == Extra);

            THEN("knn over the post-mutation live set agrees with the oracle") {
                REQUIRE(tree.size() == live.size());
                constexpr std::size_t K = 5;
                for (int trial = 0; trial < 16; ++trial) {
                    const P            query = random_point<3>(rng, coord);
                    std::vector<float> expected;
                    expected.reserve(live.size());
                    for (const auto& point : live) {
                        expected.push_back(point.sq_dist(query));
                    }
                    std::sort(expected.begin(), expected.end());
                    expected.resize(K);
                    const auto result = tree.knn_search(query, K);
                    REQUIRE(result.size() == K);
                    for (std::size_t i = 0; i < K; ++i) {
                        REQUIRE(result[i].sq_dist == expected[i]);
                    }
                }
            }
        }
    }
}

} // namespace copse
