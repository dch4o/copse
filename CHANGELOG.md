# Changelog

All notable changes to copse are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project adheres
to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). While the major
version is `0`, minor-version bumps may carry breaking changes.

## [Unreleased]

## [0.1.0] - 2026-06-17

Initial release: a fixed-capacity, single-writer kd-tree of low-dimensional
`float` points (D ∈ {2, 3, 4}) for streaming spatial workloads.

### Added
- `KDTree<Dim>` with FIFO eviction at capacity and resolution-based dedup.
- Batched `insert` / `remove`; `knn_search`, `radius_search`, `hybrid_search`.
- Spatial deletes: `box_delete` (one or many boxes) and `radius_crop`.
- Scapegoat-style partial rebuild for amortized balance under streaming load.
- Plain POD point type `copse::Point<Dim>` — no external runtime dependencies.
- CMake install/export with `find_package(copse)` support (namespaced
  `copse::copse` target); consumable via `add_subdirectory` / FetchContent.
- MIT license; Catch2 microbenchmarks; 93-case Catch2 test suite.

[Unreleased]: https://github.com/dch4o/copse/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/dch4o/copse/releases/tag/v0.1.0
