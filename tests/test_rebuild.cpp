// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
// BDD tests for KDTree<Dim>::rebuild_all.

#include "copse/kd_tree.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <vector>

namespace copse {

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

} // namespace copse
