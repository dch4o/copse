// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
// BDD tests for KDTree<Dim>::box_delete and radius_crop spatial deletes.

#include "copse/kd_tree.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <span>
#include <vector>

namespace copse {

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
