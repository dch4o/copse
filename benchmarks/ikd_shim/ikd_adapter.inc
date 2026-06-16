// Shared body for the per-mode ikd facade adapters.
//
// Included by `ikd_adapter_bg_off.cpp` and `ikd_adapter_bg_on.cpp` AFTER each
// has set its `Multi_Thread_Rebuild_Point_Num` and `KD_TREE` rename macros, so
// the `KD_TREE<ikdTree_PointType>` here resolves to that mode's renamed class.
// This file is glue — it binds the type-erased `ikd_facade::Tree` to the ikd
// API; it carries no benchmark logic.

#include <memory>

#include "ikd_Tree.h"
#include "ikd_facade.h"

namespace ikd_facade {
namespace {

using PointVector = std::vector<ikdTree_PointType, Eigen::aligned_allocator<ikdTree_PointType>>;

PointVector to_native(const std::vector<Point>& points) {
    PointVector cloud;
    cloud.reserve(points.size());
    for (const auto& point : points) {
        cloud.push_back(ikdTree_PointType(point.x, point.y, point.z));
    }
    return cloud;
}

class IkdTree final : public Tree {
public:
    IkdTree(float delete_param, float balance_param, float box_length)
        : tree_(std::make_unique<KD_TREE<ikdTree_PointType>>(delete_param, balance_param, box_length)) {}

    void build(const std::vector<Point>& cloud) override {
        PointVector points = to_native(cloud);
        tree_->Build(points);
    }

    void add_points(const std::vector<Point>& batch) override {
        PointVector points = to_native(batch);
        tree_->Add_Points(points, /*downsample_on=*/false);
    }

    std::size_t knn(const Point& query, int k, std::vector<float>& sq_dists) override {
        PointVector neighbors;
        tree_->Nearest_Search(ikdTree_PointType(query.x, query.y, query.z), k, neighbors, sq_dists);
        return neighbors.size();
    }

    std::size_t radius(const Point& query, float radius, std::vector<Point>& out) override {
        PointVector inside;
        tree_->Radius_Search(ikdTree_PointType(query.x, query.y, query.z), radius, inside);
        out.clear();
        out.reserve(inside.size());
        for (const auto& point : inside) {
            out.push_back(Point{point.x, point.y, point.z});
        }
        return out.size();
    }

    int box_delete(const Box& box) override { return box_delete(std::vector<Box>{box}); }

    int box_delete(const std::vector<Box>& boxes) override {
        std::vector<BoxPointType> bounds;
        bounds.reserve(boxes.size());
        for (const auto& box : boxes) {
            BoxPointType b;
            for (int axis = 0; axis < 3; ++axis) {
                b.vertex_min[axis] = box.min[axis];
                b.vertex_max[axis] = box.max[axis];
            }
            bounds.push_back(b);
        }
        return tree_->Delete_Point_Boxes(bounds);
    }

    int size() const override { return tree_->size(); }
    int valid() const override { return tree_->validnum(); }

private:
    std::unique_ptr<KD_TREE<ikdTree_PointType>> tree_;
};

} // namespace
} // namespace ikd_facade
