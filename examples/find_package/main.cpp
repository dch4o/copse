// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT

// Minimal downstream consumer of an installed copse. Sets no C++ standard and
// links no dependencies of its own — copse propagates its C++20 requirement and
// has none. Doubles as the CI install/consume smoke test.
#include "copse/kd_tree.hpp"

#include <cstdio>
#include <vector>

int main() {
    copse::KDTree3 tree({.capacity = 100, .resolution = 0.01f});

    const std::vector<copse::Point<3>> points{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    tree.insert(points);

    const auto hits = tree.knn_search(copse::Point<3>{1, 2, 3}, /*k=*/1);
    std::printf("size=%zu nearest_sq_dist=%g\n", tree.size(), hits.front().sq_dist);

    return (tree.size() == 3 && hits.size() == 1 && hits.front().sq_dist == 0.0f) ? 0 : 1;
}
