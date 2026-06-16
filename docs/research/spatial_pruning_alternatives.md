# Spatial Pruning Alternatives for a Fixed-Capacity SLAM kd-Tree

> Research date: 2026-05-12 · Slug: `spatial-pruning-alternatives`

## TL;DR
None of the three named variants (min/max kd-tree, implicit kd-tree, relaxed
kd-tree) is a drop-in replacement for per-node AABB caching in a mutable SLAM
workload. **A hybrid (derived partition AABB during descent + per-leaf-bucket
data AABB) is the strongest match for the existing design**; the only credible
"smaller than full bbox" alternative worth prototyping is **per-node BIH-style
two-plane bounds** (8 B vs 24 B for $D=3$, $f32$). If `box_delete` and
`delete_outside_radius` are routinely large (cull most of the map), even the
naive per-node AABB pays for itself — the question is whether you want to spend
the bytes on the hot kNN path.

## Context and scope
- Target: `copse`, fixed-capacity, mutable, FIFO eviction, 32 B `TreeNode`
  in flat `std::vector` indexed by `uint32`, scapegoat-style partial rebuild.
- $D = 3$, point counts 100 k–1 M, dominant query = kNN/radius search;
  spatial deletes are needed but less frequent.
- Out of scope: GPU kd-trees, external-memory variants beyond a brief mention,
  approximate NN trees not relevant to exact spatial deletion.

## Findings

### 1. Min/max kd-tree
- **Definition.** kd-tree where every internal node caches the
  $(\min, \max)$ of a *scalar attribute* over its subtree; leaves' values are
  derived from parents using two extra bits per node so leaf storage is
  amortized. Originated in volume rendering for empty-space skipping in
  isosurface ray casting and MIP. [HIGH]
  [Wikipedia: Min/max kd-tree][1]
- **What it prunes.** A *single scalar field* (density, intensity), not
  geometric extent. Pruning condition: skip if isovalue $\notin [\min, \max]$.
- **Spatial pruning fit.** It does not bound the *positions* of points; it
  bounds an attribute. To use it for spatial pruning you would instantiate it
  $D$ times — once per coordinate axis — which collapses to caching a per-node
  AABB ($2D$ floats). No storage saving over the naive solution. [UNVERIFIED]
- **Mutability.** The classical construction is static; updates require
  recomputing $(\min, \max)$ along the path to the root. That part is cheap
  (fits a scapegoat-rebuild model), but does not change the fundamental
  observation above.

### 2. Implicit kd-tree
- **Definition.** kd-tree whose split-plane positions and orientations are
  *not stored*; they are recovered by a recursive splitting function defined
  over a rectilinear grid. Pointers to children are also implicit (Eytzinger /
  heap layout: $\text{left}(i) = 2i+1$). [HIGH]
  [Wikipedia: Implicit k-d tree][2]
- **Storage.** $O(n)$ bytes for *attribute* data only (e.g. min/max scalar);
  zero bytes for split planes or pointers if the tree is *complete*.
- **Hard constraint: rectilinear grid.** Splits land on grid planes by
  construction. Arbitrary point clouds do not satisfy this; you would have to
  bucket points into a fixed grid first, at which point you have a voxel grid
  with a tree on top — i.e. iVox / Faster-LIO territory, not a kd-tree
  problem. [HIGH]
- **Mutability.** Static. Implicit pointer layout (heap indexing) is fragile
  under deletion: removing a non-leaf node shifts indices of its descendants.
  Incompatible with scapegoat partial rebuild as currently designed. [HIGH]
- **Verdict for SLAM.** Not applicable to unstructured point clouds; useful
  vocabulary if you ever want a *secondary* index over a fixed voxel grid.

### 3. Relaxed kd-tree
- **Definition.** kd-tree where the discriminant axis at each node is
  *arbitrary* rather than cycling $\bmod\ D$ or chosen by a heuristic. The
  splitting key is whatever value happened to land at that node when it was
  inserted root-down. Introduced by Duch, Estivill-Castro, Martínez (1998).
  [HIGH] [Wikipedia: Relaxed k-d tree][3]
- **What it gives you.** Insertion in expected $O(\log n)$ with no balancing
  effort and no need to track which axis to split on at each level —
  the discriminant is a per-node 2-bit field. Standard rebalancing techniques
  (randomized, scapegoat) extend cleanly because the structure is simply a
  random BST in $D$ dimensions.
- **What it does *not* give you.** Anything new for spatial pruning. Pruning
  during search still needs either per-node bounds or partition bounds derived
  during descent — *exactly the same options* as a strict kd-tree. The
  literature treats the variant as a balancing/insertion convenience, not a
  pruning improvement. [HIGH]
- **Verdict.** Orthogonal to the bbox question. Worth knowing about if you
  ever consider switching the insertion policy, but it does not reduce per-node
  storage for deletes.

### Other variants encountered

- **BIH (Bounding Interval Hierarchy)** — Wächter & Keller, 2006. Stores **2
  scalar planes per node** (one per child, both along the same axis): the
  upper bound of the left child along axis $a$ and the lower bound of the
  right child along the same axis. Children may overlap (BVH-like) but remain
  ordered along $a$ (kd-like). Per-node cost: $2 \times 4 = 8\ \text{B}$ +
  2 bits for axis, vs $24\ \text{B}$ for a full $D=3$ AABB. Pruning is
  tighter than partition-bbox on the split axis but loose on the other two
  axes. [HIGH] [Wikipedia: BIH][4], [Wächter & Keller PDF][5]
- **BBD tree (Arya & Mount, 1998)** — used by the canonical ANN library.
  Each node owns a *cell* that is either a box or a box-minus-an-inner-box
  ("shrink" operation), guaranteeing geometric cell shrinkage. Provides
  $C_{d,\epsilon} \le \lceil 1 + 6d/\epsilon \rceil^d$ leaf-visit bound for
  approximate NN. Stores per-node bounds explicitly; no storage win, just
  better balance guarantees. Construction is essentially static; not designed
  for incremental updates. [HIGH] [Arya–Mount JACM 1998][6]
- **K-D-B tree (Robinson, 1981)** — B-tree-of-kd-tree-pages for
  external-memory range queries. Per-page region descriptors are full AABBs.
  Optimized for disk I/O; not relevant in-memory at $\sim$1 M points. [HIGH]
  [Robinson 1981][7]
- **Bkd-tree (Procopiuc, Agarwal, Arge, Vitter, 2003)** — logarithmic-method
  trick: maintain $O(\log n)$ static kd-trees of geometrically increasing
  sizes; rebuild on overflow. Strong amortized insert performance with
  preserved space utilization. Per-static-tree storage is conventional.
  Conceptually adjacent to scapegoat-rebuild. [HIGH] [Procopiuc et al. 2003][8]
- **ikd-Tree (Cai, Xu, Zhang, 2021; FAST-LIO2)** — *does* cache per-node AABB
  (`node_range_x/y/z`, 24 B) precisely to make box-wise delete cheap. Total
  node size is $\sim$200+ B (mutex, four bool flags, three pointers, parent
  pointer, alpha counters). Validates that *for SLAM box-delete, per-node
  AABB is the established choice* — the cost was deemed worth paying.
  [HIGH] [ikd-Tree repo][9]
- **iVox (Faster-LIO, Bai et al. 2022)** — gave up on kd-trees entirely:
  spatial-hash voxel grid + LRU eviction. $O(1)$ insert; $O(m)$ kNN over
  $m \in \{7, 27\}$ voxel neighbors. Box-delete becomes "drop voxels whose
  centers lie outside the box". Different design point; relevant only if you
  are willing to reconsider the kd-tree premise. [HIGH] [Bai et al. RA-L 2022][10]

## Trade-offs

| Approach | Bytes/internal node (D=3, f32) | Pruning tightness | Mutable? | Notes |
|---|---|---|---|---|
| **Per-node AABB** (baseline) | +24 | tight | yes | what ikd-Tree ships |
| **Derived partition AABB** | +0 | loose at leaves | yes | descent stack carries 6 floats |
| **Hybrid: derived + per-leaf-bucket AABB** | +24 / leaf bucket only | tight at leaves, loose at internals | yes | bucket size $B \Rightarrow$ overhead amortized $\approx 24/B$ B/point |
| **BIH (2-plane)** | +8 | tight on split axis, loose elsewhere | yes (bounds updateable) | one axis-bit + two floats |
| **Centroid + radius** | +16 | very loose for box queries | yes | best for sphere queries; bad for AABB queries |
| **Min/max kd-tree (per-axis × 3)** | +24 | tight (= AABB) | yes (path update) | identical to AABB; no win |
| **Implicit kd-tree** | +0 (split) + attr | tight on grid only | **no** | requires rectilinear grid |
| **Relaxed kd-tree** | +1 axis byte, no bounds | depends on what you pair it with | yes | orthogonal to pruning |

## Anti-patterns

- **Adopting an implicit (heap-indexed) layout for a mutable tree.** Insertion
  and rotation invalidate Eytzinger indices; rebuild cost is global rather
  than scapegoat-local. Implicit trees pay off only for static spatial fields.
- **Bounding sphere as a substitute for AABB on axis-aligned queries.** A
  sphere of radius $r$ around centroid covers $\pi r^2$ in 2D / $\frac{4}{3}\pi r^3$
  in 3D, but the inscribed AABB is $\sqrt{D}$ times tighter on each axis for
  axis-aligned cluster shapes typical of SLAM voxelized clouds. Centroid+radius
  is reported in the literature as worse than AABB for kNN with axis-aligned
  splits. [UNVERIFIED — inferential from BVH-vs-sphere comparisons]
- **Pruning purely from split-plane half-spaces (no extent cache) for radius
  delete.** Works for kNN but produces unnecessarily deep traversal for
  `delete_outside_radius` because partition cells extend to $\pm\infty$ along
  unsplit axes; you visit every node whose partition touches the unbounded
  exterior. ikd-Tree explicitly caches `node_range_*` to avoid this. [HIGH]
- **Conflating `box_delete` correctness with bbox pruning.** Even with no
  bbox cache, the deletion is correct; bbox is purely a *traversal* prune.
  Don't introduce 24 B/node before measuring whether traversal is the
  bottleneck.

## Recommendation

Given the constraints — fixed capacity, 32 B node target, mutable with FIFO
eviction, scapegoat partial rebuild, primary cost on the kNN/radius path —
**pursue the hybrid: derived partition AABB on the descent stack for internal
nodes, plus a single AABB per leaf bucket** stored alongside the bucket (not
inside `TreeNode`). Concretely:

- Internal `TreeNode` stays at 32 B; the recursion (or stack-encoded
  iterative traversal) carries 6 floats of partition bounds, refined by each
  split. No memory cost on the hot path.
- Each leaf bucket already holds $B$ points; appending one $24\ \text{B}$ AABB
  amortizes to $24/B$ B/point ($\le 1$ B/point for $B \ge 32$). Update cost
  on insert/delete is $O(B)$ in the worst case but typically $O(1)$ via lazy
  recomputation flagged when the bucket changes.
- This gives you tight pruning exactly where the partition-bbox approach is
  loosest (at leaves), avoids inflating the hot-path node, and stays
  compatible with scapegoat rebuilds.

**Conditions to revisit.** If profiling shows that internal-node pruning is
the dominant cost during `box_delete` over very large culled regions (e.g.
discarding 60%+ of the map per call), measure **BIH-style 8 B per-node
bounds** before committing to a full per-node AABB. If `box_delete` is rare
and small compared to kNN traffic, skip even the leaf AABB and rely purely on
derived partition bounds.

**When to abandon kd-trees.** If `delete_outside_radius` runs every scan and
covers most of the map, the iVox / hash-voxel approach (Faster-LIO) is a
strictly better fit than any kd-tree variant; this is a workload question,
not a data-structure-tuning question.

## Open questions

- Empirical: what fraction of `box_delete` / `delete_outside_radius` calls
  actually prune at the internal-node level vs the leaf level for the target
  workload? Determines whether internal-node bounds (BIH or AABB) earn their
  bytes.
- Cache behavior: at $D=3, f32$, does an 8 B BIH triple (axis byte + 2
  floats, padded to 12 B) actually improve cache density vs $24$ B AABB once
  alignment and the existing 32 B node are accounted for? Needs a microbench.
- Update cost of leaf-bucket AABB under FIFO eviction: how often does the
  oldest-point eviction touch the same bucket repeatedly within one scan?

## References
1. Min/max kd-tree — https://en.wikipedia.org/wiki/Min/max_kd-tree — accessed 2026-05-12.
2. Implicit k-d tree — https://en.wikipedia.org/wiki/Implicit_k-d_tree — accessed 2026-05-12.
3. Relaxed k-d tree — https://en.wikipedia.org/wiki/Relaxed_k-d_tree — accessed 2026-05-12.
4. Bounding interval hierarchy — https://en.wikipedia.org/wiki/Bounding_interval_hierarchy — accessed 2026-05-12.
5. Wächter, C., Keller, A. *Instant Ray Tracing: The Bounding Interval Hierarchy*, EGSR 2006 — http://ainc.de/Research/BIH.pdf — accessed 2026-05-12.
6. Arya, S., Mount, D. M., Netanyahu, N. S., Silverman, R., Wu, A. Y. *An Optimal Algorithm for Approximate Nearest Neighbor Searching in Fixed Dimensions*, JACM 45(6) 1998 — https://www.cse.ust.hk/faculty/arya/pub/JACM.pdf — accessed 2026-05-12.
7. Robinson, J. T. *The K-D-B-tree: A search structure for large multidimensional dynamic indexes*, SIGMOD 1981 — https://dl.acm.org/doi/10.1145/582318.582321 — accessed 2026-05-12.
8. Procopiuc, O., Agarwal, P. K., Arge, L., Vitter, J. S. *Bkd-Tree: A Dynamic Scalable kd-Tree*, SSTD 2003 — https://users.cs.duke.edu/~pankaj/publications/papers/bkd-sstd.pdf — accessed 2026-05-12.
9. Cai, Y., Xu, W., Zhang, F. *ikd-Tree: An Incremental K-D Tree for Robotic Applications*, arXiv:2102.10808, 2021 — https://github.com/hku-mars/ikd-Tree — accessed 2026-05-12.
10. Bai, C., Xiao, T., Chen, Y., Wang, H., Zhang, F., Gao, X. *Faster-LIO: Lightweight Tightly Coupled LiDAR-Inertial Odometry Using Parallel Sparse Incremental Voxels*, RA-L 2022 — https://github.com/gaoxiang12/faster-lio — accessed 2026-05-12.
11. Wald, I., Havran, V. *On building fast kd-Trees for Ray Tracing, and on doing that in O(N log N)*, RT 2006 — https://www.irisa.fr/prive/kadi/Sujets_CTR/kadi/Kadi_sujet2_article_Kdtree.pdf — accessed 2026-05-12.
12. Hapala, M., Havran, V. *Review: Kd-tree Traversal Algorithms for Ray Tracing*, CGF 2011 — https://dcgi.fel.cvut.cz/home/havran/ARTICLES/cgf2011.pdf — accessed 2026-05-12.

[1]: https://en.wikipedia.org/wiki/Min/max_kd-tree
[2]: https://en.wikipedia.org/wiki/Implicit_k-d_tree
[3]: https://en.wikipedia.org/wiki/Relaxed_k-d_tree
[4]: https://en.wikipedia.org/wiki/Bounding_interval_hierarchy
[5]: http://ainc.de/Research/BIH.pdf
[6]: https://www.cse.ust.hk/faculty/arya/pub/JACM.pdf
[7]: https://dl.acm.org/doi/10.1145/582318.582321
[8]: https://users.cs.duke.edu/~pankaj/publications/papers/bkd-sstd.pdf
[9]: https://github.com/hku-mars/ikd-Tree
[10]: https://github.com/gaoxiang12/faster-lio

---

## Research metadata
- Date: 2026-05-12
- Search queries used:
  - "min-max kd-tree volume rendering early ray termination"
  - "implicit kd-tree heap layout memory efficient"
  - "relaxed kd-tree Duch Estivill-Castro Martinez"
  - "ikd-tree FAST-LIO incremental kd-tree LiDAR SLAM data structure"
  - "BBD tree balanced box decomposition Arya Mount approximate nearest neighbor"
  - "iVox Faster-LIO voxel hash map alternative ikd-tree"
  - "bounding interval hierarchy BIH kd-tree two split planes per node memory"
  - "kdb-tree spatial database multidimensional Robinson"
  - "Bkd-tree Procopiuc Agarwal dynamic kd-tree bulk-loaded"
  - "PCL FLANN nanoflann kd-tree node bounding box storage memory layout"
- Sources consulted: Wikipedia (min/max, implicit, relaxed kd-tree, BIH),
  Wächter & Keller 2006 (BIH), Arya & Mount 1998 (BBD/ANN), Robinson 1981
  (K-D-B), Procopiuc et al. 2003 (Bkd), Cai et al. 2021 (ikd-Tree, source
  code header), Bai et al. 2022 (iVox/Faster-LIO), Wald & Havran 2006,
  Hapala & Havran 2011.
- Time-bounded scope: SLAM-relevant variants, $D \le 3$, in-memory mutable
  trees with $\sim 10^5$–$10^6$ points. External-memory and GPU variants only
  named, not analyzed.
