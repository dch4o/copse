---
title: tkd-tree
description: Fixed-capacity kd-tree with FIFO eviction and resolution-based dedup for low-dimensional point streams.
language: C++20
status: WIP
---

# tkd-tree

A fixed-capacity kd-tree of `float` points in low dimensions (D ∈ {2, 3, 4}),
exposed as `topiary::KDTree<Dim>`. The library is named **topiary** after the
gardener's art of holding a plant at a bounded shape through repeated cutting:
inserts beyond capacity silently evict the oldest point (FIFO), and near-duplicate
inserts within a configurable `resolution` radius are rejected.

## Highlights

- Fixed capacity, single-writer, no internal locking.
- FIFO eviction when full; batched `insert` / `remove`.
- Resolution-based dedup against the live set.
- `knn_search`, `radius_search`, `hybrid_search` (kNN within radius).
- Scapegoat-style partial rebuild for amortized balance under streaming load.

## Usage

```cpp
#include "topiary/kd_tree.hpp"

topiary::KDTree3 tree({
    .capacity   = 10'000,
    .resolution = 0.05f,
});

tree.insert(points);
auto neighbors = tree.knn_search(query, /*k=*/5);
```

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

CMake options (all default `ON` except clang-tidy):
- `TOPIARY_BUILD_TESTS`
- `TOPIARY_BUILD_BENCHMARKS`
- `TOPIARY_ENABLE_CLANG_TIDY`

Requires C++20 and Eigen3.

## Status

WIP. API surface and on-disk layout may change.
