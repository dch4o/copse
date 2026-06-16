// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
// Type-erased facade over the ikd-Tree, one concrete impl per run mode.
//
// The harness talks to ikd through this header alone and never includes the
// upstream `ikd_Tree.h`. That keeps the ~42 MB `KD_TREE` object, the per-mode
// rename macros, and the template instantiation out of the harness translation
// unit (project rule: prefer type erasure over exposing the template in the
// header), and lets both BG-off and BG-on impls coexist in one binary.
//
// `make_bg_off` / `make_bg_on` are defined in the matching adapter sources
// (`ikd_adapter_bg_off.cpp` / `ikd_adapter_bg_on.cpp`), each compiled into its
// own object target with the right rebuild-gate macro. Both heap-allocate the
// tree (a stack `KD_TREE` overflows immediately — research §1).
#pragma once

#include <cstddef>
#include <vector>

namespace ikd_facade {

/// @brief Plain x/y/z point, converted to `ikdTree_PointType` at the adapter boundary.
struct Point {
    float x; /// X coordinate.
    float y; /// Y coordinate.
    float z; /// Z coordinate.
};

/// @brief Half-open AABB matching ikd's `min <= p < max` box convention.
struct Box {
    float min[3]; /// Per-axis lower bounds (inclusive).
    float max[3]; /// Per-axis upper bounds (exclusive).
};

/// @brief Opaque per-mode ikd-Tree handle. One concrete impl per object target.
class Tree {
public:
    /// @brief Virtual destructor.
    virtual ~Tree() = default;

    /// @brief One-shot balanced build from a point cloud (ikd `Build`).
    /// @param cloud Points to build from.
    virtual void build(const std::vector<Point>& cloud) = 0;

    /// @brief Raw incremental insert with downsampling off (ikd `Add_Points(_, false)`).
    /// @param batch Points to insert.
    virtual void add_points(const std::vector<Point>& batch) = 0;

    /// @brief k nearest neighbors; fills `sq_dists` with squared distances.
    /// @param query Query point.
    /// @param k Number of neighbors to return.
    /// @param sq_dists Output filled with the squared distances of the results.
    /// @return Number of neighbors found.
    virtual std::size_t knn(const Point& query, int k, std::vector<float>& sq_dists) = 0;

    /// @brief Neighbors within linear `radius`; fills `out`.
    /// @param query Query point.
    /// @param radius Search radius (Euclidean).
    /// @param out Output filled with the matching points.
    /// @return Number of neighbors found.
    virtual std::size_t radius(const Point& query, float radius, std::vector<Point>& out) = 0;

    /// @brief Lazy box delete (ikd `Delete_Point_Boxes`).
    /// @param box Box to clear.
    /// @return Number of points marked deleted.
    virtual int box_delete(const Box& box) = 0;

    /// @brief Lazy delete of a whole box batch in one `Delete_Point_Boxes` call.
    /// @param boxes Boxes to clear.
    /// @return Number of points marked deleted.
    virtual int box_delete(const std::vector<Box>& boxes) = 0;

    /// @brief Total nodes including tombstones (ikd `size`).
    /// @return Node count including tombstones.
    virtual int size() const = 0;

    /// @brief Live (non-tombstoned) points (ikd `validnum`).
    /// @return Live point count.
    virtual int valid() const = 0;
};

/// @brief Mode (b): background rebuild OFF (`Multi_Thread_Rebuild_Point_Num = INT_MAX`).
/// @param delete_param Tombstone fraction triggering rebuild.
/// @param balance_param Subtree-imbalance trigger.
/// @param box_length Downsample voxel edge.
/// @return Newly allocated tree (caller owns).
Tree* make_bg_off(float delete_param, float balance_param, float box_length);

/// @brief Mode (c): background rebuild ON (`Multi_Thread_Rebuild_Point_Num = 1500`).
/// @param delete_param Tombstone fraction triggering rebuild.
/// @param balance_param Subtree-imbalance trigger.
/// @param box_length Downsample voxel edge.
/// @return Newly allocated tree (caller owns).
Tree* make_bg_on(float delete_param, float balance_param, float box_length);

} // namespace ikd_facade
