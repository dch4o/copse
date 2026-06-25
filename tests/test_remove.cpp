// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
// BDD tests for KDTree<Dim>::remove — tombstone removal and threshold-triggered rebuild.

#include "copse/kd_tree.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <vector>

namespace copse {

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

} // namespace copse
