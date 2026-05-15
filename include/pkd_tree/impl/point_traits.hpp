#pragma once

#include <Eigen/Core>

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace pkd_tree::detail {

/// @brief Compile-time guard: D in {2, 3, 4}.
template <int Dim>
concept KdDim = (Dim == 2 || Dim == 3 || Dim == 4);

/// @brief Canonical Eigen column-vector point type for a given D (float-only).
template <int Dim>
using PointType = Eigen::Matrix<float, Dim, 1>;

/// @brief Static check that `P` is exactly the canonical PointType for D; rejects silent conversions.
template <typename P, int Dim>
concept SamePointAsEigen = std::same_as<std::remove_cvref_t<P>, PointType<Dim>>;

/// @brief Default leaf bucket size: 5 for all supported D (tuned against benchmarks).
template <int Dim>
    requires(Dim == 2 || Dim == 3 || Dim == 4)
inline constexpr std::size_t default_leaf_bucket_size_v = 5;

} // namespace pkd_tree::detail
