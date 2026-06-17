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
- Spatial bulk deletes: `box_delete` (axis-aligned boxes) and `radius_crop` (keep only a local ball).
- Scapegoat-style partial rebuild for amortized balance under streaming load.
- Plain POD point type (`copse::Point<Dim>`); no runtime dependencies.

## Usage

```cpp
#include "copse/kd_tree.hpp"

copse::KDTree3 tree({
    .capacity   = 10'000,
    .resolution = 0.05f,
});

std::vector<copse::Point<3>> points = /* ... */;
tree.insert(points);

const copse::Point<3> query{1.0f, 2.0f, 3.0f};
auto neighbors = tree.knn_search(query, /*k=*/5);   // each: .coord, .sq_dist

tree.box_delete({copse::BBox<3>{{0, 0, 0}, {1, 1, 1}}}); // clear an axis-aligned box
tree.radius_crop(query, /*r=*/2.0f);                     // keep only points within r
```

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

CMake options:
- `COPSE_BUILD_TESTS` — default `ON`
- `COPSE_BUILD_BENCHMARKS` — default `ON`
- `COPSE_BUILD_DEMOS` — default `OFF` (pulls Polyscope; needs GL/X11)
- `COPSE_ENABLE_CLANG_TIDY` — default `OFF`

## Install & consume

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCOPSE_BUILD_TESTS=OFF -DCOPSE_BUILD_BENCHMARKS=OFF
cmake --build build
cmake --install build --prefix /your/prefix     # add -DBUILD_SHARED_LIBS=ON for a .so
```

Then, from a downstream CMake project:

```cmake
find_package(copse REQUIRED)
target_link_libraries(your_app PRIVATE copse::copse)
```

copse ships both static and shared libraries, has no runtime dependencies, and
propagates its C++20 requirement to consumers. It also works via
`add_subdirectory()` / `FetchContent` — the `copse::copse` target is the same
in-tree.

## Requirements

C++20. Tested on Linux with GCC and Clang; Windows/MSVC is not currently
supported.

## License

MIT — see [`LICENSE`](LICENSE). The distributed library contains no third-party
code; [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) lists the build-, test-,
and benchmark-only dependencies.
