# copse

A fixed-capacity kd-tree of `float` points in low dimensions (D ∈ {2, 3, 4}),
exposed as `copse::KDTree<Dim>`. The library is named **copse** after the small
coppiced woodland that is held within bounds by cutting it back as it grows:
inserts beyond capacity silently evict the oldest point (FIFO), near-duplicate
inserts within a configurable `resolution` radius are rejected, and a
scapegoat-style partial rebuild periodically prunes the tree back into balance.

## Highlights

- Fixed capacity, single-writer, no internal locking.
- FIFO eviction when full; batched `insert` / `remove`.
- Resolution-based dedup against the live set.
- `knn_search`, `radius_search`, `hybrid_search` (kNN within radius).
- Scapegoat-style partial rebuild for amortized balance under streaming load.

## Usage

```cpp
#include "copse/kd_tree.hpp"

copse::KDTree3 tree({
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
- `COPSE_BUILD_TESTS`
- `COPSE_BUILD_BENCHMARKS`
- `COPSE_ENABLE_CLANG_TIDY`

Requires C++20 and Eigen3.

## Status

WIP. API surface and on-disk layout may change.
