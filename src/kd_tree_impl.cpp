#include "copse/impl/kd_tree_impl.hpp"

#include <limits>
#include <stdexcept>

namespace copse::internal {

template <int Dim>
KDTreeImpl<Dim>::KDTreeImpl(Config cfg)
    : cfg_(cfg)
    , points_(cfg.capacity)
    , leaf_buckets_(cfg.capacity * 2)
    , nodes_()
    , leaf_bboxes_()
    , root_bbox_{}
    , work_indices_()
    , builder_(nodes_,
               leaf_buckets_,
               leaf_bboxes_,
               root_bbox_,
               points_,
               cfg.leaf_bucket_size,
               cfg.alpha,
               cfg.tombstone_threshold)
    , kernel_(nodes_, leaf_buckets_, points_) {
    work_indices_.reserve(cfg.capacity);
    if (cfg.capacity == 0) {
        throw std::invalid_argument{"KDTree::Config: capacity must be > 0"};
    }
    if (!(cfg.resolution > 0.0f)) {
        throw std::invalid_argument{"KDTree::Config: resolution must be > 0"};
    }
    if (cfg.leaf_bucket_size == 0) {
        throw std::invalid_argument{"KDTree::Config: leaf_bucket_size must be > 0"};
    }
    if (!(cfg.alpha > 0.5f && cfg.alpha < 1.0f)) {
        throw std::invalid_argument{"KDTree::Config: alpha must lie in (0.5, 1.0)"};
    }
    if (!(cfg.tombstone_threshold >= 0.0f && cfg.tombstone_threshold <= 1.0f)) {
        throw std::invalid_argument{"KDTree::Config: tombstone_threshold must lie in [0, 1]"};
    }
}

template <int Dim>
std::size_t KDTreeImpl<Dim>::insert(std::span<const Point> points) {
    const float sq_res = cfg_.resolution * cfg_.resolution;

    std::size_t inserted = 0;
    for (const auto& point : points) {
        // Dedup: tree-side any_within covers both intra-batch (just-acquired
        // points are already in the tree via insert_index) and prior batches.
        if (root_ != TreeNode::INVALID && kernel_.any_within(root_, point, sq_res)) {
            continue;
        }
        const auto index = points_.acquire(point);

        if (root_ == TreeNode::INVALID) {
            work_indices_.assign({index});
            root_ = builder_.rebuild(work_indices_);
        } else {
            builder_.insert_index(root_, index);
        }
        ++inserted;
    }

    if (inserted > 0) {
        builder_.maybe_partial_rebuild_full(root_);
    }
    return inserted;
}

template <int Dim>
std::size_t KDTreeImpl<Dim>::remove(std::span<const Point> queries) {
    const float sq_radius = cfg_.resolution * cfg_.resolution;

    std::size_t cleared = 0;
    for (const auto& query : queries) {
        const auto matches = kernel_.collect_indices_within(root_, query, sq_radius);
        for (auto idx : matches) {
            builder_.tombstone_index(root_, idx); // BEFORE release
            points_.release(idx);
            ++cleared;
        }
    }

    if (cleared > 0) {
        builder_.maybe_partial_rebuild(root_);
    }
    return cleared;
}

template <int Dim>
auto KDTreeImpl<Dim>::knn_search(const Point& query, std::size_t k) const -> std::vector<Neighbor> {
    return kernel_.search(root_, query, k, std::numeric_limits<float>::infinity());
}

template <int Dim>
auto KDTreeImpl<Dim>::radius_search(const Point& query, float radius) const -> std::vector<Neighbor> {
    return kernel_.search(root_, query, std::numeric_limits<std::size_t>::max(), radius * radius);
}

template <int Dim>
auto KDTreeImpl<Dim>::hybrid_search(const Point& query, std::size_t k, float radius) const
    -> std::vector<Neighbor> {
    return kernel_.search(root_, query, k, radius * radius);
}

template <int Dim>
std::size_t KDTreeImpl<Dim>::box_delete(std::span<const BBox<Dim>> boxes) {
    std::size_t cleared = 0;
    for (const auto& box : boxes) {
        cleared += builder_.box_delete(root_, box);
    }
    if (cleared > 0) {
        builder_.maybe_partial_rebuild(root_);
    }
    return cleared;
}

template <int Dim>
std::size_t KDTreeImpl<Dim>::radius_crop(const Point& center, float r) {
    const std::size_t cleared = builder_.radius_crop(root_, center, r);
    if (cleared > 0) {
        builder_.maybe_partial_rebuild(root_);
    }
    return cleared;
}

template <int Dim>
std::size_t KDTreeImpl<Dim>::size() const noexcept {
    return points_.size();
}

template <int Dim>
std::size_t KDTreeImpl<Dim>::capacity() const noexcept {
    return points_.capacity();
}

template <int Dim>
void KDTreeImpl<Dim>::rebuild_all() {
    nodes_.clear();
    leaf_buckets_.clear();
    leaf_bboxes_.clear();

    work_indices_.clear();
    points_.for_each_live([this](std::uint32_t index, const Point&) { work_indices_.push_back(index); });

    if (work_indices_.empty()) {
        root_ = TreeNode::INVALID;
        return;
    }
    root_ = builder_.rebuild(work_indices_);
}

template class KDTreeImpl<2>;
template class KDTreeImpl<3>;
template class KDTreeImpl<4>;

} // namespace copse::internal
