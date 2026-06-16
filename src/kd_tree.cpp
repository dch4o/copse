// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
#include "copse/kd_tree.hpp"

#include "copse/impl/kd_tree_impl.hpp"

namespace copse {

template <int Dim>
    requires detail::SupportedDim<Dim>
KDTree<Dim>::KDTree(Config cfg) : impl_(std::make_unique<internal::KDTreeImpl<Dim>>(std::move(cfg))) {}

template <int Dim>
    requires detail::SupportedDim<Dim>
KDTree<Dim>::~KDTree() = default;

template <int Dim>
    requires detail::SupportedDim<Dim>
KDTree<Dim>::KDTree(KDTree&&) noexcept = default;

template <int Dim>
    requires detail::SupportedDim<Dim>
KDTree<Dim>& KDTree<Dim>::operator=(KDTree&&) noexcept = default;

template <int Dim>
    requires detail::SupportedDim<Dim>
std::size_t KDTree<Dim>::insert(std::span<const Point> points) {
    return impl_->insert(points);
}

template <int Dim>
    requires detail::SupportedDim<Dim>
std::size_t KDTree<Dim>::insert(std::initializer_list<Point> points) {
    return insert(std::span<const Point>(points.begin(), points.size()));
}

template <int Dim>
    requires detail::SupportedDim<Dim>
std::size_t KDTree<Dim>::remove(std::span<const Point> queries) {
    return impl_->remove(queries);
}

template <int Dim>
    requires detail::SupportedDim<Dim>
auto KDTree<Dim>::knn_search(const Point& query, std::size_t k) const -> std::vector<Neighbor> {
    return impl_->knn_search(query, k);
}

template <int Dim>
    requires detail::SupportedDim<Dim>
auto KDTree<Dim>::radius_search(const Point& query, float radius) const -> std::vector<Neighbor> {
    return impl_->radius_search(query, radius);
}

template <int Dim>
    requires detail::SupportedDim<Dim>
auto KDTree<Dim>::hybrid_search(const Point& query, std::size_t k, float radius) const
    -> std::vector<Neighbor> {
    return impl_->hybrid_search(query, k, radius);
}

template <int Dim>
    requires detail::SupportedDim<Dim>
std::size_t KDTree<Dim>::box_delete(std::span<const BBox<Dim>> boxes) {
    return impl_->box_delete(boxes);
}

template <int Dim>
    requires detail::SupportedDim<Dim>
std::size_t KDTree<Dim>::box_delete(std::initializer_list<BBox<Dim>> boxes) {
    return box_delete(std::span<const BBox<Dim>>(boxes.begin(), boxes.size()));
}

template <int Dim>
    requires detail::SupportedDim<Dim>
std::size_t KDTree<Dim>::radius_crop(const Point& center, float r) {
    return impl_->radius_crop(center, r);
}

template <int Dim>
    requires detail::SupportedDim<Dim>
std::size_t KDTree<Dim>::size() const noexcept {
    return impl_->size();
}

template <int Dim>
    requires detail::SupportedDim<Dim>
std::size_t KDTree<Dim>::capacity() const noexcept {
    return impl_->capacity();
}

template <int Dim>
    requires detail::SupportedDim<Dim>
void KDTree<Dim>::rebuild_all() {
    impl_->rebuild_all();
}

template class KDTree<2>;
template class KDTree<3>;
template class KDTree<4>;

} // namespace copse
