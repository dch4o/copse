// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
// BDD tests for KDTree<Dim> knn / radius / hybrid search against a brute-force oracle.

#include "copse/kd_tree.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace copse {

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

} // namespace copse
