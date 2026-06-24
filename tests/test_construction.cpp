// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
// BDD tests for KDTree<Dim> construction and Config validation.

#include "copse/kd_tree.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

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

} // namespace copse
