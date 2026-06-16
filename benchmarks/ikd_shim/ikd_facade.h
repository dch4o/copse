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

/// Plain x/y/z point, converted to `ikdTree_PointType` at the adapter boundary.
struct Point {
    float x, y, z;
};

/// Half-open AABB matching ikd's `min <= p < max` box convention.
struct Box {
    float min[3];
    float max[3];
};

/// Opaque per-mode ikd-Tree handle. One concrete impl per object target.
class Tree {
public:
    virtual ~Tree() = default;

    /// One-shot balanced build from a point cloud (ikd `Build`).
    virtual void build(const std::vector<Point>& cloud) = 0;

    /// Raw incremental insert with downsampling off (ikd `Add_Points(_, false)`).
    virtual void add_points(const std::vector<Point>& batch) = 0;

    /// k nearest neighbors; fills `sq_dists` with squared distances. Returns count found.
    virtual std::size_t knn(const Point& query, int k, std::vector<float>& sq_dists) = 0;

    /// Neighbors within linear `radius`; fills `out`. Returns count found.
    virtual std::size_t radius(const Point& query, float radius, std::vector<Point>& out) = 0;

    /// Lazy box delete (ikd `Delete_Point_Boxes`). Returns count marked deleted.
    virtual int delete_box(const Box& box) = 0;

    /// Lazy delete of a whole box batch in one `Delete_Point_Boxes` call. Returns count marked deleted.
    virtual int delete_boxes(const std::vector<Box>& boxes) = 0;

    /// Total nodes including tombstones (ikd `size`).
    virtual int size() const = 0;

    /// Live (non-tombstoned) points (ikd `validnum`).
    virtual int valid() const = 0;
};

/// Mode (b): background rebuild OFF (`Multi_Thread_Rebuild_Point_Num = INT_MAX`).
Tree* make_bg_off(float delete_param, float balance_param, float box_length);

/// Mode (c): background rebuild ON (`Multi_Thread_Rebuild_Point_Num = 1500`).
Tree* make_bg_on(float delete_param, float balance_param, float box_length);

} // namespace ikd_facade
