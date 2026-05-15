#include "pkd_tree/fixed_kd_tree.hpp"

#include "pkd_tree/impl/fixed_kd_tree_impl.hpp"

namespace pkd_tree {

template <int Dim>
    requires detail::KdDim<Dim>
FixedKdTree<Dim>::FixedKdTree(Config cfg)
    : impl_(std::make_unique<internal::FixedKdTreeImpl<Dim>>(std::move(cfg))) {}

template <int Dim>
    requires detail::KdDim<Dim>
FixedKdTree<Dim>::~FixedKdTree() = default;

template <int Dim>
    requires detail::KdDim<Dim>
FixedKdTree<Dim>::FixedKdTree(FixedKdTree&&) noexcept = default;

template <int Dim>
    requires detail::KdDim<Dim>
FixedKdTree<Dim>& FixedKdTree<Dim>::operator=(FixedKdTree&&) noexcept = default;

template <int Dim>
    requires detail::KdDim<Dim>
std::size_t FixedKdTree<Dim>::insert(std::span<const Point> points) {
    return impl_->insert(points);
}

template <int Dim>
    requires detail::KdDim<Dim>
std::size_t FixedKdTree<Dim>::remove(std::span<const Point> queries) {
    return impl_->remove(queries);
}

template <int Dim>
    requires detail::KdDim<Dim>
auto FixedKdTree<Dim>::knn_search(const Point& query, std::size_t k) const -> std::vector<Neighbor> {
    return impl_->knn_search(query, k);
}

template <int Dim>
    requires detail::KdDim<Dim>
auto FixedKdTree<Dim>::radius_search(const Point& query, float radius) const -> std::vector<Neighbor> {
    return impl_->radius_search(query, radius);
}

template <int Dim>
    requires detail::KdDim<Dim>
auto FixedKdTree<Dim>::hybrid_search(const Point& query, std::size_t k, float radius) const
    -> std::vector<Neighbor> {
    return impl_->hybrid_search(query, k, radius);
}

template <int Dim>
    requires detail::KdDim<Dim>
std::size_t FixedKdTree<Dim>::size() const noexcept {
    return impl_->size();
}

template <int Dim>
    requires detail::KdDim<Dim>
std::size_t FixedKdTree<Dim>::capacity() const noexcept {
    return impl_->capacity();
}

template <int Dim>
    requires detail::KdDim<Dim>
void FixedKdTree<Dim>::rebuild_all() {
    impl_->rebuild_all();
}

template class FixedKdTree<2>;
template class FixedKdTree<3>;
template class FixedKdTree<4>;

} // namespace pkd_tree
