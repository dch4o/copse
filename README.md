# copse

**A fast, dependency-free kd-tree for streaming spatial data** — SLAM local maps,
point clouds, and anything where points stream in, get queried, and age out.

[![Build & Test](https://github.com/dch4o/copse/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/dch4o/copse/actions/workflows/build-and-test.yml)
[![Sanitizers](https://github.com/dch4o/copse/actions/workflows/sanitizers.yml/badge.svg)](https://github.com/dch4o/copse/actions/workflows/sanitizers.yml)
[![Packaging](https://github.com/dch4o/copse/actions/workflows/packaging.yml/badge.svg)](https://github.com/dch4o/copse/actions/workflows/packaging.yml)
[![Coverage](https://github.com/dch4o/copse/actions/workflows/coverage.yml/badge.svg)](https://github.com/dch4o/copse/actions/workflows/coverage.yml)
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
- **Tested & sanitized.** The Catch2 suite runs in CI across GCC and Clang, with a
  dedicated AddressSanitizer / UndefinedBehaviorSanitizer pass on every push and a
  line-coverage gate that blocks merges below a floor.
- **Drop-in to consume.** Builds as a static *or* shared library; `find_package(copse)`,
  `add_subdirectory`, and FetchContent all work identically, and the shared
  library exports only its public API.

## Quick start

```cpp
#include "copse/kd_tree.hpp"

copse::KDTree3 tree({.capacity = 10'000, .resolution = 0.05f});

std::vector<copse::Point<3>> points = /* ... */;
tree.insert(points);

const copse::Point<3> query{1.0f, 2.0f, 3.0f};
auto nearest = tree.knn_search(query, /*k=*/5);                     // 5 nearest; each: .coord, .sq_dist
auto within  = tree.radius_search(query, /*radius=*/1.5f);          // every point within the radius
auto capped  = tree.hybrid_search(query, /*k=*/5, /*radius=*/1.5f); // up to 5 nearest within the radius

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
and Debian/Ubuntu `.deb` packages (runtime + dev) are attached to each
[release](https://github.com/dch4o/copse/releases).

## Build & test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

| CMake option | Default | Effect |
| --- | --- | --- |
| `COPSE_BUILD_TESTS` | `ON` | Build the Catch2 test suite |
| `COPSE_BUILD_BENCHMARKS` | `ON` | Build the benchmark targets |
| `COPSE_BUILD_DEMOS` | `OFF` | Build the Polyscope demos (needs GL/X11) |
| `COPSE_INSTALL` | `ON` | Generate install and package-config rules |
| `COPSE_ENABLE_CLANG_TIDY` | `OFF` | Run clang-tidy during the build |

## Requirements

C++20. Tested on Linux with GCC and Clang; Windows/MSVC is not supported.

## License

MIT — see [`LICENSE`](LICENSE). The distributed library contains no third-party
code; [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) lists the build- and
test-only dependencies.
