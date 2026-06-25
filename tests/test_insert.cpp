// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
// BDD tests for KDTree<Dim>::insert — dedup, FIFO eviction, worst-case insert order.

#include "copse/kd_tree.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <vector>

namespace copse {

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

} // namespace copse
