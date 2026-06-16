#pragma once

#include <cstddef>

namespace copse::detail {

/// @brief Compile-time guard: D in {2, 3, 4}.
/// @tparam Dim Point dimensionality.
template <int Dim>
concept SupportedDim = (Dim == 2 || Dim == 3 || Dim == 4);

/// @brief Default leaf bucket size: 5 for all supported D (tuned against benchmarks).
/// @tparam Dim Point dimensionality.
template <int Dim>
    requires(Dim == 2 || Dim == 3 || Dim == 4)
inline constexpr std::size_t default_leaf_bucket_size_v = 5;

} // namespace copse::detail
