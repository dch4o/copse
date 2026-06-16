# Fixed-size, Mutable kd-Tree in C++

> Research date: 2026-05-04 (revised 2026-05-05 to remove prescriptive code design) · Slug: fixed-size-kdtree

## TL;DR

For a fixed-capacity, mutable, low-dimensional kd-tree at $N \in [10^5, 10^6]$ with FIFO eviction, a `resolution`-based dedup invariant, and the throughput targets below, the literature converges on three load-bearing techniques: (1) **storing point coordinates separately from tree topology** so that mutation only touches a slot, not the tree; (2) **lazy deletion (tombstones) combined with a deterministic, partial-rebuild trigger** (scapegoat-style $\alpha$-weight balance and/or a tombstone-fraction threshold) so query quality does not silently degrade; and (3) **generational/versioned indexing** so a tree node that still references a slot whose occupant has been replaced by FIFO is provably detectable on its next traversal. The biggest correctness risk is **search-time stale references**: any leaf-handle dereference during traversal must be guarded by a generation check, and any distance comparison must skip dead entries.

## Context and scope

### Question

Survey the design space for a **fixed-capacity, mutable kd-tree** for low-dimensional ($d \in \{2,3,4\}$, predominantly 3D `float`) point data, supporting kNN, radius, and hybrid (kNN-within-radius) queries plus batched insert and remove. When capacity is reached, new inserts overwrite the FIFO-oldest entries (ring-buffer semantics). The scope is **what techniques exist, what trade-offs they imply, and what published systems do** — the actual API and code shape is decided downstream by a separate code-design step.

### Locked workload parameters (treated as analysis input, not as a code spec)

- **Dimensions:** 2–4, predominantly 3D `float` (Eigen-compatible).
- **Capacity:** $N \in [10^5, 10^6]$, fixed at construction.
- **Eviction:** strict FIFO when full. No timestamps, no TTL.
- **Duplicates:** rejected. Any insert candidate within a construction-time `resolution` distance of an existing live point is dropped.
- **Removal semantics:** "remove all live tree points within `resolution` of each query point" (normally 0 or 1 per query under the dedup invariant, but the contract is "all matches").
- **Throughput targets:** ~10k–100k inserts/sec sustained (delivered as 1k–10k point batches at ~10 batches/sec, i.e. ~100 ms inter-batch budget), ~10k–100k single-point searches/sec (5–20 search ops per insert).
- **Concurrency:** single-writer; concurrent readers not required now.
- **Search modes needed:** kNN, radius, kNN-within-radius (hybrid).

### Out of scope

- Approximate NN (ANN), hashing, learned indexes.
- GPU implementations.
- Disk-resident / external-memory variants (Bkd, K-D-B): not relevant at this $N$.
- ikd-tree code patterns (per user instruction); the *ideas* are fair game to discuss.
- Concrete API signatures and code shape (handled by a separate code-design step).

## Findings

### F1. The classical kd-tree is not naturally dynamic

- Bentley's 1975 paper introduces kd-trees as fundamentally a static structure; it provides $O(\log n)$ insertion *on average* but $O(n^{(k-1)/k})$ for root deletion, and an $O(n \log n)$ "optimization" (full rebuild) is required to restore guarantees [HIGH] [Bentley 1975](https://dl.acm.org/doi/10.1145/361002.361007).
- AVL/red-black-style rotations cannot be applied to kd-trees because the splitting dimension at each node depends on tree depth; a rotation would re-mix the dimensional invariant. This is widely acknowledged and is the central reason mutable kd-trees rely on subtree rebuild rather than rotation [HIGH] [Brown 2025 arXiv:2509.08148](https://arxiv.org/abs/2509.08148), [Scapegoat tree — Wikipedia](https://en.wikipedia.org/wiki/Scapegoat_tree).
- Friedman/Bentley/Finkel (1977) is the canonical reference for the $O(\log n)$ expected-time best-match search algorithm with bounded backtracking via splitting-plane / hypersphere tests [HIGH] [Friedman, Bentley, Finkel 1977](https://dl.acm.org/doi/10.1145/355744.355745).

### F2. Mutation strategies in the literature

Three families of techniques are reported as practical for dynamic kd-trees:

- **Scapegoat / $\alpha$-weight-balanced partial rebuild.** Each node tracks subtree size; on insert/delete, walk back to the root and find the highest "scapegoat" node whose children violate $\max(|L|, |R|) \le \alpha \cdot |\text{node}|$ for some $0.5 < \alpha < 1$, then rebuild that subtree from its leaves. Amortized $O(\log n)$ per operation. Galperin & Rivest's original scapegoat-tree analysis treats $\alpha$ as a tunable balance parameter; values in roughly the 0.55–0.75 range are reported in the literature, with smaller $\alpha$ giving more frequent but smaller rebuilds [HIGH] [Scapegoat tree — Wikipedia](https://en.wikipedia.org/wiki/Scapegoat_tree), [Brown 2025 arXiv:2509.08148](https://arxiv.org/abs/2509.08148). Brown's recent dynamic kd-tree [(GitHub: RussellABrown/kd-tree, commit `master` ~2025-09)](https://github.com/RussellABrown/kd-tree) takes this approach.
- **Tombstone + global rebuild** (rebuild the whole tree when live count drops to half of size at last build) is the simpler classical fallback with the same amortized bounds in low-d. The trade-off is a worst-case full-rebuild spike at large $N$ (see F12) versus implementation simplicity.
- **Bentley–Saxe logarithmic forests** (the technique behind nanoflann's dynamic adaptor) maintain $O(\log n)$ static sub-indices and rebuild them in a logarithmic schedule. Inserts amortize to $O(\log^2 n)$; queries pay an extra $O(\log n)$ factor because they must visit each sub-index. One nanoflann user reports approximately a **2.5× query slowdown** versus the static index on equivalent data [UNVERIFIED — single user report] [nanoflann issue #104](https://github.com/jlblancoc/nanoflann/issues/104). The technique is most attractive when inserts dominate or when avoiding rebalancing logic is preferred over query performance.

### F3. nanoflann's dynamic adaptor uses Bentley–Saxe and pays for it

- `KDTreeSingleIndexDynamicAdaptor` implements the logarithmic method (multiple static sub-indices) [HIGH] [nanoflann docs](https://jlblancoc.github.io/nanoflann/classnanoflann_1_1KDTreeSingleIndexDynamicAdaptor.html).
- Reported user-observed slowdown for queries: roughly **2.5×** vs the static index on equivalent data [UNVERIFIED — single user report] [nanoflann issue #104](https://github.com/jlblancoc/nanoflann/issues/104). Cost stems from querying $O(\log n)$ sub-trees and aggregating results.
- Implication for query-heavy workloads: the per-query overhead compounds when many queries are performed per insert.

### F4. CGAL's kd-tree treats mutation as deferred batch

- `CGAL::Kd_tree::insert` does *not* update the structure; the next query (or explicit `build()`) triggers a full rebuild. `remove` is naive (no rebalance) and the user is expected to call `build()` periodically [HIGH] [CGAL 6.1.1 dD Spatial Searching](https://doc.cgal.org/latest/Spatial_searching/index.html).
- This is a reasonable model for batch geometry processing where queries are clustered in time and bracketed by explicit rebuild calls. It is a poor model when inserts and queries are interleaved at high rate.

### F5. PCL/FLANN does not really support mutation

- `pcl::KdTreeFLANN` wraps FLANN's static index; updating points means constructing a new tree [HIGH] [PCL kdtree module docs](https://pointclouds.org/documentation/group__kdtree.html).
- FLANN itself has been effectively unmaintained since April 2019 [HIGH] [PCL issue #4699](https://github.com/PointCloudLibrary/pcl/issues/4699). PCL 1.15.1 added optional nanoflann backing [HIGH] [PCL 1.15.1 release notes](https://github.com/PointCloudLibrary/pcl/releases/tag/pcl-1.15.1).
- Open3D's `KDTreeFlann` is similarly a thin wrapper around FLANN with no first-class incremental mutation [HIGH] [Open3D KDTreeFlann docs](https://www.open3d.org/docs/release/python_api/open3d.geometry.KDTreeFlann.html).

### F6. Generational / versioned indexing as the load-bearing safety mechanism

- A slot map pairs each storage slot with a monotonically increasing generation counter; a reference is logically `(index, generation)`. On lookup, the reference's generation is compared to the slot's; mismatch means the original entry was reclaimed [HIGH] [Hagins slotmap (C++20), commit `master` ~2024](https://github.com/jodyhagins/slotmap), [ECS overview, Mertens](https://github.com/SanderMertens/ecs-faq).
- For a FIFO-overwriting kd-tree, this technique is what makes lazy tombstoning **safe**: when slot $i$ is overwritten, every tree node that still references the prior occupant is automatically invalidated on its next traversal because the slot's generation has changed. Without a generation check, a tree node would silently start referring to the new point in slot $i$ — geometrically wrong, no crash. This is a finding about the technique, independent of how it is exposed (or not) at the API surface.

### F7. Storage-layout options

- **AoS in-place (tree node owns its point).** Best traversal locality but mutation requires moving points; conflicts naturally with ring-buffer semantics.
- **Tree node references a separate point store.** Tree topology stores splitting planes and child links; point coordinates live in a separate array (which can be the FIFO ring). Locality is one indirection worse during distance evaluation, but mutation is dramatically cleaner: deleting a point only touches its slot; the tree node can stay until the next subtree rebuild. This is the layout used by nanoflann (it accesses points through a `DatasetAdaptor` interface) [HIGH] [nanoflann v1.9.0, 2025-12-22](https://github.com/jlblancoc/nanoflann).
- **Separate node pool + separate point array.** Same as above, but tree nodes live in a contiguous array indexed by integer rather than allocated individually. Wins: cache-friendly node traversal, free-list management without `new`/`delete`, easy preallocation since maximum node count is bounded by ~$2N/B$ for a leaf-bucketed tree.

The general lesson from the survey: at this scale, mutation friendliness wins over the marginal locality cost of one indirection.

### F8. Bucketed leaves

- Storing $B$ points per leaf instead of one materially improves traversal cost in 3D point clouds: fewer split-plane decisions per query, better cache use during the leaf's intra-bucket linear scan, and less node-allocation churn [HIGH] [k-d tree — Wikipedia](https://en.wikipedia.org/wiki/K-d_tree), [pbrt kd-tree accelerator](https://pbr-book.org/3ed-2018/Primitives_and_Intersection_Acceleration/Kd-Tree_Accelerator). Reported defaults and norms: nanoflann exposes this as `leaf_max_size` with a default of 10 [HIGH] [nanoflann v1.9.0](https://github.com/jlblancoc/nanoflann); pbrt and FLANN report bucket sizes in the 8–32 range as common for point-cloud workloads.
- For mutation, buckets are friendlier: removing one point from a 16-point bucket doesn't change the tree topology at all, just the bucket's content.

### F9. Splitting-plane heuristics

- "Median of max-spread dimension": pick the dimension with greatest range, split at the median of that coordinate. Yields a well-balanced tree but requires computing spread per dimension at build time [HIGH] [k-d tree — Wikipedia](https://en.wikipedia.org/wiki/K-d_tree).
- "Sliding midpoint": split at the midpoint of the dimension's range; if all points end up on one side, slide the plane to the nearest point. Reported as better behaved on clustered data; used by SciPy [HIGH] [SciPy KDTree docs](https://docs.scipy.org/doc/scipy/reference/generated/scipy.spatial.KDTree.html).
- Cyclic dimension splitting (depth modulo $d$) is the textbook default; for real (anisotropic) point clouds it is reported as markedly worse than max-spread or sliding-midpoint [HIGH] [k-d tree — Wikipedia](https://en.wikipedia.org/wiki/K-d_tree).
- For incremental insertion *without* rebuild, plane choice is fixed at the node's creation time and degrades over time — a documented motivation for periodic subtree rebuild rather than pure incremental insertion.

### F10. Hybrid (kNN-within-radius) is just kNN with the pruning radius initialized at $r^2$

- The standard implementation of kNN uses a max-heap of size $\le k$ keyed on squared distance; the current pruning radius $\rho^2$ is the heap top once the heap is full, and starts at $+\infty$ otherwise [HIGH] [k-d tree — Wikipedia](https://en.wikipedia.org/wiki/K-d_tree).
- Hybrid search initializes $\rho^2 := r^2$ instead of $+\infty$; the rest of the algorithm is identical. This formulation subsumes kNN ($r = +\infty$), pure radius (set $k$ unbounded, return everything within $r$), and the hybrid case.
- Distance comparisons throughout should be in squared-distance space to avoid square roots in the hot loop; this is consensus across implementations and benchmarked in [Sample & Haines](http://infolab.stanford.edu/~nsample/pubs/samplehaines.pdf).

### F11. The `resolution` invariant is a structural property worth exploiting

- After every successful insert, no two live points lie within `resolution` of each other (Euclidean). Therefore any radius search with $r < \mathtt{resolution}$ returns **at most one** point.
- **Consequence for insert dedup:** the dedup test against the live tree is structurally a 1-NN-within-`resolution` query; on a hit, the insert candidate is rejected; on a miss, it is safe.
- **Consequence for remove cost:** "remove all live tree points within `resolution` of $q$" is bounded above by a single radius search per query point, and the result list is normally empty or a singleton. The contract still says "remove all matches", so the implementation must enumerate matches rather than early-exit, but expected work per query is one $O(\log n)$ traversal plus $O(1)$ removals.
- This is general knowledge for the designer: it constrains worst-case cost and suggests where early-exits are or aren't safe, without dictating any particular code shape. Floating-point edge cases at exactly `resolution` distance are harmless and shouldn't be asserted as a hard invariant.

### F12. Worst-case partial-rebuild cost at $N=10^6$

Order-of-magnitude estimate, useful for tail-latency sizing. With $\alpha = 0.7$ and a tree built over $10^6$ points, the worst-case scapegoat subtree contains at most $\alpha N \approx 7 \times 10^5$ live points. Rebuilding from scratch via median-of-max-spread is $O(m \log m)$; at typical hardware throughputs of $\sim 10^7$–$10^8$ point-comparisons/sec for cache-friendly partition passes, that's roughly **0.2–2 seconds** in the absolute worst case. This **exceeds the 100 ms inter-batch budget**, so a real implementation has to mitigate it somehow. Mitigation strategies reported in the literature include:

- A smaller $\alpha$, making rebuilds more frequent but smaller — the highest scapegoat is bounded by $\alpha^k N$ at depth $k$ from the root.
- Incremental / time-sliced rebuild that yields between batches.
- Off-thread rebuild with a deferred swap (single-writer regime makes this straightforward to coordinate).

In the typical case the scapegoat trigger fires deep in the tree, where the affected subtree is much smaller and rebuild fits comfortably in the budget. The worst case matters for tail-latency budgeting, not steady-state throughput.

## Trade-offs

### Storage layout

| Option | Win | Cost |
|---|---|---|
| Tree node owns point (AoS) | Best traversal locality (one cache line per node visit) | Mutation requires moving points or reconstructing nodes; conflicts with ring-buffer semantics |
| Tree node references separate point store, point store is the FIFO ring | Mutation is local to a slot; supports tombstoning naturally | One extra indirection per distance computation (~10 ns on a hot cache, much worse on cold) |
| Same as above, plus bucket leaves of $B$ points | Amortizes the indirection across $B$ distance computations and dramatically reduces node count | Need to choose $B$ (8–32 reported as common); leaf bucket scan must be branch-friendly |

### Mutation strategy

| Strategy | Insert (amortized) | Delete (amortized) | Query overhead | Code complexity | Behavior at $N \in [10^5, 10^6]$ |
|---|---|---|---|---|---|
| Tombstone + global rebuild at $n_{\text{live}}/N \le 1/2$ | $O(\log n)$ + occasional $O(n \log n)$ | $O(\log n)$ | Skips tombstones | Low | Acceptable amortized; full-rebuild spikes can reach ~1 s at $N = 10^6$ |
| Bentley–Saxe logarithmic | $O(\log^2 n)$ | $O(\log n)$ via tombstone | $O(\log n)$ extra factor; ~2.5× observed [UNVERIFIED] | Medium | Insert-friendly; query slowdown is a real cost when queries dominate |
| Scapegoat $\alpha$-weight-balanced partial rebuild | $O(\log n)$ | $O(\log n)$ | None outside rebuild | Medium-high | Rebuild cost is localized to a subtree; tail latency depends on $\alpha$ (F12) |

### Identifying invalidated tree nodes after a slot overwrite

| Mechanism | Win | Cost |
|---|---|---|
| Eager: delete the old point from the tree before the new insert | Tree is always exactly the live set; no tombstone-skip logic in queries | Delete-then-insert costs ~2× a single update; deletion path must navigate to the right leaf via the point's coordinates |
| Generational handles + lazy tombstone in tree | $O(1)$ "delete" — bump the slot's generation; tree learns about it on the next traversal that touches the leaf | Every leaf access in the query path must compare generations; subtree rebuilds must filter tombstones |
| Reverse map (point slot → list of tree nodes referencing it) | Eager removal in $O(\log n)$ without re-searching by coordinate | Doubles per-node memory; bookkeeping every time a node moves during rebuild |

The eager + reverse-map combination is what ikd-tree-style designs effectively use, and is a documented reason their internals are intricate. Generational lazy tombstoning is simpler and integrates naturally with periodic subtree rebuild.

### Where to do dedup work in a batched insert

A batched insert with a `resolution`-based dedup invariant has at least three logically separable concerns: *batch-vs-batch* near-duplicates within the incoming points themselves, *batch-vs-tree* near-duplicates against the existing live set, and *capacity overflow* when the batch (post-dedup) would exceed $N$. These can in principle be handled in different orders and at different granularities. The trade-offs:

- Doing batch-vs-batch dedup first, before touching the tree, keeps the tree-side work proportional to the post-dedup batch size and avoids inserting then immediately removing.
- Doing batch-vs-tree dedup before eviction lets eviction be sized to the actual number of accepted candidates, not a worst-case estimate.
- Performing one rebuild-trigger check after the entire batch lands (rather than after each individual insert) is a known win in batch-dynamic structures; the alternative — per-point trigger checks — wastes work. This is reported in the parallel batch-dynamic kd-tree literature [HIGH] [Yesantharao 2021 arXiv:2112.06188](https://arxiv.org/abs/2112.06188).
- A deterministic tie-break rule for batch-vs-batch collisions (e.g. "first-in-batch wins" or "last-in-batch wins") needs to be picked and documented. The literature does not prescribe one; the choice should be consistent with the project's other ordering semantics.

The exact sequencing is a code-design decision; what the literature establishes is that all three concerns must be addressed and that per-point rebuild checks inside a batch are wasteful.

## Anti-patterns

- **Pure incremental insertion with no rebuild.** Bentley's $O(\log n)$ insert preserves the tree but not the splitting-plane quality; over thousands of inserts the tree degenerates and queries slow toward $O(n)$ on adversarial data. Documented in essentially every dynamic-kd-tree paper [HIGH] [Tao notes](https://www.cse.cuhk.edu.hk/~taoyf/course/comp3506/camp/kd-dyn.pdf), [Brown 2025](https://arxiv.org/abs/2509.08148).
- **Trying to rotate a kd-tree.** Rotations break the dimensional split invariant; the literature is unanimous that rebuild (full or partial) is the only correct rebalancing operation [HIGH] [Brown 2025 arXiv:2509.08148](https://arxiv.org/abs/2509.08148), [Scapegoat tree — Wikipedia](https://en.wikipedia.org/wiki/Scapegoat_tree).
- **Storing a raw integer index from a tree node into a ring buffer with no generation check.** When the slot is overwritten, the index silently points to a different point — wrong geometry, no crash. Generational/versioned references are how the literature solves this.
- **Wrapping `std::sqrt` into the inner pruning loop.** Every kd-tree implementation that has been measured agrees: keep the priority queue and pruning bound in squared-distance space; only take the square root for the user-facing return value (and only if the user actually asks for distances rather than indices) [HIGH] [Stanford kd-tree search optimization](http://infolab.stanford.edu/~nsample/pubs/samplehaines.pdf).
- **Splitting on a fixed dimension cycle (`depth % d`) regardless of data.** Works for uniform random points but is markedly worse than max-spread or sliding-midpoint for real point clouds (which are anisotropic) [HIGH] [k-d tree — Wikipedia](https://en.wikipedia.org/wiki/K-d_tree).
- **One point per leaf node.** Acceptable for didactic kd-trees, painful for production: more pointer chasing, more nodes to allocate, worse cache behavior than bucketed leaves [HIGH] [pbrt kd-tree accelerator](https://pbr-book.org/3ed-2018/Primitives_and_Intersection_Acceleration/Kd-Tree_Accelerator).
- **Returning unsorted kNN results when the contract says sorted.** A bounded max-heap stores results in arbitrary order; if sorted output is part of the contract, the heap must be sorted before being returned. Easy to forget when the heap *is* the result.
- **Mixing tombstones and rebuild without a deterministic trigger.** Without a rule (e.g. weight imbalance threshold, tombstone-fraction threshold, or both), tombstones accumulate and queries slow down silently. The trigger choice is a tunable, but its *existence* is not optional.
- **Per-point insert/remove inside a batch.** Doing $|B|$ individual rebuild-trigger walks back to the root is $|B| \cdot O(\log n)$ wasted work compared to deferring the trigger check to the end of the batch.
- **Bulk-removing by issuing $K$ individual single-point removes.** A batch of $K$ FIFO evictions on overflow can tombstone $K$ slots in a single pass and run one rebuild-trigger check at the end, rather than $K$ separate checks.

## OSS survey — what's worth learning from each

- **nanoflann** (header-only, MIT, [v1.9.0 released 2025-12-22](https://github.com/jlblancoc/nanoflann)). Static index is fast and well tuned; the `DatasetAdaptor` decoupling between tree topology and point storage is the layout to learn from. The dynamic adaptor uses Bentley–Saxe and pays measurable query cost (F3); the static path is the one to look at for low-level traversal idioms.
- **FLANN** (effectively unmaintained since April 2019, [PCL issue #4699](https://github.com/PointCloudLibrary/pcl/issues/4699)). Historically the reference for randomized kd-forest ANN and hierarchical clustering trees; not a model for incremental mutation. Useful for the bucket-leaf and squared-distance-pruning idioms.
- **PCL** (`pcl::KdTreeFLANN`, [release 1.15.1](https://github.com/PointCloudLibrary/pcl/releases/tag/pcl-1.15.1)). Wrapper over FLANN, recently with optional nanoflann backing; treats the tree as immutable and rebuilds on update. Useful as a contrast: in PCL's domain, static-rebuild is acceptable, which is why no incremental-mutation infrastructure is provided.
- **CGAL** (`CGAL::Kd_tree`, [6.1 docs](https://doc.cgal.org/latest/Spatial_searching/index.html)). Inserts are deferred until the next query or explicit `build()` call; remove is naive. Useful as a model for "batch geometry" workloads where mutation and query are temporally separated, and as a counterexample for high-rate interleaved use.
- **Open3D** (`KDTreeFlann`, [docs](https://www.open3d.org/docs/release/python_api/open3d.geometry.KDTreeFlann.html)). FLANN wrapper; same caveats as PCL.
- **RussellABrown/kd-tree** ([GitHub, master ~2025-09](https://github.com/RussellABrown/kd-tree)). A recent dynamic, self-balancing kd-tree using scapegoat-style partial rebuild, accompanying the Brown 2025 arXiv paper. Useful as a worked example of the scapegoat approach in C++ and Java.
- **slotmap** ([Hagins, C++20, master ~2024](https://github.com/jodyhagins/slotmap)). A clean C++20 generational slot map. Useful as a reference for the indexing technique itself.

## Recommendation

Given the locked workload — $N$ in the $10^5$–$10^6$ range, FIFO eviction, `resolution`-based dedup, batched insert/remove, single-query searches, queries dominating per insert, single-writer — the techniques the literature most directly supports are: a **separated point store + tree topology** layout (so FIFO eviction is decoupled from tree structure), **bucketed leaves** (so the indirection cost is amortized), **lazy deletion guarded by generational/versioned indexing** (so FIFO overwrite is safe under tombstoning), and a **deterministic, partial-rebuild trigger** in the scapegoat family (so query quality does not silently degrade and rebuild cost is localized rather than concentrated in one global event). Distance comparisons should stay in squared-distance space throughout, and the kNN/radius/hybrid search modes can share a single traversal kernel parameterized by an initial squared pruning radius.

Conditions that would shift the recommendation: if concurrent readers later become a requirement, the simpler tombstone+global-rebuild variant (or an RCU-style subtree swap) becomes more attractive than in-place partial rebuild; if the workload becomes batched bulk-load + rare query, the CGAL "rebuild on demand" model becomes adequate and avoids the partial-rebuild machinery entirely; if inserts come to dominate over queries, the Bentley–Saxe trade-off (cheaper inserts, costlier queries) flips in its favor. The exact API shape, trigger constants, batch ordering, and concurrency arrangement are downstream code-design decisions informed by — but not dictated by — the findings here.

## Open questions

- **Quantitative tail-latency behavior of partial rebuild at $N = 10^6$ on the actual target hardware.** The F12 estimate is order-of-magnitude; actual budget compatibility depends on machine, scalar type, and bucket size, and is best resolved by measurement.
- **Empirical comparison of $\alpha$ values at this $N$ for the scapegoat trigger.** The 0.55–0.75 range is reported in the literature, but the rebuild-frequency / rebuild-size / query-quality trade-off curve at this specific scale is not, to my knowledge, published.
- **Interaction between bucket size $B$ and 3D float SIMD-friendly distance evaluation.** Reported defaults (8–32) span a wide range; the optimum for an Eigen-backed `Vector3f` leaf scan would benefit from direct measurement.

## References

1. Bentley, J. L. — "Multidimensional binary search trees used for associative searching" — *Communications of the ACM* 18(9), 1975 — https://dl.acm.org/doi/10.1145/361002.361007 — accessed 2026-05-04.
2. Friedman, J. H., Bentley, J. L., Finkel, R. A. — "An algorithm for finding best matches in logarithmic expected time" — *ACM TOMS* 3(3), 1977 — https://dl.acm.org/doi/10.1145/355744.355745 — accessed 2026-05-04.
3. Procopiuc, O., Agarwal, P. K., Arge, L., Vitter, J. S. — "Bkd-Tree: A Dynamic Scalable kd-Tree" — SSTD 2003 — https://users.cs.duke.edu/~pankaj/publications/papers/bkd-sstd.pdf — accessed 2026-05-04.
4. Tao, Y. — "Fully Dynamic kd-Tree" lecture notes, COMP3506/7505, U. of Queensland — https://www.cse.cuhk.edu.hk/~taoyf/course/comp3506/camp/kd-dyn.pdf — accessed 2026-05-04.
5. Brown, R. A. — "A Dynamic, Self-balancing k-d Tree" — arXiv:2509.08148, 2025 — https://arxiv.org/abs/2509.08148 — accessed 2026-05-04.
6. Yesantharao, R. — "Parallel Batch-Dynamic kd-trees" — arXiv:2112.06188 — https://arxiv.org/abs/2112.06188 — accessed 2026-05-04.
7. "Scapegoat tree" — Wikipedia — https://en.wikipedia.org/wiki/Scapegoat_tree — accessed 2026-05-04.
8. "k-d tree" — Wikipedia — https://en.wikipedia.org/wiki/K-d_tree — accessed 2026-05-04.
9. nanoflann project (release v1.9.0, 2025-12-22) — https://github.com/jlblancoc/nanoflann — accessed 2026-05-04.
10. nanoflann `KDTreeSingleIndexDynamicAdaptor` API — https://jlblancoc.github.io/nanoflann/classnanoflann_1_1KDTreeSingleIndexDynamicAdaptor.html — accessed 2026-05-04.
11. nanoflann issue #104 (dynamic adaptor performance) — https://github.com/jlblancoc/nanoflann/issues/104 — accessed 2026-05-04.
12. CGAL 6.1 dD Spatial Searching User Manual — https://doc.cgal.org/latest/Spatial_searching/index.html — accessed 2026-05-04.
13. PCL `kdtree` module documentation — https://pointclouds.org/documentation/group__kdtree.html — accessed 2026-05-04.
14. PCL issue #4699: FLANN maintenance discussion — https://github.com/PointCloudLibrary/pcl/issues/4699 — accessed 2026-05-04.
15. PCL release 1.15.1 (nanoflann backend) — https://github.com/PointCloudLibrary/pcl/releases/tag/pcl-1.15.1 — accessed 2026-05-04.
16. Open3D `KDTreeFlann` documentation — https://www.open3d.org/docs/release/python_api/open3d.geometry.KDTreeFlann.html — accessed 2026-05-04.
17. Hagins, J. — `slotmap` (C++20 generational slot map) — https://github.com/jodyhagins/slotmap — accessed 2026-05-04.
18. Mertens, S. — ECS FAQ (generational handles) — https://github.com/SanderMertens/ecs-faq — accessed 2026-05-04.
19. RussellABrown — `kd-tree` (dynamic self-balancing kd-tree, C++ and Java) — https://github.com/RussellABrown/kd-tree — accessed 2026-05-04.
20. pbrt-v3 — "Kd-Tree Accelerator" — https://pbr-book.org/3ed-2018/Primitives_and_Intersection_Acceleration/Kd-Tree_Accelerator — accessed 2026-05-04.
21. Sample, N., Haines, M. — "Optimizing Search Strategies in k-d Trees" — Stanford InfoLab — http://infolab.stanford.edu/~nsample/pubs/samplehaines.pdf — accessed 2026-05-04.
22. SciPy `KDTree` documentation — https://docs.scipy.org/doc/scipy/reference/generated/scipy.spatial.KDTree.html — accessed 2026-05-04.

---

## Research metadata

- **Date:** 2026-05-04 (revised 2026-05-05 to remove prescriptive code design)
- **Search queries used:**
  - nanoflann kd-tree dynamic insertion deletion C++ header-only
  - kd-tree dynamic insertion lazy deletion rebuild threshold algorithm
  - scapegoat tree partial rebuild alpha weight balance kd-tree
  - Bentley 1975 kd-tree multidimensional binary search trees original paper
  - FLANN library kdtree dynamic insertion deletion source code
  - PCL kdtree FLANN wrapper static dynamic point cloud library
  - nanoflann KDTreeSingleIndexDynamicAdaptor implementation logarithmic method tree
  - CGAL kd tree spatial searching dynamic insert delete
  - Open3D kdtree nearest neighbor implementation source
  - k nearest neighbor search bounded priority queue max-heap kd-tree pruning squared distance
  - Friedman Bentley Finkel kd tree algorithm expected logarithmic time 1977
  - generational index versioned slot map ECS entity component system C++
  - nanoflann license BSD performance benchmark vs FLANN PCL
  - "kd-tree" tombstone deletion rebuild ratio threshold percentage
  - "kd-tree" insertion overwrite ring buffer SLAM rolling window point cloud
  - RussellABrown dynamic self balancing k-d tree github implementation
  - "kd-tree" SoA structure of arrays cache-friendly memory layout point cloud
  - nanoflann issue dynamic adaptor slow performance KDTreeSingleIndexDynamicAdaptor
  - Bkd-tree dynamic scalable kd-tree Procopiuc Agarwal logarithmic method
  - Eigen Vector3f kd-tree alignment SIMD nanoflann adaptor
  - "kd-tree" leaf bucket multiple points splitting strategy build performance
  - FLANN library maintenance status PCL deprecated alternative 2024 2025
- **Sources consulted:** 22 primary references (peer-reviewed papers, library docs, lecture notes, GitHub repositories) plus several dozen secondary search results.
- **Time-bounded scope:** Library status pinned as of May 2026; nanoflann release v1.9.0 (2025-12-22) is the most recent referenced. PCL release 1.15.1 is the most recent referenced. ikd-tree code patterns explicitly excluded per user instruction.
- **Revision notes:**
  - 2026-05-04: Updated to reflect locked workload constraints (FIFO ordering, single-writer concurrency, `resolution`-based dedup, batched insert/remove, single-query searches, $N \in [10^5, 10^6]$). Added §F11 (`resolution` invariant), §F12 (worst-case rebuild cost), batched-mutation anti-patterns.
  - 2026-05-05: Stripped prescriptive code-design content. Removed concrete C++ struct/class definitions, the public API signature block, the per-stage insert/remove pipeline framed as "the algorithm to implement", and the constants block presented as starting values. Reframed those constants as data points from the literature. Added an explicit OSS survey section. Added an Open Questions section. The report is now strictly informational, leaving API and code shape to a separate code-design step.
