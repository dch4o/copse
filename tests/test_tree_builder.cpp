// BDD tests for TreeBuilder<Dim>::rebuild on a small fixed point set.

#include "topiary/impl/leaf_bucket.hpp"
#include "topiary/impl/point_store.hpp"
#include "topiary/impl/tree_builder.hpp"
#include "topiary/impl/tree_node.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <random>
#include <set>
#include <vector>

namespace topiary::internal {

namespace {

void collect_leaf_indices(const std::vector<TreeNode>& nodes,
                          const LeafBucket&            leaf_buckets,
                          std::uint32_t                node_idx,
                          std::vector<std::uint32_t>&  indices,
                          std::size_t&                 max_depth,
                          std::size_t                  current_depth) {
    if (node_idx == TreeNode::INVALID) {
        return;
    }
    if (current_depth > max_depth) {
        max_depth = current_depth;
    }
    const auto& node = nodes[node_idx];
    if (node.is_leaf) {
        for (const auto& entry : leaf_buckets.view(node.bucket_offset, node.bucket_size)) {
            indices.push_back(entry.index);
        }
        return;
    }
    collect_leaf_indices(nodes, leaf_buckets, node.left, indices, max_depth, current_depth + 1);
    collect_leaf_indices(nodes, leaf_buckets, node.right, indices, max_depth, current_depth + 1);
}

template <int Dim>
void collect_leaf_nodes(const std::vector<TreeNode>& nodes,
                        std::uint32_t                node_idx,
                        std::vector<std::uint32_t>&  leaf_node_indices) {
    if (node_idx == TreeNode::INVALID) {
        return;
    }
    const auto& node = nodes[node_idx];
    if (node.is_leaf) {
        leaf_node_indices.push_back(node_idx);
        return;
    }
    collect_leaf_nodes<Dim>(nodes, node.left, leaf_node_indices);
    collect_leaf_nodes<Dim>(nodes, node.right, leaf_node_indices);
}

template <int Dim>
std::uint32_t
descend_to_leaf(const std::vector<TreeNode>& nodes, std::uint32_t root, const detail::PointType<Dim>& point) {
    std::uint32_t node_idx = root;
    while (!nodes[node_idx].is_leaf) {
        const auto& node = nodes[node_idx];
        node_idx         = (point[node.split_dim] < node.split_value) ? node.left : node.right;
    }
    return node_idx;
}

} // namespace

SCENARIO("TreeBuilder<3>::rebuild on a small fixed point set", "[tree_builder][rebuild]") {
    GIVEN("a PointStore<3> filled with 10 fixed points and a builder with leaf_bucket_size=2") {
        using P = detail::PointType<3>;
        const std::vector<P> coords{
            P{0, 0, 0},
            P{1, 0, 0},
            P{0, 1, 0},
            P{0, 0, 1},
            P{2, 2, 2},
            P{3, 1, 0},
            P{1, 3, 1},
            P{2, 0, 3},
            P{4, 4, 4},
            P{5, 1, 2},
        };

        PointStore<3>         points{coords.size()};
        LeafBucket            leaf_buckets{coords.size() * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{nodes,
                               leaf_buckets,
                               leaf_bboxes,
                               root_bbox,
                               points,
                               /*leaf_bucket_size=*/2,
                               0.7f,
                               0.25f};

        std::vector<std::uint32_t> live_indices;
        for (const auto& coord : coords) {
            live_indices.push_back(points.acquire(coord));
        }

        WHEN("rebuild is called over the live indices") {
            const auto root = builder.rebuild(live_indices);
            THEN("a non-INVALID root is returned") {
                REQUIRE(root != TreeNode::INVALID);
            }
            AND_THEN("every input index appears in exactly one leaf bucket") {
                std::vector<std::uint32_t> visited;
                std::size_t                max_depth = 0;
                collect_leaf_indices(nodes, leaf_buckets, root, visited, max_depth, 0);

                REQUIRE(visited.size() == live_indices.size());
                std::set<std::uint32_t> visited_set(visited.begin(), visited.end());
                REQUIRE(visited_set.size() == live_indices.size());
                for (auto idx : live_indices) {
                    REQUIRE(visited_set.contains(idx));
                }
            }
            AND_THEN("root subtree counts equal the input size with no tombstones") {
                REQUIRE(nodes[root].subtree_live_count == coords.size());
                REQUIRE(nodes[root].subtree_total_count == coords.size());
            }
            AND_THEN("depth is bounded near log2(N)") {
                std::vector<std::uint32_t> visited;
                std::size_t                max_depth = 0;
                collect_leaf_indices(nodes, leaf_buckets, root, visited, max_depth, 0);
                REQUIRE(max_depth <= 6);
            }
        }
    }

    GIVEN("an empty live-index span") {
        PointStore<3>         points{4};
        LeafBucket            leaf_buckets{8};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{nodes, leaf_buckets, leaf_bboxes, root_bbox, points, 4, 0.7f, 0.25f};

        WHEN("rebuild is called") {
            const auto root = builder.rebuild(std::span<std::uint32_t>{});
            THEN("the returned root is INVALID and no nodes were emitted") {
                REQUIRE(root == TreeNode::INVALID);
                REQUIRE(nodes.empty());
            }
        }
    }
}

SCENARIO("TreeBuilder<3>::rebuild populates leaf BBoxes, root BBox, and leaf capacity",
         "[tree_builder][rebuild][bbox]") {
    GIVEN("a PointStore<3> filled with 10 fixed points and a builder with leaf_bucket_size=5") {
        using P = detail::PointType<3>;
        const std::vector<P> coords{
            P{0, 0, 0},
            P{1, 0, 0},
            P{0, 1, 0},
            P{0, 0, 1},
            P{2, 2, 2},
            P{3, 1, 0},
            P{1, 3, 1},
            P{2, 0, 3},
            P{4, 4, 4},
            P{5, 1, 2},
        };

        PointStore<3>         points{coords.size()};
        LeafBucket            leaf_buckets{coords.size() * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        constexpr std::size_t leaf_bucket_size = 5;
        TreeBuilder<3>        builder{
            nodes, leaf_buckets, leaf_bboxes, root_bbox, points, leaf_bucket_size, 0.7f, 0.25f};

        std::vector<std::uint32_t> live_indices;
        for (const auto& coord : coords) {
            live_indices.push_back(points.acquire(coord));
        }

        WHEN("rebuild is called over the live indices") {
            const auto root = builder.rebuild(live_indices);
            REQUIRE(root != TreeNode::INVALID);

            THEN("every leaf has capacity == 2 * leaf_bucket_size and size <= leaf_bucket_size") {
                std::vector<std::uint32_t> leaf_node_indices;
                collect_leaf_nodes<3>(nodes, root, leaf_node_indices);
                REQUIRE_FALSE(leaf_node_indices.empty());
                for (auto idx : leaf_node_indices) {
                    const auto& leaf = nodes[idx];
                    REQUIRE(leaf.bucket_capacity == static_cast<std::uint16_t>(2 * leaf_bucket_size));
                    REQUIRE(leaf.bucket_size <= static_cast<std::uint16_t>(leaf_bucket_size));
                }
            }
            AND_THEN("each leaf_bbox equals the cwiseMin/cwiseMax of its bucket's points") {
                std::vector<std::uint32_t> leaf_node_indices;
                collect_leaf_nodes<3>(nodes, root, leaf_node_indices);
                REQUIRE(leaf_node_indices.size() == leaf_bboxes.size());
                for (auto idx : leaf_node_indices) {
                    const auto& leaf = nodes[idx];
                    REQUIRE(leaf.leaf_bbox_idx < leaf_bboxes.size());
                    P expected_min = P::Constant(std::numeric_limits<float>::infinity());
                    P expected_max = P::Constant(-std::numeric_limits<float>::infinity());
                    for (const auto& entry : leaf_buckets.view(leaf.bucket_offset, leaf.bucket_size)) {
                        const auto& point = points.point(entry.index);
                        expected_min      = expected_min.cwiseMin(point);
                        expected_max      = expected_max.cwiseMax(point);
                    }
                    const auto& bbox = leaf_bboxes[leaf.leaf_bbox_idx];
                    REQUIRE(bbox.min_corner == expected_min);
                    REQUIRE(bbox.max_corner == expected_max);
                }
            }
            AND_THEN("root_bbox equals the cwiseMin/cwiseMax of all live points") {
                P expected_min = P::Constant(std::numeric_limits<float>::infinity());
                P expected_max = P::Constant(-std::numeric_limits<float>::infinity());
                for (auto idx : live_indices) {
                    const auto& point = points.point(idx);
                    expected_min      = expected_min.cwiseMin(point);
                    expected_max      = expected_max.cwiseMax(point);
                }
                REQUIRE(root_bbox.min_corner == expected_min);
                REQUIRE(root_bbox.max_corner == expected_max);
            }
        }
    }
}

SCENARIO("TreeBuilder<3>::insert_index appends to the routed leaf and bumps subtree counts",
         "[tree_builder][insert]") {
    GIVEN("a rebuilt tree over 8 fixed points with leaf_bucket_size=2 and room to grow") {
        using P = detail::PointType<3>;
        const std::vector<P> coords{
            P{0, 0, 0},
            P{1, 0, 0},
            P{0, 1, 0},
            P{0, 0, 1},
            P{2, 2, 2},
            P{3, 1, 0},
            P{1, 3, 1},
            P{2, 0, 3},
        };

        PointStore<3>         points{coords.size() + 4};
        LeafBucket            leaf_buckets{(coords.size() + 4) * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        constexpr std::size_t leaf_bucket_size = 2;
        TreeBuilder<3>        builder{
            nodes, leaf_buckets, leaf_bboxes, root_bbox, points, leaf_bucket_size, 0.7f, 0.25f};

        std::vector<std::uint32_t> live_indices;
        for (const auto& coord : coords) {
            live_indices.push_back(points.acquire(coord));
        }
        const auto root = builder.rebuild(live_indices);
        REQUIRE(root != TreeNode::INVALID);

        WHEN("an in-extent point is inserted") {
            const P    new_point{1.5f, 1.5f, 1.5f};
            const auto new_index        = points.acquire(new_point);
            const P    saved_root_min   = root_bbox.min_corner;
            const P    saved_root_max   = root_bbox.max_corner;
            const auto target_leaf      = descend_to_leaf<3>(nodes, root, new_point);
            const auto saved_leaf_size  = nodes[target_leaf].bucket_size;
            const auto saved_leaf_live  = nodes[target_leaf].subtree_live_count;
            const auto saved_leaf_total = nodes[target_leaf].subtree_total_count;

            // Capture saved subtree counts for every node on the descent path so the
            // post-call assertion can check each was bumped by exactly 1.
            std::vector<std::uint32_t> descent_path;
            std::vector<std::uint32_t> saved_path_live;
            std::vector<std::uint32_t> saved_path_total;
            {
                std::uint32_t node_idx = root;
                while (true) {
                    descent_path.push_back(node_idx);
                    saved_path_live.push_back(nodes[node_idx].subtree_live_count);
                    saved_path_total.push_back(nodes[node_idx].subtree_total_count);
                    const auto& node = nodes[node_idx];
                    if (node.is_leaf)
                        break;
                    node_idx = (new_point[node.split_dim] < node.split_value) ? node.left : node.right;
                }
            }

            builder.insert_index(root, new_index);

            THEN("every node on the descent path has subtree counts bumped by 1") {
                for (std::size_t i = 0; i < descent_path.size(); ++i) {
                    REQUIRE(nodes[descent_path[i]].subtree_live_count == saved_path_live[i] + 1);
                    REQUIRE(nodes[descent_path[i]].subtree_total_count == saved_path_total[i] + 1);
                }
            }
            AND_THEN("each visited node has its subtree counts bumped by 1") {
                REQUIRE(nodes[target_leaf].bucket_size == saved_leaf_size + 1);
                REQUIRE(nodes[target_leaf].subtree_live_count == saved_leaf_live + 1);
                REQUIRE(nodes[target_leaf].subtree_total_count == saved_leaf_total + 1);
                REQUIRE(nodes[root].subtree_live_count == coords.size() + 1);
                REQUIRE(nodes[root].subtree_total_count == coords.size() + 1);
            }
            AND_THEN("the inserted index lives in the target leaf's bucket") {
                const auto& leaf  = nodes[target_leaf];
                const auto  slice = leaf_buckets.view(leaf.bucket_offset, leaf.bucket_size);
                bool        found = false;
                for (const auto& entry : slice) {
                    if (entry.index == new_index) {
                        found = true;
                        break;
                    }
                }
                REQUIRE(found);
            }
            AND_THEN("root_bbox does not shrink when the point lies inside the prior extent") {
                REQUIRE(root_bbox.min_corner == saved_root_min);
                REQUIRE(root_bbox.max_corner == saved_root_max);
            }
        }

        WHEN("an out-of-extent point is inserted") {
            const P    saved_root_min = root_bbox.min_corner;
            const P    saved_root_max = root_bbox.max_corner;
            const P    new_point{-1.0f, -2.0f, -3.0f};
            const auto new_index   = points.acquire(new_point);
            const auto target_leaf = descend_to_leaf<3>(nodes, root, new_point);
            const auto bbox_idx    = nodes[target_leaf].leaf_bbox_idx;

            builder.insert_index(root, new_index);

            THEN("root_bbox expands via cwiseMin to include the new point") {
                REQUIRE(root_bbox.min_corner == saved_root_min.cwiseMin(new_point));
                REQUIRE(root_bbox.max_corner == saved_root_max.cwiseMax(new_point));
            }
            AND_THEN("the touched leaf's bbox expands to include the new point") {
                const auto& bbox = leaf_bboxes[bbox_idx];
                REQUIRE(bbox.min_corner[0] <= new_point[0]);
                REQUIRE(bbox.min_corner[1] <= new_point[1]);
                REQUIRE(bbox.min_corner[2] <= new_point[2]);
                REQUIRE(bbox.max_corner[0] >= new_point[0]);
                REQUIRE(bbox.max_corner[1] >= new_point[1]);
                REQUIRE(bbox.max_corner[2] >= new_point[2]);
            }
        }
    }
}

SCENARIO("TreeBuilder<3>::tombstone_index decrements live counts and records the descent path",
         "[tree_builder][tombstone]") {
    GIVEN("a rebuilt tree over 10 fixed points with leaf_bucket_size=2") {
        using P = detail::PointType<3>;
        const std::vector<P> coords{
            P{0, 0, 0},
            P{1, 0, 0},
            P{0, 1, 0},
            P{0, 0, 1},
            P{2, 2, 2},
            P{3, 1, 0},
            P{1, 3, 1},
            P{2, 0, 3},
            P{4, 4, 4},
            P{5, 1, 2},
        };

        PointStore<3>         points{coords.size()};
        LeafBucket            leaf_buckets{coords.size() * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        constexpr std::size_t leaf_bucket_size = 2;
        TreeBuilder<3>        builder{
            nodes, leaf_buckets, leaf_bboxes, root_bbox, points, leaf_bucket_size, 0.7f, 0.25f};

        std::vector<std::uint32_t> live_indices;
        for (const auto& coord : coords) {
            live_indices.push_back(points.acquire(coord));
        }
        const auto root = builder.rebuild(live_indices);
        REQUIRE(root != TreeNode::INVALID);

        const auto  victim       = live_indices.front();
        const auto& victim_point = points.point(victim);

        std::vector<std::uint32_t> expected_path;
        {
            std::uint32_t node_idx = root;
            while (true) {
                expected_path.push_back(node_idx);
                const auto& node = nodes[node_idx];
                if (node.is_leaf) {
                    break;
                }
                node_idx = (victim_point[node.split_dim] < node.split_value) ? node.left : node.right;
            }
        }

        std::vector<std::uint32_t> saved_live;
        std::vector<std::uint32_t> saved_total;
        saved_live.reserve(expected_path.size());
        saved_total.reserve(expected_path.size());
        for (auto idx : expected_path) {
            saved_live.push_back(nodes[idx].subtree_live_count);
            saved_total.push_back(nodes[idx].subtree_total_count);
        }

        const auto               target_leaf      = expected_path.back();
        const auto               bbox_idx         = nodes[target_leaf].leaf_bbox_idx;
        const auto               saved_bbox       = leaf_bboxes[bbox_idx];
        const auto               saved_bucket_off = nodes[target_leaf].bucket_offset;
        const auto               saved_bucket_sz  = nodes[target_leaf].bucket_size;
        const auto               saved_bucket_cap = nodes[target_leaf].bucket_capacity;
        const auto               saved_view_slice = leaf_buckets.view(saved_bucket_off, saved_bucket_sz);
        std::vector<BucketEntry> saved_bucket_contents(saved_view_slice.begin(), saved_view_slice.end());

        // Capture every node's pre-tombstone live count so we can verify off-path nodes
        // are untouched (the removed `modified_indices == expected_path` assertion is
        // recast into observable side-effects: on-path counts decrement, off-path don't).
        std::vector<std::uint32_t> all_indices;
        {
            std::vector<std::uint32_t> stack{root};
            while (!stack.empty()) {
                const auto idx = stack.back();
                stack.pop_back();
                all_indices.push_back(idx);
                const auto& node = nodes[idx];
                if (!node.is_leaf) {
                    stack.push_back(node.left);
                    stack.push_back(node.right);
                }
            }
        }
        std::vector<std::uint32_t> saved_all_live;
        saved_all_live.reserve(all_indices.size());
        for (auto idx : all_indices) {
            saved_all_live.push_back(nodes[idx].subtree_live_count);
        }
        const std::set<std::uint32_t> expected_path_set(expected_path.begin(), expected_path.end());

        WHEN("tombstone_index is invoked for a live index") {
            builder.tombstone_index(root, victim);

            THEN("nodes not on the descent path retain their pre-tombstone live count") {
                for (std::size_t i = 0; i < all_indices.size(); ++i) {
                    if (expected_path_set.contains(all_indices[i]))
                        continue;
                    REQUIRE(nodes[all_indices[i]].subtree_live_count == saved_all_live[i]);
                }
            }
            AND_THEN("every node on the path has subtree_live_count decremented by 1") {
                for (std::size_t i = 0; i < expected_path.size(); ++i) {
                    REQUIRE(nodes[expected_path[i]].subtree_live_count == saved_live[i] - 1);
                }
            }
            AND_THEN("every node on the path has subtree_total_count unchanged") {
                for (std::size_t i = 0; i < expected_path.size(); ++i) {
                    REQUIRE(nodes[expected_path[i]].subtree_total_count == saved_total[i]);
                }
            }
            AND_THEN("the target leaf's BBox is byte-equal to its pre-tombstone value") {
                const auto& bbox = leaf_bboxes[bbox_idx];
                REQUIRE(bbox.min_corner == saved_bbox.min_corner);
                REQUIRE(bbox.max_corner == saved_bbox.max_corner);
            }
            AND_THEN("the target leaf's bucket offset, size, capacity and contents are untouched") {
                const auto& leaf = nodes[target_leaf];
                REQUIRE(leaf.bucket_offset == saved_bucket_off);
                REQUIRE(leaf.bucket_size == saved_bucket_sz);
                REQUIRE(leaf.bucket_capacity == saved_bucket_cap);
                const auto view_slice = leaf_buckets.view(leaf.bucket_offset, leaf.bucket_size);
                REQUIRE(view_slice.size() == saved_bucket_contents.size());
                for (std::size_t i = 0; i < view_slice.size(); ++i) {
                    REQUIRE(view_slice[i].index == saved_bucket_contents[i].index);
                    REQUIRE(view_slice[i].gen == saved_bucket_contents[i].gen);
                }
            }
        }
    }
}

namespace {

bool tree_nodes_field_equal(const TreeNode& lhs, const TreeNode& rhs) {
    if (lhs.is_leaf != rhs.is_leaf)
        return false;
    if (lhs.subtree_live_count != rhs.subtree_live_count)
        return false;
    if (lhs.subtree_total_count != rhs.subtree_total_count)
        return false;
    if (lhs.is_leaf) {
        return lhs.bucket_offset == rhs.bucket_offset && lhs.bucket_size == rhs.bucket_size &&
               lhs.bucket_capacity == rhs.bucket_capacity && lhs.leaf_bbox_idx == rhs.leaf_bbox_idx;
    }
    return lhs.split_dim == rhs.split_dim && lhs.split_value == rhs.split_value && lhs.left == rhs.left &&
           lhs.right == rhs.right;
}

template <int Dim>
std::vector<std::uint32_t> all_node_indices(const std::vector<TreeNode>& nodes, std::uint32_t root) {
    std::vector<std::uint32_t> result;
    if (root == TreeNode::INVALID)
        return result;
    std::vector<std::uint32_t> stack{root};
    while (!stack.empty()) {
        const auto idx = stack.back();
        stack.pop_back();
        result.push_back(idx);
        const auto& node = nodes[idx];
        if (!node.is_leaf) {
            stack.push_back(node.left);
            stack.push_back(node.right);
        }
    }
    return result;
}

} // namespace

SCENARIO("TreeBuilder<3>::maybe_partial_rebuild leaves a balanced tree untouched",
         "[tree_builder][partial_rebuild]") {
    GIVEN("a freshly rebuilt balanced tree over 10 points with no violators") {
        using P = detail::PointType<3>;
        const std::vector<P> coords{
            P{0, 0, 0},
            P{1, 0, 0},
            P{0, 1, 0},
            P{0, 0, 1},
            P{2, 2, 2},
            P{3, 1, 0},
            P{1, 3, 1},
            P{2, 0, 3},
            P{4, 4, 4},
            P{5, 1, 2},
        };

        PointStore<3>         points{coords.size()};
        LeafBucket            leaf_buckets{coords.size() * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{nodes,
                               leaf_buckets,
                               leaf_bboxes,
                               root_bbox,
                               points,
                               /*leaf_bucket_size=*/2,
                               0.7f,
                               0.25f};

        std::vector<std::uint32_t> live_indices;
        for (const auto& coord : coords) {
            live_indices.push_back(points.acquire(coord));
        }
        const auto root = builder.rebuild(live_indices);
        REQUIRE(root != TreeNode::INVALID);

        WHEN("maybe_partial_rebuild runs on a balanced tree") {
            const auto saved_nodes     = nodes;
            const auto saved_pool_size = nodes.size();

            builder.maybe_partial_rebuild(root);

            THEN("the node pool size is unchanged") {
                REQUIRE(nodes.size() == saved_pool_size);
            }
            AND_THEN("every node is field-equal to its pre-call value") {
                REQUIRE(nodes.size() == saved_nodes.size());
                for (std::size_t i = 0; i < nodes.size(); ++i) {
                    REQUIRE(tree_nodes_field_equal(nodes[i], saved_nodes[i]));
                }
            }
        }

        WHEN("maybe_partial_rebuild is invoked with an INVALID current_root") {
            const auto saved_pool_size = nodes.size();
            builder.maybe_partial_rebuild(TreeNode::INVALID);
            THEN("the node pool size is unchanged") {
                REQUIRE(nodes.size() == saved_pool_size);
            }
        }
    }
}

SCENARIO("TreeBuilder<3>::maybe_partial_rebuild fires on a manufactured imbalance",
         "[tree_builder][partial_rebuild][imbalance]") {
    GIVEN("a balanced tree whose root subtree counts are skewed past alpha=0.7") {
        using P = detail::PointType<3>;
        const std::vector<P> coords{
            P{-1, 0, 0},
            P{-2, 0, 0},
            P{1, 0, 0},
            P{2, 0, 0},
        };

        PointStore<3>         points{coords.size()};
        LeafBucket            leaf_buckets{coords.size() * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{nodes,
                               leaf_buckets,
                               leaf_bboxes,
                               root_bbox,
                               points,
                               /*leaf_bucket_size=*/2,
                               0.7f,
                               0.25f};

        std::vector<std::uint32_t> live_indices;
        for (const auto& coord : coords) {
            live_indices.push_back(points.acquire(coord));
        }
        const auto root = builder.rebuild(live_indices);
        REQUIRE(root != TreeNode::INVALID);
        REQUIRE_FALSE(nodes[root].is_leaf);

        // Skew the subtree totals on one child past the alpha=0.7 threshold without
        // touching the actual bucket contents (the rebuild will recompute counts from
        // live points; it tolerates incoming inflated totals because it only consults
        // live_count for the live_indices reservation).
        const auto right_child                 = nodes[root].right;
        nodes[root].subtree_total_count        = 20;
        nodes[right_child].subtree_total_count = 18;
        // 18 > 0.7 * 20 = 14 → root is unbalanced via right.
        // The scoped trigger only inspects nodes a mutation recorded; register root as a real
        // insert/delete touching it would, so the manufactured imbalance is in scope.
        builder.modified_nodes().push_back(root);

        WHEN("maybe_partial_rebuild sweeps the unbalanced tree") {
            const auto saved_pool_size = nodes.size();
            builder.maybe_partial_rebuild(root);

            THEN("the node pool grew (the scapegoat subtree was rebuilt)") {
                REQUIRE(nodes.size() > saved_pool_size);
            }
            AND_THEN("the root slot is still a valid node and its subtree counts are recomputed") {
                REQUIRE(nodes[root].subtree_live_count == live_indices.size());
                REQUIRE(nodes[root].subtree_total_count == live_indices.size());
            }
        }
    }
}

SCENARIO("TreeBuilder<3>::maybe_partial_rebuild fires when tombstone fraction crosses threshold",
         "[tree_builder][partial_rebuild][tombstone]") {
    GIVEN("a balanced tree where >= 25 percent of points get tombstoned") {
        using P = detail::PointType<3>;
        const std::vector<P> coords{
            P{0, 0, 0},
            P{1, 0, 0},
            P{0, 1, 0},
            P{0, 0, 1},
            P{2, 2, 2},
            P{3, 1, 0},
            P{1, 3, 1},
            P{2, 0, 3},
        };

        PointStore<3>         points{coords.size()};
        LeafBucket            leaf_buckets{coords.size() * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{nodes,
                               leaf_buckets,
                               leaf_bboxes,
                               root_bbox,
                               points,
                               /*leaf_bucket_size=*/2,
                               0.7f,
                               0.25f};

        std::vector<std::uint32_t> live_indices;
        for (const auto& coord : coords) {
            live_indices.push_back(points.acquire(coord));
        }
        const auto root = builder.rebuild(live_indices);
        REQUIRE(root != TreeNode::INVALID);

        // Tombstone 2 of 8 points (fraction = 0.25 >= tombstone_threshold).
        builder.tombstone_index(root, live_indices[0]);
        points.release(live_indices[0]);
        builder.tombstone_index(root, live_indices[1]);
        points.release(live_indices[1]);

        REQUIRE(nodes[root].subtree_total_count == coords.size());
        REQUIRE(nodes[root].subtree_live_count == coords.size() - 2);

        WHEN("maybe_partial_rebuild sweeps after the tombstones") {
            const auto saved_pool_size = nodes.size();
            builder.maybe_partial_rebuild(root);

            THEN("the node pool grew (a tombstone-saturated subtree was rebuilt)") {
                REQUIRE(nodes.size() > saved_pool_size);
            }
            AND_THEN("post-rebuild, the root's live_count equals total_count (tombstones flushed)") {
                REQUIRE(nodes[root].subtree_live_count == coords.size() - 2);
                REQUIRE(nodes[root].subtree_total_count == coords.size() - 2);
            }
        }
    }
}

SCENARIO("TreeBuilder<3>::insert_index proactively splits a leaf that hits its 2B cap",
         "[tree_builder][insert][eager_split]") {
    GIVEN("a tree with a single leaf seeded by 5 tightly-clustered points (cap=10)") {
        using P = detail::PointType<3>;
        const std::vector<P> initial_coords{
            P{0.0f, 0.0f, 0.0f},
            P{0.1f, 0.0f, 0.0f},
            P{0.0f, 0.1f, 0.0f},
            P{0.0f, 0.0f, 0.1f},
            P{0.1f, 0.1f, 0.1f},
        };

        constexpr std::size_t leaf_bucket_size = 5;
        const std::size_t     capacity         = 20;

        PointStore<3>         points{capacity};
        LeafBucket            leaf_buckets{capacity * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{
            nodes, leaf_buckets, leaf_bboxes, root_bbox, points, leaf_bucket_size, 0.7f, 0.25f};

        std::vector<std::uint32_t> live_indices;
        for (const auto& coord : initial_coords) {
            live_indices.push_back(points.acquire(coord));
        }
        const auto root = builder.rebuild(live_indices);
        REQUIRE(root != TreeNode::INVALID);
        REQUIRE(nodes[root].is_leaf);
        REQUIRE(nodes[root].bucket_capacity == 2 * leaf_bucket_size);
        REQUIRE(nodes[root].bucket_size == initial_coords.size());

        WHEN("5 more clustered points are inserted, driving the leaf to its 2B cap") {
            for (int i = 0; i < 5; ++i) {
                const P    extra{0.05f + 0.01f * static_cast<float>(i),
                              0.05f + 0.01f * static_cast<float>(i),
                              0.05f + 0.01f * static_cast<float>(i)};
                const auto idx = points.acquire(extra);
                builder.insert_index(root, idx);
            }

            THEN("the previously-leaf root slot is now an internal node with two leaf children") {
                REQUIRE_FALSE(nodes[root].is_leaf);
                const auto left  = nodes[root].left;
                const auto right = nodes[root].right;
                REQUIRE(left != TreeNode::INVALID);
                REQUIRE(right != TreeNode::INVALID);
                REQUIRE(nodes[left].is_leaf);
                REQUIRE(nodes[right].is_leaf);
            }
            AND_THEN("the rebuilt subtree's live count equals 10 (5 seed + 5 inserted)") {
                REQUIRE(nodes[root].subtree_live_count == 10);
                REQUIRE(nodes[root].subtree_total_count == 10);
            }
            AND_THEN("each new leaf has cap = 2 * leaf_bucket_size and size <= leaf_bucket_size") {
                const auto left  = nodes[root].left;
                const auto right = nodes[root].right;
                REQUIRE(nodes[left].bucket_capacity == 2 * leaf_bucket_size);
                REQUIRE(nodes[right].bucket_capacity == 2 * leaf_bucket_size);
                REQUIRE(nodes[left].bucket_size <= leaf_bucket_size);
                REQUIRE(nodes[right].bucket_size <= leaf_bucket_size);
            }
        }
    }
}

SCENARIO("TreeBuilder<3>::maybe_partial_rebuild keeps the scapegoat slot index valid (in-place reuse)",
         "[tree_builder][partial_rebuild][in_place]") {
    GIVEN("a tree of 8 points where the root's right child is forced into imbalance") {
        using P = detail::PointType<3>;
        const std::vector<P> coords{
            P{-3, 0, 0},
            P{-2, 0, 0},
            P{-1, 0, 0},
            P{-0.5f, 0, 0},
            P{0.5f, 0, 0},
            P{1, 0, 0},
            P{2, 0, 0},
            P{3, 0, 0},
        };

        PointStore<3>         points{coords.size()};
        LeafBucket            leaf_buckets{coords.size() * 2};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{nodes,
                               leaf_buckets,
                               leaf_bboxes,
                               root_bbox,
                               points,
                               /*leaf_bucket_size=*/2,
                               0.7f,
                               0.25f};

        std::vector<std::uint32_t> live_indices;
        for (const auto& coord : coords) {
            live_indices.push_back(points.acquire(coord));
        }
        const auto root = builder.rebuild(live_indices);
        REQUIRE(root != TreeNode::INVALID);
        REQUIRE_FALSE(nodes[root].is_leaf);

        const auto right_child        = nodes[root].right;
        const auto saved_root_left    = nodes[root].left;
        const auto saved_root_split_d = nodes[root].split_dim;
        const auto saved_root_split_v = nodes[root].split_value;

        // Inflate the right subtree's totals to force the unbalanced trigger on
        // `right_child`, not on the root itself. The root's total and live counts are
        // bumped proportionally so its max(left,right)/total ratio stays under alpha
        // and its (total-live)/total ratio stays under tombstone_threshold; the sweep
        // therefore descends into the children rather than rebuilding the root first.
        const auto saved_root_left_idx                 = nodes[root].left;
        nodes[root].subtree_total_count                = 100;
        nodes[root].subtree_live_count                 = 100;
        nodes[saved_root_left_idx].subtree_total_count = 50;
        nodes[saved_root_left_idx].subtree_live_count  = 50;
        nodes[right_child].subtree_total_count         = 50;
        nodes[right_child].subtree_live_count          = 50;
        if (!nodes[right_child].is_leaf) {
            const auto rc_left                 = nodes[right_child].left;
            nodes[rc_left].subtree_total_count = 48;
            nodes[rc_left].subtree_live_count  = 48;
        }
        // Scoped trigger inspects only recorded nodes; register the descent path a real mutation
        // would so the sweep reaches and rebuilds right_child in place (the case under test).
        builder.modified_nodes().push_back(root);
        builder.modified_nodes().push_back(right_child);

        WHEN("maybe_partial_rebuild fires on the right child") {
            builder.maybe_partial_rebuild(root);

            THEN("the right child slot index is unchanged and remains a valid node") {
                REQUIRE(right_child < nodes.size());
                REQUIRE(nodes[root].right == right_child);
            }
            AND_THEN("the root's other split metadata is untouched") {
                REQUIRE(nodes[root].left == saved_root_left);
                REQUIRE(nodes[root].split_dim == saved_root_split_d);
                REQUIRE(nodes[root].split_value == saved_root_split_v);
            }
        }
    }
}

namespace {

template <int Dim>
std::uint32_t live_leaf_total(const std::vector<TreeNode>& nodes,
                              const LeafBucket&            leaf_buckets,
                              const PointStore<Dim>&       points,
                              std::uint32_t                node_idx) {
    const TreeNode& node = nodes[node_idx];
    if (node.is_leaf) {
        std::uint32_t live = 0;
        for (const auto& entry : leaf_buckets.view(node.bucket_offset, node.bucket_size)) {
            if (points.generation(entry.index) == entry.gen && points.is_live(entry.index)) {
                ++live;
            }
        }
        return live;
    }
    return live_leaf_total<Dim>(nodes, leaf_buckets, points, node.left) +
           live_leaf_total<Dim>(nodes, leaf_buckets, points, node.right);
}

template <int Dim>
void check_live_counts(const std::vector<TreeNode>& nodes,
                       const LeafBucket&            leaf_buckets,
                       const PointStore<Dim>&       points,
                       std::uint32_t                node_idx) {
    const std::uint32_t actual = live_leaf_total<Dim>(nodes, leaf_buckets, points, node_idx);
    REQUIRE(nodes[node_idx].subtree_live_count == actual);
    if (!nodes[node_idx].is_leaf) {
        check_live_counts<Dim>(nodes, leaf_buckets, points, nodes[node_idx].left);
        check_live_counts<Dim>(nodes, leaf_buckets, points, nodes[node_idx].right);
    }
}

} // namespace

SCENARIO("TreeBuilder<3>::delete_in_box agrees with the oracle and keeps subtree_live_count consistent",
         "[tree_builder][delete][box]") {
    GIVEN("a tree built from 256 uniform random D=3 points") {
        using P = detail::PointType<3>;

        constexpr std::size_t N = 256;

        std::mt19937                          rng{0xDEADD00Du};
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

        WHEN("delete_in_box runs with a sub-box of the extent") {
            const P min_corner{-4.0f, -4.0f, -4.0f};
            const P max_corner{4.0f, 4.0f, 4.0f};

            std::size_t expected = 0;
            for (const auto& point : coords) {
                if ((point.array() >= min_corner.array()).all() &&
                    (point.array() <= max_corner.array()).all()) {
                    ++expected;
                }
            }

            const auto cleared = builder.delete_in_box(root, min_corner, max_corner);

            THEN("the cleared count matches, no in-box point is live, and counts stay consistent") {
                REQUIRE(cleared == expected);
                REQUIRE(points.size() == N - expected);
                for (std::uint32_t i = 0; i < N; ++i) {
                    const bool in_box = (coords[i].array() >= min_corner.array()).all() &&
                                        (coords[i].array() <= max_corner.array()).all();
                    REQUIRE(points.is_live(i) == !in_box);
                }
                check_live_counts<3>(nodes, leaf_buckets, points, root);
            }
        }
    }
}

SCENARIO("TreeBuilder<3>::delete_outside_radius agrees with the oracle and keeps counts consistent",
         "[tree_builder][delete][radius]") {
    GIVEN("a tree built from 256 uniform random D=3 points") {
        using P = detail::PointType<3>;

        constexpr std::size_t N = 256;

        std::mt19937                          rng{0xBADC0FFEu};
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

        WHEN("delete_outside_radius runs with a sphere inside the extent") {
            const P     center{2.0f, -1.0f, 0.5f};
            const float radius    = 6.0f;
            const float sq_radius = radius * radius;

            std::size_t expected = 0;
            for (const auto& point : coords) {
                if ((point - center).squaredNorm() > sq_radius) {
                    ++expected;
                }
            }

            const auto cleared = builder.delete_outside_radius(root, center, radius);

            THEN("the cleared count matches, only inside-sphere points stay live, counts consistent") {
                REQUIRE(cleared == expected);
                REQUIRE(points.size() == N - expected);
                for (std::uint32_t i = 0; i < N; ++i) {
                    const bool outside = (coords[i] - center).squaredNorm() > sq_radius;
                    REQUIRE(points.is_live(i) == !outside);
                }
                check_live_counts<3>(nodes, leaf_buckets, points, root);
            }
        }
    }
}

SCENARIO("TreeBuilder<3>::delete_in_box on an INVALID root returns 0", "[tree_builder][delete][box][empty]") {
    GIVEN("an empty node pool and an INVALID root") {
        PointStore<3>         points{4};
        LeafBucket            leaf_buckets{8};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{nodes, leaf_buckets, leaf_bboxes, root_bbox, points, 4, 0.7f, 0.25f};

        WHEN("delete_in_box is invoked") {
            const auto cleared = builder.delete_in_box(
                TreeNode::INVALID, detail::PointType<3>{-1, -1, -1}, detail::PointType<3>{1, 1, 1});
            THEN("the count is zero") {
                REQUIRE(cleared == 0);
            }
        }
    }
}

SCENARIO("TreeBuilder<3>::delete_outside_radius on an INVALID root returns 0",
         "[tree_builder][delete][radius][empty]") {
    GIVEN("an empty node pool and an INVALID root") {
        PointStore<3>         points{4};
        LeafBucket            leaf_buckets{8};
        std::vector<TreeNode> nodes;
        std::vector<BBox<3>>  leaf_bboxes;
        BBox<3>               root_bbox{};
        TreeBuilder<3>        builder{nodes, leaf_buckets, leaf_bboxes, root_bbox, points, 4, 0.7f, 0.25f};

        WHEN("delete_outside_radius is invoked") {
            const auto cleared =
                builder.delete_outside_radius(TreeNode::INVALID, detail::PointType<3>{0, 0, 0}, 1.0f);
            THEN("the count is zero") {
                REQUIRE(cleared == 0);
            }
        }
    }
}

} // namespace topiary::internal
