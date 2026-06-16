# Fixed-size kd-tree (`topiary`)

> Design date: 2026-05-05 · Last revised: 2026-05-14 (rev16) · Slug: fixed-size-kdtree
> Companion: `docs/research/fixed_size_kdtree.md`

This document captures the design for a fixed-capacity, mutable kd-tree with FIFO eviction, resolution-based dedup, and batched mutation.

## Goal

A header-templated, single-writer C++ library providing a fixed-capacity kd-tree of low-dimensional `float` points (D ∈ {2,3,4}) that supports:

- Batched `insert` with resolution-based dedup against the existing live set.
- Batched `remove` (every live point within `resolution` of each query).
- Single-query `knn_search`, `radius_search`, `hybrid_search` (kNN within radius).
- Strict FIFO eviction when full.
- Throughput targets: 10k–100k inserts/sec, 10k–100k searches/sec, ~100 ms inter-batch budget.

## Non-goals

- Approximate NN, GPU, on-disk variants.
- Concurrent readers (the tree is single-writer with **no internal locking**; concurrent reader support is a future extension hook, not a present feature).
- ikd-tree implementation patterns (per user instruction; ideas may be borrowed but no code shape).
- A general-purpose container interface (no STL allocator awareness, no iterators over the tree itself; the API is operation-centric).
- Persistent point identity. The use case is streaming/query: callers insert points and read back coordinates from queries. Result rows carry coords, not handles; users requiring persistent point identity should layer that themselves.
- Scalar-type genericity. The tree is hardcoded to `float`; `double` is not offered.

## Public API

The library lives under namespace `topiary`. Header layout is shown in [File layout](#file-layout).

### Core type

```cpp
namespace topiary {

// D restricted to {2,3,4} via concept.
// SupportedDim and PointType live under topiary::detail; users reach the canonical
// point type through KDTree<Dim>::Point and never need to spell either name.
template <int Dim>
    requires detail::SupportedDim<Dim>
class KDTree {
public:
    using Point = detail::PointType<Dim>;      // Eigen column-vector of float as the canonical point.

    // Construction-time configuration; nested member of KDTree.
    // The only spelling for this type is KDTree<Dim>::Config — there is no
    // namespace-scope alias.
    struct Config {
        std::size_t capacity                   = 0;     // required > 0
        float       resolution                 = 0.0f;  // required > 0
        std::size_t leaf_bucket_size           = detail::default_leaf_bucket_size_v<Dim>; // 5 for all D
        float       alpha                      = 0.7f;  // 0.5 < α < 1
        float       tombstone_threshold        = 0.25f; // 0 ≤ x ≤ 1
    };

    struct Neighbor {
        Point coord;
        float sq_dist; // squared by contract; caller takes sqrt if needed
    };

    explicit KDTree(Config cfg);

    // --- Mutation (batched, single-writer) ---

    // Number of points actually inserted (i.e., not rejected by the resolution dedup pass and
    // not lost to intra-batch first-seen-wins). FIFO-evicted prior occupants are still counted
    // as inserted. No further telemetry is exposed; callers wanting per-input outcomes
    // can layer that on top via a future opt-in out-span.
    std::size_t insert(std::span<const Point> points);

    // Total number of live points removed across all queries in this batch.
    std::size_t remove(std::span<const Point> queries);

    // --- Query (single-point) ---

    // Returned neighbors are sorted by ascending sq_dist.
    std::vector<Neighbor> knn_search   (const Point& query, std::size_t k) const;
    std::vector<Neighbor> radius_search(const Point& query, float radius)  const;
    std::vector<Neighbor> hybrid_search(const Point& query, std::size_t k, float radius) const;

    // --- Introspection ---

    std::size_t size()     const noexcept; // live count
    std::size_t capacity() const noexcept;

    // --- Spatial deletes (uses per-leaf AABB infrastructure from §Tree topology) ---

    // Release every live point inside the axis-aligned `box`.
    std::size_t delete_box(const BBox<Dim>& box);
    // Release every live point inside any of `boxes` (single end-of-batch rebuild).
    std::size_t delete_boxes(std::span<const BBox<Dim>> boxes);
    // Release every live point strictly outside the sphere of radius `r` around `center`.
    std::size_t delete_outside_radius(const Point& center, float r);

    // --- Maintenance (manual escape hatch) ---

    void rebuild_all(); // forces a from-scratch rebuild; intended for tests, benchmarks, manual tail control.
};

} // namespace topiary
```

The use case is streaming/query — callers insert points and read back the coordinates of nearest matches. Result rows carry coords, not opaque references. Persistent point identity is the caller's concern; the tree exposes no `Handle`, no `is_valid`, no `point_of`. Internally each live point has an integer index used by the topology, but the index never escapes the API.

### Aliases

```cpp
namespace topiary {
using KDTree2 = KDTree<2>;
using KDTree3 = KDTree<3>;
using KDTree4 = KDTree<4>;
}
```

### Mutation result shape

`insert` and `remove` each return `std::size_t` — the count of points written / cleared. No per-input telemetry, no aggregate counters.

A future opt-in caller-provided out-span is the cleanest extension point if per-input outcomes are ever needed; not added in v1.

### Error model

- **Preconditions** (e.g. capacity == 0, k == 0, NaN coordinates): throw `std::invalid_argument` or a `topiary::PreconditionError` derived from it.
- **Operational results** (duplicate rejection, no-match remove, empty-tree query): communicated through return values; never thrown.
- **Capacity overflow** is not an error — it is silent FIFO eviction. Evictions are not reported back to the caller.

Exceptions are used for precondition breaches; operational outcomes flow through return values. The throw surface is small if the project later prefers `std::expected` or status codes.

## Design

### Storage layout (research F7)

Three contiguous arrays, all sized exactly to `capacity` at construction:

1. **Point store** — `std::vector<Point>` aligned to `Eigen` requirements.
2. **Liveness bitset** — `std::vector<std::uint8_t>` (one byte per index for cheap atomic-free updates; could be packed later).
3. **Generation counters** — `std::vector<std::uint32_t> generations_` (one 32-bit counter per index). `PointStore::acquire` bumps `generations_[i]` on EVERY reuse of slot `i`, whether the reuse comes from an explicit `release` followed by re-acquire OR from silent FIFO eviction of the slot's prior occupant. Read access via `PointStore::generation(idx) -> std::uint32_t`. Bucket entries stamp the current `gen` at push time; subsequent queries skip any entry whose stamp no longer matches.

A **FIFO buffer** is maintained as a separate free-list / write cursor over the index values: a `std::vector<std::uint32_t>` recording the order in which indices were filled, plus a head/tail index pair. When the tree is full, the next insert evicts the index at `head`, advances `head`, and reuses that index for the new point.

### Tree topology (research F7, F8)

A **node pool** of `TreeNode` records lives in a `std::vector<TreeNode>` indexed by 32-bit integers. Nodes are bucketed leaves:

```cpp
// internal — include/topiary/impl/tree_node.hpp
struct TreeNode {
    bool          is_leaf;
    std::uint32_t subtree_live_count;   // for scapegoat α-balance check
    std::uint32_t subtree_total_count;  // includes tombstoned indices
    union {
        struct {                          // unnamed internal payload
            std::uint8_t  split_dim;
            float         split_value;
            std::uint32_t left;
            std::uint32_t right;
        };
        struct {                          // unnamed leaf payload
            std::uint32_t bucket_offset;
            std::uint16_t bucket_size;
            std::uint16_t bucket_capacity;
            std::uint32_t leaf_bbox_idx;  // index into per-tree std::vector<BBox<Dim>>
        };
    };
    std::uint32_t reserved_;              // pad to 32 B
};
static_assert(sizeof(TreeNode) == 32, "TreeNode must stay 32 B for cache density");
```

Callers access the active arm directly: `node.split_dim` / `node.bucket_offset`, etc. The anonymous inner structs are a widely supported GCC/Clang extension and `-Wpedantic` is suppressed locally at the type's definition site.

Bucket storage is a single flat `std::vector<BucketEntry> data_` (in `LeafBucket`, renamed from `BucketPool`) referenced by `(offset, size, capacity)` per leaf, where:

```cpp
// internal — include/topiary/impl/leaf_bucket.hpp
struct BucketEntry {
    std::uint32_t index;
    std::uint32_t gen;   // stamp from PointStore::generation(index) at push time
};
static_assert(sizeof(BucketEntry) == 8, "BucketEntry must stay 8 B");
```

Every leaf scan first checks `gen` against `PointStore::generation(entry.index)`; on mismatch the entry is treated as stale and skipped. Flat-pool is chosen for cache-friendliness during scan over the per-leaf small-vector alternative; the 8-byte entry width keeps the total bucket footprint at ~16N for cap=2B leaves.

Leaves are allocated with `cap = 2 * leaf_bucket_size` so an incremental insert always has headroom. If a same-batch insert fills a leaf to exactly `cap`, `insert_index` proactively splits the leaf in place (so a follow-up same-batch insert routing here lands in one of the new children); the end-of-batch `maybe_partial_rebuild` sweep also catches any leaf with `bucket_size > 2 * leaf_bucket_size` as a backstop. `LeafBucket::data_.size()` stays bounded by (leaf count × 2B) ≈ 2N. There is no `compact`/`shrink` API and no tail-reallocate-and-leak path.

**Hybrid AABB caching for spatial pruning.** Internal nodes carry no AABB; the partition AABB is *derived* during descent from the chain of split planes (passed in the recursion frame). Each leaf stores a 32-bit `leaf_bbox_idx` in the Leaf union (no node-size growth thanks to union slack — the Internal arm is the size-determining one) into a per-tree `std::vector<BBox<Dim>>` of tight data extents. Internal-level pruning uses the (loose) partition AABB; leaf-level pruning uses the (tight) stored leaf `BBox<Dim>` to catch cases where the partition straddles the query but the actual data is disjoint or contained. ~24B per leaf ≈ 1.5B/point amortized at `B≥16`. Maintenance: `rebuild` populates leaf BBoxes from bucket points; incremental `insert_index` expands the touched leaf's BBox along the path; tombstones leave the BBox as a conservative upper bound, refreshed on next partial rebuild.

A whole-tree `root_bbox_` member of the same `BBox<Dim>` type lives on `KDTreeImpl`. It is populated by `rebuild` (full extent of the live set) and expanded by `insert_index` (cwiseMin/cwiseMax with each inserted point). `tombstone_index` does NOT shrink it (conservative upper bound; refreshed on the next full `rebuild`). The read site for `root_bbox_` is the spatial-deletes descent, which lands in a later slice.

### Mutation strategies (research F2)

- **Insert** (single-pass over the input batch — no separate intra-batch accepted set; tree-side dedup alone covers both regimes):

  For each candidate `p` in input order:

  1. **Dedup.** Call `kernel_.any_within(root_, p, sq_res)` — an allocation-free early-exit predicate. On hit → `continue`. Because each accepted candidate has already been incrementally inserted into the tree (step 3 below), prior-batch and earlier-in-this-batch points are both visible to the same query — no separate `accepted` vector is needed.
  2. **Acquire index.** `points_.acquire(p)` — silently FIFO-evicts the oldest live index if the store is full. Bumps `generations_[index]` on the chosen slot (and additionally on the evicted slot when the store was full), invalidating any stale bucket entries that still reference the slot.
  3. **Tree topology insert.** If `root_` is INVALID (empty tree), seed via a single-index `builder_.rebuild`; otherwise call `builder_.insert_index(root_, index)`. `insert_index` descends by split planes to the target leaf, appends `BucketEntry{index, points_.generation(index)}` via `LeafBucket::push`, expands the leaf's `BBox<Dim>` and `root_bbox_`, and bumps `subtree_*_count` on every node along the descent path. If the leaf reaches its 2B cap on this insert, `insert_index` proactively splits the leaf in place so a same-batch follow-up insert routing here lands in one of the new children.

  After the entire batch, `maybe_partial_rebuild_full(root_)` runs a recursive top-down sweep over the tree, rebuilding every subtree that fails the unbalanced (`max_child/total > α`) or tombstoned (`(total-live)/total ≥ tombstone_threshold`) trigger, plus any leaf whose `bucket_size > 2 * leaf_bucket_size`. After a non-violator node's children are processed, the node is re-checked once because a child rebuild may have shifted its left/right ratio. Returns the count of accepted candidates.

- **Remove**:
  1. For each query, run a radius search at `resolution` and collect matches.
  2. For each match: call `builder_.tombstone_index(root_, idx)` **first** (it reads `points_.point(idx)` to descend), then `points_.release(idx)`. The call order is load-bearing because the coord must still be valid during descent. `tombstone_index` decrements `subtree_live_count` along the descent path and stops at the leaf — it does NOT scrub the bucket entry. The stale `(index, gen)` pair is invalidated automatically the next time `acquire` reuses the slot and bumps its gen; until then, leaf scans skip the entry via the gen guard (the `is_live` check stays for the released-but-not-yet-reacquired sub-window).
  3. After the batch, call `maybe_partial_rebuild(root_)` once to apply the scapegoat + tombstone-fraction trigger; the rebuilt subtree physically drops every stale entry.

- **Spatial deletes** (`delete_box`, `delete_outside_radius`):
  1. Recursive descent carrying the partition AABB in the stack frame.
  2. At each node, classify partition vs query: DISJOINT → skip; CONTAINED → release every live index in the subtree; STRADDLE → recurse children with bbox adjusted by the split plane.
  3. At leaves, classify the stored tight leaf AABB vs query before falling back to per-point checks for the residual STRADDLE case.
  4. After the batch, run the scapegoat + tombstone-fraction trigger once.

- **Rebuild trigger** (after each batch only, never per-point) — two entry points over one node-level violator predicate (`max(left_total, right_total) > α · subtree_total` **or** `tombstoned/total ≥ tombstone_threshold` **or**, for a leaf, `bucket_size > 2 * leaf_bucket_size`):
  - **Batch insert** → `maybe_partial_rebuild_full(root_)`: a recursive top-down sweep over the whole tree. A batch insert touches a broad swath, so visiting every node once beats recording every point-path and scoping.
  - **Sparse deletes** (`delete_box`, `delete_outside_radius`, coord `remove`) → `maybe_partial_rebuild(root_)`: the mutating descent records each touched subtree node into `modified_nodes_` (one entry per node, not per point), and the sweep descends only into recorded children (sorted-unique, `binary_search` membership). After any sweep no violator remains, so an untouched subtree cannot have gained one — the scoped walk is exact and skips the bulk of the tree for a region delete. A size guard falls back to the full sweep when the recorded count approaches the node count (a delete hitting most of the tree).
  - On a hit, the subtree is rebuilt from its live points using median-of-max-spread split (research F9); the new subroot is written **in place** to the scapegoat's existing node-pool slot — the parent's `internal.left/right` pointer already points there, so no reparenting. New descendants append at the tail of `nodes_`; old descendants leak (bounded by N; reclaimed on the next full `rebuild_all`). On a miss, recurse into the (recorded) children then re-check self, because a child rebuild may have shifted the ratio.

Split selection uses median-of-max-spread, computed via `std::nth_element`. Sliding-midpoint is deferred.

### Search kernel (research F10)

A single internal traversal kernel parameterized by `(initial_squared_pruning_radius, k)`:

- `knn_search(q, k)` — `initial_radius² = +∞`.
- `radius_search(q, r)` — `initial_radius² = r²`, `k = SIZE_MAX`.
- `hybrid_search(q, k, r)` — bounded by both.

All distance comparisons are squared throughout. The result heap is a bounded max-heap of size ≤ k keyed on squared distance. On return, the heap is sorted ascending and copied into the user's `std::vector<Neighbor>` with each entry carrying the live point's coord.

Descent prunes by **incremental box-distance**: the traversal carries, per axis, the squared minimum distance from the query to the current cell on that axis (`min_sq_axis_dist`) and their running sum `min_sq_dist` (the squared distance from the query to the subtree's bounding box). The far child is visited only when `min_sq_dist < worst_sq_dist`. This is tighter than a single split-plane gap, which under-prunes once ancestor splits have pushed the query off the cell on other axes — the difference is decisive for `radius_search` (large `k = SIZE_MAX`, fixed `worst = r²`), where a plane-only test degenerates to a near-full scan.

Leaf scan iterates `[bucket_offset, bucket_offset + bucket_size)` as `BucketEntry` records. For each entry it checks `points_.generation(entry.index) == entry.gen` first (skip stale), then `points_.is_live(entry.index)` (skip released-but-not-yet-reacquired), then the squared-distance test.

A separate `SearchKernel::any_within(root, query, sq_radius)` early-exit predicate sits alongside `search` and `collect_indices_within`; it returns `bool` and allocates nothing. The insert-dedup hot path is its sole caller in v1.

### Threading and concurrency

- **Single writer, no internal locking.** All public mutators are non-`const`; all queries are `const`. The user is responsible for serializing access.
- Rebuilds run inline on the writer thread. An off-thread rebuild + atomic swap is sketched in [Alternatives considered](#alternatives-considered) and explicitly out of scope for v1.

### Header documentation convention

All declarations in public headers under `include/topiary/` and internal headers under `include/topiary/impl/` follow the project's docstring rule:

- `///` line comments only — never `/** ... */`.
- Never `///<`. Single-line docstrings on a variable/field/alias declaration go on the same line, to the right (`std::size_t capacity; /// Max points.`); multi-line docstrings and docstrings on functions/classes/structs go on their own `///` lines above the declaration.
- Document every function fully: `@brief`, a `@param` for each parameter, `@return` for every non-void return, `@tparam` for each template parameter, and `@throws` where it applies. Do not skip "obvious" parameters — completeness over brevity. Use `@copydoc` to inherit a public method's full doc on its impl mirror.
- Allowed tags: `@brief`, `@param`, `@return`, `@tparam`, `@throws`. No `@invariant`/`@note`/`@pre`/`@post`, and no per-mutator `@warning` when the class-level brief already states the threading model.
- Sources (`.cpp`) keep their current sparse-comment style.
- Skeleton bodies are `// TODO: <one-line intent>`. **Carve-out:** mechanical PIMPL forwarders that contain no logic beyond a single forward to the impl member may be written out in the skeleton; any body that contains logic beyond a forward stays `// TODO:`.
- No `Doxyfile` and no doc-generation infrastructure are added; only the comment style is adopted.

### Invariants (post-condition of every public mutator)

1. `live_count <= capacity`.
2. No two live points within `resolution` of each other (modulo FP edge cases at exactly `resolution`; not asserted as a hard invariant per research F11).
3. Every tree leaf bucket may contain stale `(index, gen)` pairs whose `gen` no longer matches `PointStore::generation(index)`. Queries skip them via the per-leaf generation guard. Stale entries are physically removed only on the next subtree rebuild that covers that leaf. Liveness alone is no longer sufficient to validate a bucket entry — `gen` match is the load-bearing check.

## File layout

### Public headers — `include/topiary/`

- `kd_tree.hpp` — `KDTree<Dim>` declaration (with nested `Config` and `Neighbor`); primary entry point for users. Pulls in `impl/point_traits.hpp` transitively because the template signature and `Point` alias name those types; the names themselves remain under `topiary::detail` and are not part of the public surface.
- `topiary.hpp` — umbrella header that includes `kd_tree.hpp`.

### Internal headers — `include/topiary/impl/`

These are **not part of the public API** and live under `impl/` so that any future install rule can drop them with a single exclusion. They are placed under `include/` (not `src/`) so the `KDTreeImpl` PIMPL template can be reached by `kd_tree.cpp` via the same include style as public headers, and so callers building from a source checkout get consistent include paths.

> **Install note (TODO when an install target is added):** the install rule must exclude `include/topiary/impl/`. Suggested form:
> ```cmake
> install(DIRECTORY include/topiary/ DESTINATION include/topiary
>         PATTERN "impl" EXCLUDE)
> ```

- `point_traits.hpp` — minimal concept / trait machinery (`SupportedDim`, `PointType`, `SamePointAsEigen`, `default_leaf_bucket_size_v`), in `namespace topiary::detail`. The canonical point type is exposed publicly only as `KDTree<Dim>::Point`.
- `tree_node.hpp` — internal `TreeNode`, the `BBox<Dim>` AABB struct used by leaves and the root extent, and node-pool typedefs. Pulls in `point_traits.hpp` for `detail::PointType<Dim>`.
- `point_store.hpp` — point + liveness arrays, FIFO buffer (declarations).
- `leaf_bucket.hpp` — flat bucket-index storage with per-leaf `(offset, size, capacity)` slices; fixed-cap leaves (`cap = 2B`), no compaction.
- `tree_builder.hpp` — from-scratch and partial subtree rebuild (median-of-max-spread); partial rebuild reuses the scapegoat's node-pool slot in place.
- `search_kernel.hpp` — the unified squared-distance traversal kernel.
- `kd_tree_impl.hpp` — the PIMPL template, owns the storage and orchestrates calls to the helpers. Intra-batch dedup is inlined here as part of the single-pass insert loop; there is no standalone dedup helper.

### Sources — `src/`

`src/` holds only `.cpp` files (no headers). Definitions for both public and internal templates live here, with explicit instantiations for the three default aliases.

- `kd_tree.cpp` — out-of-line definitions for `KDTree<Dim>` + explicit instantiations.
- `kd_tree_impl.cpp` — definitions + explicit instantiations for `KDTreeImpl<Dim>`.
- `point_store.cpp`, `leaf_bucket.cpp`, `tree_builder.cpp`, `search_kernel.cpp` — definitions for the helpers.

### Tests — `tests/` (Catch2 v3)

Tests are organized **one translation unit per testable class/struct/free function** in the library. The split mirrors the public and internal headers:

- `CMakeLists.txt` — registers Catch2 test executables; uses `catch_discover_tests` for ctest integration.
- `test_kd_tree.cpp` — public `KDTree<Dim>` facade: insert/remove/search/rebuild_all + end-to-end invariants. Absorbs what was previously `test_kd_tree_impl.cpp` because the impl orchestration is fully exercisable through the facade.
- `test_point_store.cpp` — `PointStore<Dim>`: acquire/release, FIFO buffer removal-by-index, iteration.
- `test_leaf_bucket.cpp` — `LeafBucket`: allocate, view, push.
- `test_tree_node.cpp` — `TreeNode`: default state, sentinel, internal/leaf union round-trips.
- `test_tree_builder.cpp` — `TreeBuilder<Dim>`: from-scratch rebuild, partial rebuild, insert_index, tombstone_index, spatial deletes (delete_box / delete_outside_radius).
- `test_search_kernel.cpp` — `SearchKernel<Dim>`: traversal kernel, k_max/radius bounds, oracle agreement, liveness skipping.

Intra-batch dedup is exercised through `test_kd_tree.cpp`'s insert tests; there is no standalone `test_dedup.cpp` because the inline check is no longer a standalone function.

Internal tests `#include "topiary/impl/...hpp"` directly; the impl headers ride on `topiary`'s public include path so no extra plumbing is needed.

### Benchmarks — `benchmarks/` (Catch2 v3 microbench)

- `CMakeLists.txt` — registers Catch2 benchmark executables.
- `bench_insert.cpp`, `bench_search.cpp`, `bench_mixed.cpp`, `bench_rebuild.cpp` — `TEST_CASE` + `BENCHMARK` blocks. Tagged `[!benchmark]` so they don't run by default; invoke with `--benchmark-samples N` etc.

### Top-level

- `CMakeLists.txt` — project setup, target wiring, options.
- `cmake/Dependencies.cmake` — `find_package(Eigen3)`, then `find_package(Catch2 REQUIRED)` (module mode) when tests or benchmarks are enabled.
- `cmake/FindCatch2.cmake` — find-module shim. Tries `find_package(Catch2 3 CONFIG QUIET)` first so a system Catch2Config.cmake wins (CONFIG mode does not recurse into this find-module); falls back to `FetchContent_MakeAvailable(Catch2)`. After resolution it `include(Catch)` for `catch_discover_tests`.
- `cmake/ClangTidy.cmake` — `TOPIARY_ENABLE_CLANG_TIDY` option; helper `topiary_enable_clang_tidy_on(<target>...)`. Wires `CXX_CLANG_TIDY` per first-party target so clang-tidy runs only on translation units that actually compile (incremental builds re-tidy only changed TUs); FetchContent'd Catch2 is excluded by being scoped per-target rather than via `CMAKE_CXX_CLANG_TIDY`.
- `Dockerfile` — multi-stage, `docker buildx`-compatible. Stage `dev` (full toolchain + Eigen + clang-tidy + clang-format on `ubuntu:22.04`) is fleshed out; stages `build` and `runtime` are stubs with TODO comments.
- `.clang-format` — formatting rules (existing).
- `.clang-tidy` — broad checks (`bugprone-*`, `cppcoreguidelines-*`, `modernize-*`, `performance-*`, `portability-*`, `readability-*`) with carve-outs for low-signal noise (magic numbers, easily-swappable parameters, identifier length, union access required by the tagged `TreeNode`). `HeaderFilterRegex` restricts diagnostics to `include/topiary/` and `src/`.
- `.gitignore` — build/IDE noise.

## Dependencies

- **Eigen** ≥ 5.0 (header-only) — point and small-vector math. Resolved via `find_package(Eigen3 5.0 REQUIRED NO_MODULE)`.
- **Catch2** v3.5.4 — used for both unit tests and benchmarks. Resolved via `cmake/FindCatch2.cmake`, which prefers a system-installed Catch2 (CONFIG mode) and falls back to `FetchContent` so the build is self-contained.
- **C++20** standard (`std::span`, concepts, designated initializers, `<bit>`).

## Build and test

The project's expected generator is **Ninja**. The Dockerfile's `dev` stage installs `ninja-build`.

- `cmake -S . -B build -G Ninja` — configure.
- `cmake --build build` — build the library, tests, and benchmarks.
- `ctest --test-dir build` — run the test suite (Catch2 cases registered via `catch_discover_tests`).
- `./build/benchmarks/bench_insert "[!benchmark]"` etc. runs the benchmarks (Catch2 skips `[!benchmark]`-tagged cases unless explicitly selected).

Options:

- `-DTOPIARY_BUILD_TESTS=OFF` / `-DTOPIARY_BUILD_BENCHMARKS=OFF` to opt out.
- `-DTOPIARY_ENABLE_CLANG_TIDY=ON` to run clang-tidy as part of every TU compile of first-party targets. The wiring is per-target via `set_target_properties(... CXX_CLANG_TIDY ...)` so FetchContent'd Catch2 sources are not linted; incremental builds only re-tidy translation units that actually recompile.

## Risks

- **Tail latency from worst-case partial rebuild** (research F12). v1 mitigation: smaller α or `rebuild_all` on caller-chosen quiet windows.
- **Bucket overflow churn** before rebuild. Mitigation: fixed-cap `2B` leaves trigger an in-place single-leaf split inside `insert_index` the moment the leaf reaches `cap`; the end-of-batch recursive sweep also catches any leaf with `bucket_size > 2 * leaf_bucket_size` as a backstop. No reallocate-and-leak path.
- **FP edge cases at exactly `resolution`**. Treated as harmless per research F11.
- **Eigen alignment** for `Vector4f`. Caught by `point_traits` static checks.
- **Mutation telemetry is fully opaque.** Callers see only inserted/removed counts; per-input outcomes (rejection cause, FIFO victim identity) are unavailable. If a downstream user pushes back, the cleanest restoration is an opt-in caller-provided out-span; not adding pre-emptively.

## Alternatives considered

- **Full global rebuild (no scapegoat).** Worse tail spike; rejected.
- **Bentley–Saxe logarithmic forest.** ~2.5× query slowdown; rejected.
- **Eager removal with reverse map (ikd-tree-style).** Rejected per user instruction.
- **Off-thread rebuild + atomic-pointer swap.** Best for tail latency; out of scope for v1.
- **Sliding-midpoint splits.** Better on clustered data; deferred behind a future `Config::SplitStrategy` enum.
- **Per-node AABB cache (ikd-tree style).** Standard 24B AABB on every node enables tight pruning everywhere. Rejected in favor of the hybrid scheme in [Tree topology](#tree-topology-research-f7-f8) — internal nodes stay 32B (cache density preserved on the hot kNN/radius descent), spatial deletes still get tight pruning at leaves where it matters most. Reconsider if `delete_box` profiling shows internal-level pruning is the bottleneck.
- **Point-per-node tree (ikd-tree style).** Each node holds one point + bbox + split info; tree size ≈ N nodes; pairs naturally with point-granular tree rotation and parallel rebuild thread. Rejected for v1 because bucket-leaf gives ~1.5–3× faster kNN/radius (shallower tree by `log B`, cache-coherent leaf scans, amortized pruning per bucket) and smaller meta footprint (~32B × 2N/B vs ~80B × N). Reconsider if a v2 concurrent-write workload + parallel rebuild thread changes the calculus.

## Resolved decisions

1. Q1 — `alpha` default locked at `0.7f`; `leaf_bucket_size` default is `detail::default_leaf_bucket_size_v<Dim>` = 5 for all D ∈ {2,3,4}; both still user-overridable via `Config`.
2. Q2 — Empirical leaf bucket size B: fixed at 5 globally (matches benchmarks for D∈{2,3,4}; revisit if a higher-D variant is added).
3. Q3 — `SplitStrategy` enum deferred; v1 hardcodes median-of-max-spread.
4. Q4 — Off-thread rebuild deferred to v2; tracked under [Alternatives considered](#alternatives-considered).
5. Q5 — No `Handle` exposed; result rows carry coords. Persistent point identity is the caller's concern.
6. Q6 — `insert(span<const Point>) -> std::size_t` only; no out-span overload and no `InsertOutcome` type in v1 (future-extension note retained in [Mutation result shape](#mutation-result-shape)).
7. rev7 — `T` template parameter removed (the tree is hardcoded to `float`; aliases collapse to `KDTree2/3/4`); internal `slot` terminology renamed to `index` (internal-only, never on the public surface).
8. rev14 2026-05-14 — Resolution dedup folded into the single-pass insert path (tree-side `any_within` alone, enabled by incremental visibility); `BucketPool` renamed to `LeafBucket`; leaves are fixed-cap (`2B`) with immediate single-leaf split on `push` overflow; partial rebuild reuses the scapegoat's node-pool slot in place (no reparenting); `dirty_path` renamed to `modified_indices`; `root_bbox_` member added (populated; consumer lands in a later slice).
9. rev16 2026-05-14 — `maybe_partial_rebuild` becomes a recursive top-down sweep that rebuilds every violator subtree in place, with a post-children re-check at non-violator nodes so child-rebuild-induced ratio shifts are caught in the same batch. The `modified_indices_` scratch buffer is removed end-to-end (member, parameters, descent-time appends); `insert_index` / `tombstone_index` lose their out-param. Cost is O(node_pool size) = O(N/B).

## Open questions

1. Empirical α sweet spot at N=1e6 for the scapegoat trigger.

## Future extensions

Designed for SLAM-style correspondence / surface-fit workflows. The kd-tree exposes buckets and their tight AABBs; **feature extraction (centroid, covariance, plane normal, signed-distance fitting) is the caller's responsibility**, not ours. Sketch:

```cpp
template <int Dim>
struct BucketView {
    std::uint32_t              leaf_id;   /// Stable while tree topology unchanged.
    std::span<const Point>     points;    /// Live points in this bucket.
    const BBox<Dim>&           bbox;      /// Tight per-leaf AABB.
};

std::optional<BucketView> bucket_containing(const Point& query) const;
std::vector<BucketView>   buckets_within  (const Point& query, float r) const;
```

`leaf_id` invalidates on any topology change (rebuild, partial rebuild, leaf split). Not added in this slice — the bucket / `BBox<Dim>` infrastructure is the prerequisite, which lands here.

## Implementation notes

> Status as of 2026-05-14 (rev16). Per-slice history lives in git; only
> the current state and non-obvious design choices are recorded here.

### Implemented

- **Public API surface**: `KDTree<Dim>` for D ∈ {2, 3, 4}; Config
  validated in ctor (capacity, resolution, leaf bucket size, alpha,
  tombstone threshold). `Neighbor { Point coord; float sq_dist; }`. Spatial
  deletes: `delete_box`, `delete_boxes` (batch, one rebuild for the
  whole list), `delete_outside_radius`; `BBox<Dim>` is a public type in
  `topiary/bbox.hpp`.
- **Queries**: `knn_search`, `radius_search`, `hybrid_search` through the
  unified `SearchKernel<Dim>::search` (bounded max-heap, incremental
  box-distance pruning); `any_within` early-exit predicate sibling for the
  dedup hot path.
- **Single-pass incremental insert with resolution dedup**: tree-side
  `any_within` covers both intra-batch and prior-batch dedup;
  `insert_index` appends to the target leaf's bucket and expands the leaf
  `BBox<Dim>` + `root_bbox_`; end-of-batch `maybe_partial_rebuild`
  applies the scapegoat/tombstone/leaf-overflow trigger once.
- **Coord-based remove**: `remove(span<Point>)` collects matches via
  `collect_indices_within(resolution)`, then for each match
  `tombstone_index → release` (in that order, for descent coord
  availability); batch-end `maybe_partial_rebuild`.
- **FIFO eviction with generation handles**: `PointStore::acquire`
  silently overwrites the oldest live index when full; per-slot
  `generations_[i]` is bumped on every reuse so stale bucket entries
  pointing at the prior occupant are auto-invalidated by the leaf-scan
  gen guard.
- **`rebuild_all`**: clears node/leaf-bucket/leaf-bbox pools, gathers
  every live index, dispatches to `TreeBuilder::rebuild`.
- **Spatial deletes**: `TreeBuilder::delete_box` /
  `delete_outside_radius` descend carrying the partition AABB (seeded from
  `root_bbox_`, narrowed by split planes); classify partition-vs-query as
  DISJOINT (skip) / CONTAINED (`release_subtree` bulk-release) / STRADDLE
  (recurse), with a tight `leaf_bboxes_` refinement before per-point checks
  at leaves. Each recursor returns its released count and the caller
  subtracts it from the node's `subtree_live_count` (mirrors
  `tombstone_index`), recording each touched internal node into the
  builder's own `modified_nodes_`; `subtree_total_count` is left for the
  batch-end `maybe_partial_rebuild` trigger that `KDTreeImpl` drives.
- **Batch box delete**: `delete_boxes(span<const BBox<Dim>>)` runs every
  box through `TreeBuilder::delete_box` (accumulating `modified_nodes_`
  across the whole batch) and fires a SINGLE end-of-batch
  `maybe_partial_rebuild`. The single-box `delete_box(box)`
  delegates to it with a one-element span, so there is one code path.
- **Trust-the-caller surface**: no precondition throws on negative
  `radius` or `k == 0` (Config validation only). Operational outcomes
  flow through return values.

### Stubbed (mirrors `docs/TODO.md`)

- Future bucket-view API (`bucket_containing`, `buckets_within`) —
  sketched in §Future extensions; not started.

### Non-obvious implementation choices

- **Per-slot `std::uint32_t` generation stamps invalidate stale bucket
  entries automatically.** `PointStore` holds `std::vector<std::uint32_t>
  generations_` (one counter per slot, default 0); `acquire` bumps the
  counter on every slot reuse (normal-branch occupant write AND
  FIFO-eviction victim retire). Every leaf bucket entry is a
  `BucketEntry { uint32_t index; uint32_t gen; }` (8 B) stamped at push
  time, and every leaf scan in `search_kernel.cpp` skips entries where
  `generations_[entry.index] != entry.gen`. This closes the
  release+reacquire / FIFO-eviction window where the tree's leaf still
  referenced an index that the store had already reused. Per-slot
  uint32_t was chosen over a global counter (wraps in ~12 h at 100k
  acquire/sec) and over a packed 24+8 layout (wraps in ~43 min per slot
  at the same rate); per-slot uint32_t wraps in centuries even at SLAM
  throughput. Memory cost: +4 MB per N=1M for `generations_`, plus
  doubling of bucket storage to ~16 MB at N=1M with cap=2B leaves.
- **`TreeNode` uses an anonymous union with unnamed inner structs.**
  Callers access the active arm as `node.split_dim` / `node.bucket_offset`
  directly — no `internal.` / `leaf.` middle hop. The named `Internal`
  and `Leaf` types are gone. The unnamed inner structs are a widely
  supported GCC/Clang extension; `-Wpedantic` is suppressed locally at
  the type's definition site. `sizeof(TreeNode) == 32` remains
  static-asserted.
- **Single tree-side dedup query, no intra-batch accepted set.**
  Incremental `insert_index` makes earlier-accepted candidates visible to
  the tree within the same batch, so `kernel_.any_within` alone covers
  both prior-batch and intra-batch dedup. Drops the per-call
  `std::vector<Point> accepted` allocation that an inline scan would
  need.
- **`LeafBucket` capacity = 2B per leaf, immediate split on overflow.**
  Leaves never grow past `2 * leaf_bucket_size`; if `push` returns
  `false`, `maybe_partial_rebuild` splits that leaf at end of batch. No
  tail-reallocate-and-leak, no `compact` API. `LeafBucket::data_.size()`
  stays bounded by ~2N.
- **Partial rebuild reuses the scapegoat's node-pool slot.** The new
  subroot is written in place at the scapegoat's existing index, so the
  parent's `left` / `right` child pointer is correct without any
  reparenting step. Old descendants leak into `nodes_` (bounded by N;
  reclaimed on the next `rebuild_all`).
- **Recursive top-down sweep with post-children re-check.**
  `maybe_partial_rebuild` recurses from the root, rebuilding any violator
  subtree in place; for non-violators it descends into both children and
  then re-checks itself because a child rebuild may have shifted the
  left/right ratio. Cost is O(node_pool size) = O(N/B), comfortably under
  1 ms at N=1M. Every violator the trigger conditions can detect is
  resolved within the same batch. The previous `modified_indices_`
  scratch buffer + first-hit highest-violator scan is gone end-to-end.
- **Spatial delete lives in `TreeBuilder`, not a free function.** A region
  delete is a tree mutation (release points + decrement
  `subtree_live_count` + record touched nodes), so it sits next to
  `insert_index` / `tombstone_index` and shares `modified_nodes_` and the
  storage members directly. The earlier free-function entry point threaded
  a `modified` out-param solely to reach the rebuild trigger — that hack is
  gone; only the internal recursion helpers (in the `tree_builder.cpp`
  anonymous namespace) still pass their storage refs by argument, which is
  fine for file-local recursion. `TreeBuilder::points_` is held by
  non-const reference because delete must `release`.
- **`tombstone_index` must be called BEFORE `points_.release(idx)`** —
  the descent inside `tombstone_index` reads `points_.point(idx)` to
  follow split planes to the host leaf; the coord must still be valid.
  `KDTreeImpl::remove` enforces this order.
- **`buffer_` doubles as FIFO live region + free list.** The live region is
  `[buf_head_, buf_tail_) mod capacity_`; the slots elsewhere hold parked
  free indices. Ctor seeds with `std::iota`. `acquire` reads
  `buffer_[buf_tail_]` (originally-free or just-evicted) and advances the
  tail. `release` swaps the freed buffer position with the position just
  before the tail, parks the freed index there, and retracts the tail —
  O(1) removal-by-index, live region stays contiguous. The naive
  "next-free = `static_cast<uint32_t>(live_)`" was abandoned because it
  breaks once `release` is called from a non-tail position.
- **Leaf-scan skip condition**: `if (sq_dist >= worst_sq_dist) continue;`
  works for kNN (no-op while `worst_sq_dist == +inf`, then becomes the
  worst-in-heap once full) AND for radius/hybrid (filters out-of-radius
  points). The earlier `&& heap.size() == k_max` clause silently failed
  for radius (heap never reaches `SIZE_MAX`).
- **`for_each_live` is type-erased** via `std::function<void(uint32_t,
  const Point&)>` so the implementation lives in `point_store.cpp`
  (project preference for header/source separation). The single call-site
  lambda fits SBO so no heap alloc, but pays virtual dispatch per visit.

### Build + test status

- `cmake --build build -- -j $(nproc --ignore=1)` — clean, zero warnings.
- `ctest -j $(nproc --ignore=1)` — all green (93/93 on `build` and
  `build-asan`).
- `clang-format` applied repo-wide.
