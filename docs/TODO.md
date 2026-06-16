# TODO — `KDTree`

Working list of slices for the fixed-size kd-tree, in recommended order.
Each section is meant to land independently with the build green and tests
covering the new behavior. Check items off as they ship.

Plan reference: [`design/fixed-size-kdtree.md`](design/fixed-size-kdtree.md).

## Design + skeleton

- [x] Public API surface (`KDTree<D>`, `Config`, `Neighbor`)
- [x] Internal `impl/` headers (`PointStore`, `BucketPool`, `TreeBuilder`,
      `SearchKernel`, `KDTreeImpl`, `point_traits`, `tree_node`)
- [x] CMake + Ninja wiring; Eigen + Catch2 v3 dependency setup
- [x] `tests/` and `benchmarks/` scaffolding
- [x] Dockerfile `dev` stage; `.clang-format`, `.clang-tidy`,
      `cmake/ClangTidy.cmake`

## Plan + skeleton revision (rev5)

- [x] Docstring rule sweep (`///` only; full `@brief`/`@param`/`@return`/`@tparam`,
      no `@invariant`/`@note`)
- [x] Lift `PointStore::for_each_live` template definition into the header
      (fix link-time hole)
- [x] Carve out mechanical PIMPL forwarders from the `// TODO:` skeleton
      rule

## Open questions resolved (rev6)

- [x] Q1: lock `alpha = 0.7`; per-D `default_leaf_bucket_size_v<D>`
      (8/16/32 for D=2/3/4)
- [x] Q3: defer `SplitStrategy` enum; hardcode median-of-max-spread
- [x] Q4: defer off-thread rebuild to v2
- [x] Q5: drop `Handle` exposure entirely
- [x] Q6: `insert(span<Point>) -> size_t` only; no out-span overload in v1

## Bulk-load + `knn_search` (rev7)

- [x] Drop `Handle`, `is_valid`, `point_of`; `Neighbor { Point coord; float
      squared_distance; }`
- [x] Drop `T` template parameter; hardcode `float`; aliases
      `KDTree2/3/4`
- [x] Rename internal `slot` → `index`
- [x] `Config` validation in ctor (capacity, resolution, leaf bucket size,
      alpha, tombstone threshold)
- [x] `PointStore<D>::acquire(p)` returns next sequential index; throws when
      full (FIFO out of slice)
- [x] `BucketPool::allocate` + `view`
- [x] `TreeBuilder<D>::rebuild` via median-of-max-spread + `std::nth_element`
- [x] `SearchKernel<D>::search` knn variant (bounded max-heap, standard
      pruning); populates `Neighbor::coord` from `PointStore`
- [x] `KDTreeImpl<D>::insert` whole-tree rebuild per batch;
      `knn_search` rejects `k == 0`
- [x] BDD-style Catch2 tests across all units; 20/20 passing
- [x] `clang-format` applied repo-wide
- [x] Bump Eigen dep 3.4 → 5.0

## Plan cleanup (rev8)

- [x] Drop pre-rev7 implementation notes
- [x] Drop rev-history breadcrumbs in Alternatives considered
- [x] Retire resolved `ASSUMPTION:` markers
- [x] Compress Mutation result shape historical narrative
- [x] Fix Eigen version reference; simplify Q5 wording; collapse rev marker
      chain (419 → 371 lines)

## FIFO eviction

Capacity-bounded streaming. When the tree is full, the next `insert`
silently overwrites the oldest live index instead of throwing.

- [x] `PointStore::acquire(p)` evicts FIFO head when `live_ == capacity_`
      (pop `buffer_[buf_head_]`, advance head, write into that index, append
      to buffer tail)
- [x] `PointStore::release(index)` clears live bit, removes from buffer in
      O(1) via `buf_pos_`, decrements `live_`
- [x] `KDTreeImpl::insert` drops the pre-acquire capacity check;
      eviction is silent
- [x] Tests: capacity overflow wraps cleanly; FIFO eviction order; evicted
      points no longer appear in `knn_search`
- [x] Rename `ring_` → `buffer_`, `index_to_ring_pos_` → `buf_pos_`,
      `ring_head_`/`ring_tail_` → `buf_head_`/`buf_tail_`
- [x] Benchmark: insert speed without eviction vs. with eviction; report at
      `docs/benchmark/insert.md`

## `radius_search` + `hybrid_search`

Same kernel, parameterized by `(k, r²)`.

- [x] `SearchKernel::search` accepts `initial_squared_radius` and `k_max`
      arguments (no defaults; callers always pass both)
- [x] `KDTreeImpl::radius_search(q, r)` — kernel call with
      `k=SIZE_MAX`, `r² = r*r`
- [x] `KDTreeImpl::hybrid_search(q, k, r)` — both bounds
- [x] Tests: brute-force agreement on random inputs across D ∈ {2, 3, 4};
      empty match returns empty; hybrid bounded by whichever fires first

## `remove`

Coord-based removal. Used by external "drop these points" callers (SLAM
outliers, dynamic-object culling).

- [x] `KDTreeImpl::remove(span<Point>)` — for each query, run a radius
      search at `Config::resolution`; release each match via
      `PointStore::release`
- [x] Tree-side: matches stay in leaf buckets but liveness bit is cleared,
      so `SearchKernel` skips them (no tree restructuring in this slice)
- [x] Tests: explicit remove drops points from subsequent queries; no-match
      remove is a no-op; remove count matches cleared indices

## Resolution dedup on insert

Both intra-batch and tree-side; folded into the single-pass insert below.

- [x] Tree-side `SearchKernel::any_within` covers intra-batch + prior-batch
      (incremental `insert_index` makes earlier accepted candidates
      visible to the same query)
- [x] Tests: duplicates within `resolution` collapse to one; first-seen
      rule holds across input re-orderings

## Incremental insert + scapegoat trigger + hybrid AABB

Replace the whole-tree rebuild with the per-batch incremental path. Land
the per-leaf AABB infrastructure here too (prerequisite for spatial
deletes; integrates naturally since TreeBuilder is being reworked anyway).

- [x] `LeafBucket::push(offset, size, capacity, BucketEntry)` — append
      to a bucket slice; signal full when `size == capacity`
- [x] (`compact` dropped — fixed-cap `2B` leaves with immediate split on
      overflow leave no leak to reclaim)
- [x] `TreeBuilder::insert_index` — descend, append, expand leaf BBox +
      root_bbox, push visited nodes to `modified_indices`; eager split
      when leaf hits `2B`
- [x] `TreeBuilder::tombstone_index` — descend, decrement
      `subtree_live_count` on path; bucket entry left in place (gen guard
      handles staleness)
- [x] `TreeBuilder::maybe_partial_rebuild` — walk `modified_indices`;
      rebuild highest unbalanced / tombstoned / overflowed subtree
      in-place
- [x] `KDTreeImpl::insert` rewrite — single-pass dedup +
      incremental insert + end-of-batch trigger
- [x] `leaf_bbox_idx` in `TreeNode` Leaf payload; sizeof 32B
      static_asserted (anonymous union, no inner type names)
- [x] `KDTreeImpl` gains `std::vector<BBox<Dim>> leaf_bboxes_` +
      `BBox<Dim> root_bbox_` members
- [x] `TreeBuilder::rebuild` populates each leaf bbox + `root_bbox_`;
      `insert_index` expands the touched leaf bbox + root_bbox;
      `tombstone_index` leaves bbox alone (refreshed on next rebuild)
- [x] Per-slot `uint32_t generations_` in `PointStore`; `BucketEntry {
      index, gen }` stamped at push; every leaf scan skips on gen
      mismatch (auto-invalidates stale entries from FIFO eviction OR
      release+reacquire)
- [x] Tests: unbalanced inserts trigger one partial rebuild per batch;
      tombstone fraction trigger fires after many removes; per-leaf bbox
      stays a valid superset across insert/release sequences; gen-stale
      skip on remove + on FIFO eviction

## `rebuild_all`

Manual escape hatch (currently unused outside tests).

- [x] `KDTreeImpl::rebuild_all` — full `TreeBuilder::rebuild` over all
      live indices; resets node, leaf-bucket, leaf-bbox pools
- [x] Tests: post-rebuild leaf set covers exactly the live index set

## Spatial deletes

Bbox-based and radius-based bulk deletion. Uses the per-leaf AABB
infrastructure introduced alongside slice 5 (incremental insert) plus
partition-AABB derivation during descent.

- [x] `KDTree::delete_box(const BBox<Dim>& box)` — release every
      live point inside the AABB; returns count cleared
- [x] `KDTree::delete_outside_radius(center, r)` — release every live
      point strictly outside the sphere; returns count cleared
- [x] `SearchKernel` gains `delete_box_descend` and
      `delete_outside_radius_descend` free functions; both carry the
      partition AABB in recursion frames and consult `leaf_bboxes_` at
      leaves for tight pruning
- [x] Subtree-bulk-release helper: when partition (internal) or leaf bbox
      is fully contained in the query, release every live index in the
      subtree without per-point checks
- [x] Tests: brute-force agreement on random data; corner cases (empty
      box, box covering all live points, radius 0, radius ≥ root extent);
      subsequent searches do not return released points

## ikd-tree comparative benchmark (later)

Microbenchmark comparing tkd-tree against ikd-tree (HKU MARS) on SLAM
workloads. Run once Slice 5 + Spatial deletes have landed so the
comparison is meaningful.

- [x] `FetchContent` ikd-tree (`@c0e36a16`), PCL-free via shim + sed-patch;
      two object targets (BG-off / BG-on) behind a type-erased facade
- [x] `benchmarks/bench_perf_ikd.cpp` — common workloads: per-batch insert
      latency, kNN throughput, radius search, spatial delete, memory
      footprint; run at N ∈ {10k, 100k} (1M dropped from the run — dominant
      cost, signal saturated by 100k; 1M memory projected analytically)
- [x] Run modes: tkd-tree (single-thread) | ikd-tree single-thread (BG
      rebuild disabled) | ikd-tree default (BG rebuild on) — report all
      three transparently so algorithmic vs concurrency wins are
      separable
- [x] Report at `docs/benchmark/vs_ikd_tree.md` — methodology, results
      table, Mermaid charts, fairness caveats

## Tuning (benchmark work, not blocking)

Open questions still in the plan. Use the existing `bench_*.cpp` files;
sweep against representative workloads.

- [ ] α sweep at N=1e6 over α ∈ {0.6, 0.65, 0.7, 0.75, 0.8}
- [ ] B sweep over B ∈ {8, 16, 32} per D ∈ {2, 3, 4}

---

This file tracks the fixed-size variant only. When other kd-tree variants
get scaffolded, either split this into per-variant TODO files or add
top-level sections here.
