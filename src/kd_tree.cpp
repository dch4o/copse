#include "topiary/kd_tree.hpp"

#include "topiary/impl/kd_tree_impl.hpp"

namespace topiary {

template <int Dim>
    requires detail::KDDim<Dim>
KDTree<Dim>::KDTree(Config cfg) : impl_(std::make_unique<internal::KDTreeImpl<Dim>>(std::move(cfg))) {}

template <int Dim>
    requires detail::KDDim<Dim>
KDTree<Dim>::~KDTree() = default;

template <int Dim>
    requires detail::KDDim<Dim>
KDTree<Dim>::KDTree(KDTree&&) noexcept = default;

template <int Dim>
    requires detail::KDDim<Dim>
KDTree<Dim>& KDTree<Dim>::operator=(KDTree&&) noexcept = default;

template <int Dim>
    requires detail::KDDim<Dim>
std::size_t KDTree<Dim>::insert(std::span<const Point> points) {
    return impl_->insert(points);
}

template <int Dim>
    requires detail::KDDim<Dim>
std::size_t KDTree<Dim>::remove(std::span<const Point> queries) {
    return impl_->remove(queries);
}

template <int Dim>
    requires detail::KDDim<Dim>
auto KDTree<Dim>::knn_search(const Point& query, std::size_t k) const -> std::vector<Neighbor> {
    return impl_->knn_search(query, k);
}

template <int Dim>
    requires detail::KDDim<Dim>
auto KDTree<Dim>::radius_search(const Point& query, float radius) const -> std::vector<Neighbor> {
    return impl_->radius_search(query, radius);
}

template <int Dim>
    requires detail::KDDim<Dim>
auto KDTree<Dim>::hybrid_search(const Point& query, std::size_t k, float radius) const
    -> std::vector<Neighbor> {
    return impl_->hybrid_search(query, k, radius);
}

template <int Dim>
    requires detail::KDDim<Dim>
std::size_t KDTree<Dim>::delete_in_box(const Point& min_corner, const Point& max_corner) {
    return impl_->delete_in_box(min_corner, max_corner);
}

template <int Dim>
    requires detail::KDDim<Dim>
std::size_t KDTree<Dim>::delete_in_boxes(std::span<const BBox<Dim>> boxes) {
    return impl_->delete_in_boxes(boxes);
}

template <int Dim>
    requires detail::KDDim<Dim>
std::size_t KDTree<Dim>::delete_outside_radius(const Point& center, float r) {
    return impl_->delete_outside_radius(center, r);
}

template <int Dim>
    requires detail::KDDim<Dim>
std::size_t KDTree<Dim>::size() const noexcept {
    return impl_->size();
}

template <int Dim>
    requires detail::KDDim<Dim>
std::size_t KDTree<Dim>::capacity() const noexcept {
    return impl_->capacity();
}

template <int Dim>
    requires detail::KDDim<Dim>
void KDTree<Dim>::rebuild_all() {
    impl_->rebuild_all();
}

template class KDTree<2>;
template class KDTree<3>;
template class KDTree<4>;

} // namespace topiary
