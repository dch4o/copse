// BDD sanity tests for the TreeNode tagged-union record.

#include "pkd_tree/impl/tree_node.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace pkd_tree::internal {

SCENARIO("TreeNode default state and INVALID sentinel", "[tree_node]") {
    GIVEN("a default-constructed TreeNode") {
        TreeNode n;
        THEN("it is a zeroed leaf") {
            REQUIRE(n.is_leaf);
            REQUIRE(n.subtree_live_count == 0);
            REQUIRE(n.subtree_total_count == 0);
            REQUIRE(n.bucket_offset == 0);
            REQUIRE(n.bucket_size == 0);
            REQUIRE(n.bucket_capacity == 0);
        }
    }

    GIVEN("the INVALID sentinel") {
        THEN("it equals uint32_t max") {
            REQUIRE(TreeNode::INVALID == ~std::uint32_t{0});
        }
    }

    GIVEN("the TreeNode layout") {
        THEN("sizeof(TreeNode) is 32 bytes") {
            STATIC_REQUIRE(sizeof(TreeNode) == 32);
        }
    }
}

} // namespace pkd_tree::internal
