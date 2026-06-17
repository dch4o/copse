# copse

**A fast, dependency-free kd-tree for streaming spatial data** — SLAM local maps,
point clouds, and anything where points stream in, get queried, and age out.

[![Build & Test](https://github.com/dch4o/copse/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/dch4o/copse/actions/workflows/build-and-test.yml)
[![Sanitizers](https://github.com/dch4o/copse/actions/workflows/sanitizers.yml/badge.svg)](https://github.com/dch4o/copse/actions/workflows/sanitizers.yml)
[![Packaging](https://github.com/dch4o/copse/actions/workflows/packaging.yml/badge.svg)](https://github.com/dch4o/copse/actions/workflows/packaging.yml)
[![Release](https://img.shields.io/github/v/release/dch4o/copse?include_prereleases&sort=semver)](https://github.com/dch4o/copse/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

`copse` keeps a **bounded** set of low-dimensional `float` points (D ∈ {2, 3, 4})
balanced under a continuous insert / query / delete load. It is named after the
small coppiced woodland kept in shape by cutting it back as it grows: the tree
evicts the oldest point when full (FIFO), rejects near-duplicates within a
`resolution` radius, and prunes itself back into balance with a scapegoat-style
partial rebuild.

## Why copse

- **Zero dependencies.** Just C++20 — no Eigen, no Boost. Points are a plain POD
  (`copse::Point<Dim>`), so the binary ABI does not shift with compiler flags.
- **Built for streaming.** Fixed capacity + FIFO eviction + resolution dedup hold
  memory flat under an unbounded stream; the partial rebuild amortizes balance.
- **Fast & lean.** Benchmarked against HKU MARS **ikd-Tree**, copse leads
  single-threaded on insert, kNN, radius search, and batched spatial delete — with
  a ~2.8× smaller per-point footprint. [See the comparison →](docs/benchmark/vs_ikd_tree.md)
- **Drop-in to consume.** Ships static *and* shared libraries; `find_package(copse)`,
  `add_subdirectory`, and FetchContent all work identically, and the shared
  library exports only its public API.

## Quick start

```cpp
#include "copse/kd_tree.hpp"

copse::KDTree3 tree({.capacity = 10'000, .resolution = 0.05f});

std::vector<copse::Point<3>> points = /* ... */;
tree.insert(points);

const copse::Point<3> query{1.0f, 2.0f, 3.0f};
auto neighbors = tree.knn_search(query, /*k=*/5);   // each: .coord, .sq_dist

tree.box_delete({copse::BBox<3>{{0, 0, 0}, {1, 1, 1}}}); // clear an axis-aligned box
tree.radius_crop(query, /*r=*/2.0f);                     // keep only points within r
```

## Features

- Fixed capacity, single-writer (not internally synchronized).
- FIFO eviction when full; batched `insert` / `remove`.
- Resolution-based dedup against the live set.
- `knn_search`, `radius_search`, `hybrid_search` (kNN within a radius).
- Spatial bulk deletes: `box_delete` (one or many boxes) and `radius_crop`.
- Scapegoat-style partial rebuild for amortized balance under streaming load.

## Install & consume

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCOPSE_BUILD_TESTS=OFF -DCOPSE_BUILD_BENCHMARKS=OFF
cmake --build build
cmake --install build --prefix /your/prefix     # add -DBUILD_SHARED_LIBS=ON for a .so
```

From a downstream CMake project:

```cmake
find_package(copse REQUIRED)
target_link_libraries(your_app PRIVATE copse::copse)
```

copse has no runtime dependencies and propagates its C++20 requirement to
consumers. A complete consumer is in [`examples/find_package/`](examples/find_package/),
and prebuilt Linux binaries are attached to each
[release](https://github.com/dch4o/copse/releases).

## Build & test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

CMake options: `COPSE_BUILD_TESTS` (ON), `COPSE_BUILD_BENCHMARKS` (ON),
`COPSE_BUILD_DEMOS` (OFF — pulls Polyscope), `COPSE_ENABLE_CLANG_TIDY` (OFF).

## Benchmarks

Per-operation reports live in [`docs/benchmark/`](docs/benchmark/):
[insert](docs/benchmark/insert.md) · [search](docs/benchmark/search.md) ·
[rebuild](docs/benchmark/rebuild.md) · [remove](docs/benchmark/remove.md) ·
[vs. ikd-Tree](docs/benchmark/vs_ikd_tree.md).

## Requirements

C++20. Tested on Linux with GCC and Clang; Windows/MSVC is not supported.

## License

MIT — see [`LICENSE`](LICENSE). The distributed library contains no third-party
code; [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) lists the build- and
test-only dependencies.
