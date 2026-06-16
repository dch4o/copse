# Integrating HKU MARS ikd-Tree for a Comparative Microbenchmark

> Research date: 2026-06-16 · Slug: `ikd-tree-benchmark`
> Pinned commit: `c0e36a16b6e4d557d3783b16911207f6398dd478` (HEAD of `master` at clone time)

## TL;DR

The core `KD_TREE<PointType>` **does NOT require PCL** — PCL only appears as a
header include (`#include <pcl/point_types.h>`, for `Eigen::aligned_allocator`)
and as three `pcl::Point*` explicit template instantiations at the bottom of
`ikd_Tree.cpp`. The shipped, PCL-free `ikdTree_PointType {float x,y,z;}` struct
is already a first-class point type (the demo uses it, and the file already
instantiates `KD_TREE<ikdTree_PointType>`). **A PCL-free compile was VERIFIED
inside the container** with exactly two source tweaks: swap the header include
to `<Eigen/Core>`, and drop the three `pcl::` instantiation lines. The library
is **hard-wired 3D** (`node_range_z`, `division_axis == 2`, `point_cmp_z`,
fixed-size `[3]` box arrays) — **the comparison is D=3 only.** Recommended
integration: **`FetchContent_Declare` pinned to the commit + a manually-defined
`OBJECT`/`STATIC` target over `ikd_Tree.cpp`**, with the PCL-free tweaks applied
either via a tiny patch step or a thin wrapper header. The biggest fairness
caveats are (1) ikd-Tree's deletes are **lazy/tombstone** like ours but its
*default* `Add_Points` path runs an **internal downsample** that must be turned
OFF to match a fair "raw insert", and (2) ikd-Tree starts a **background
rebuild pthread in its constructor unconditionally** — wall-clock vs CPU-time
must both be reported. **License is GPLv2** — relevant if benchmark binaries are
ever distributed.

## Context and scope

- Goal: stand up ikd-Tree alongside `copse::KDTree<Dim>` so the existing
  TODO slice (`docs/TODO.md` → "ikd-tree comparative benchmark") can measure
  per-batch insert latency, kNN throughput, radius/box search, spatial delete,
  and resident memory at N ∈ {10k, 100k, 1M}, across three run modes:
  copse (single-thread) | ikd-tree single-thread (BG rebuild off) |
  ikd-tree default (BG rebuild on).
- This is the **research stage only**: findings, not design. No source/CMake
  changes, no `third_party/`. A separate design stage decides the integration.
- Environment (verified in-container, name `admiring_payne`): gcc-13.3,
  cmake 3.31, ninja, Eigen at `/usr/local/include/eigen3`, pthread present,
  **PCL absent**, no TBB, GitHub reachable.
- `copse::KDTree<Dim>` API read from `include/copse/kd_tree.hpp`; point
  type is `Eigen::Matrix<float, Dim, 1>` (`impl/point_traits.hpp`).

## Findings

### 1. PCL dependency — the gating question [HIGH, verified by compile + run]

**Where PCL actually appears:**

- `ikd_Tree.h:11` — `#include <pcl/point_types.h>`. This is the *only* PCL
  surface in the header. It is pulled in for `Eigen::aligned_allocator`
  (`ikd_Tree.h:62`: `using PointVector = vector<PointType, Eigen::aligned_allocator<PointType>>`),
  which `pcl/point_types.h` transitively provides — **there is no explicit
  `#include <Eigen/...>` anywhere in the two files.**
- `ikd_Tree.cpp:1447–1449` — three explicit instantiations:
  ```cpp
  template class KD_TREE<pcl::PointXYZ>;
  template class KD_TREE<pcl::PointXYZI>;
  template class KD_TREE<pcl::PointXYZINormal>;
  ```
- `ikd_Tree.cpp:1446` — the PCL-free one is **already there**:
  ```cpp
  template class KD_TREE<ikdTree_PointType>;
  ```
- The remaining textual "PCL" hits (`PCL_Storage`, `Rebuild_PCL_Storage`) are
  just member *variable names*, not PCL types.

**What the core actually needs from a point type.** The algorithm only ever
touches `.x`, `.y`, `.z` (44 references each in the `.cpp`) and default-
constructs the point. No `.intensity`, `.curvature`, `.normal_*`, `.data[]`.
The bundled struct satisfies this exactly:
```cpp
struct ikdTree_PointType {
    float x, y, z;
    ikdTree_PointType(float px = 0.0f, float py = 0.0f, float pz = 0.0f) { x=px; y=py; z=pz; }
};
```

**Verified PCL-free compile (inside `admiring_payne`).** Two tweaks only:

1. In the header, replace the PCL include with Eigen:
   ```diff
   - #include <pcl/point_types.h>
   + #include <Eigen/Core>
   ```
2. In the `.cpp`, remove the three `pcl::` instantiation lines (keep the
   `ikdTree_PointType` one), and point its `#include` at the patched header.

Exact commands that produced an object file and a passing end-to-end driver:

```bash
# (patched copies created via sed; originals untouched)
#   ikd_Tree_nopcl.h  = header with <pcl/point_types.h> -> <Eigen/Core>
#   ikd_Tree_nopcl.cpp = cpp including the patched header, pcl:: instantiations deleted

g++ -std=c++14 -O2 -pthread -I/usr/local/include/eigen3 \
    -c ikd_Tree_nopcl.cpp -o ikd_Tree_nopcl.o        # exit 0; 77 KB object
```

A driver that calls `Build`, `Add_Points(.,false)`, `Nearest_Search`,
`Radius_Search`, `Box_Search`, `Delete_Points`, `Delete_Point_Boxes`,
`tree_range`, `size`, `validnum` **linked and ran clean** (output below). The
only nonstandard linker requirement is `-pthread` (the rebuild thread).

```
Multi thread started
after Build: size=5000 validnum=5000
after Add_Points: added=0 size=5500           # added counts downsample ops only; size grew
kNN found=5 first_sqdist=0.0840
radius<=1.0 count=29
box[-1,1]^3 count=54
after Delete_Points(100): size=5500 validnum=5400   # lazy delete: size unchanged, validnum drops
Delete_Point_Boxes removed=53 validnum=5347
tree_range x[-5.00,5.00]
DRIVER OK
Rebuild thread terminated normally
```

**One non-obvious gotcha discovered during verification.** A `KD_TREE` object
is **~42 MB** — `sizeof(KD_TREE<ikdTree_PointType>) = 44,000,464` — because it
embeds `MANUAL_Q<Operation_Logger_Type> Rebuild_Logger` whose backing store is
a *fixed* `T q[Q_LEN]` with `Q_LEN = 1000000` and
`sizeof(Operation_Logger_Type) = 44`. **Do not allocate the tree on the
stack** — a stack local segfaults immediately (verified: ASan reports a
stack-overflow). Heap-allocate it (e.g. `std::make_shared<KD_TREE<...>>(...)`
or a free-store `new`/`unique_ptr`). The benchmark harness must account for
this fixed ~42 MB overhead in any memory comparison (see §6, §7).

**Conclusion:** PCL is **not** required. The web folklore that "removing PCL
needs large modifications" (ikd-Tree issues #20, #31) does not hold for the
*core* tree — those threads concern custom point types carrying extra payload
fields and SOA/Eigen-alignment subtleties, not the bare x/y/z core that this
benchmark needs.

### 2. API surface mapped to our workloads [HIGH, from `ikd_Tree.h` / `.cpp`]

All signatures verbatim; `PointVector = vector<PointType, Eigen::aligned_allocator<PointType>>`.

| Our workload | ikd-Tree method (exact signature) | Notes |
|---|---|---|
| Bulk build | `void Build(PointVector point_cloud)` | Takes the cloud **by value**; median-split, longest-axis. Wipes any existing tree. |
| Incremental per-batch insert | `int Add_Points(PointVector & PointToAdd, bool downsample_on)` | Point-by-point internally. **Pass `downsample_on = false`** for raw insert parity. Return value counts *downsample operations*, not points added (0 when downsample off — points still inserted). |
| kNN | `void Nearest_Search(PointType point, int k_nearest, PointVector &Nearest_Points, vector<float> & Point_Distance, double max_dist = INFINITY)` | `Point_Distance` are **squared** distances. `max_dist` is a *linear* radius cap (squared internally). Results sorted nearest-first. Allocates a `MANUAL_HEAP(2*k_nearest)` per call. |
| Radius / range (sphere) | `void Radius_Search(PointType point, const float radius, PointVector &Storage)` | `radius` is **linear**; internal compare is `<= radius*radius`. No distances returned, points only. |
| Box / range (AABB) | `void Box_Search(const BoxPointType &Box_of_Point, PointVector &Storage)` | Half-open box: `min <= p < max` per axis (`vertex_min[i] <= p && vertex_max[i] > p`). |
| Point delete | `void Delete_Points(PointVector & PointToDel)` | **Lazy/tombstone**: matches by `same_point` (L∞ < `EPSS=1e-6`); sets `point_deleted`; `size()` unchanged, `validnum()` drops. |
| Box delete | `int Delete_Point_Boxes(vector<BoxPointType> & BoxPoints)` | Lazy; returns count of points marked deleted. Subtree-wide tombstone when a node range is fully covered. |
| Box re-insert (un-delete) | `void Add_Point_Boxes(vector<BoxPointType> & BoxPoints)` | Reactivates tombstoned points in the box. No direct `copse` analog. |
| Tree extent | `BoxPointType tree_range()` | Root AABB. |
| Counts | `int size()`, `int validnum()` | `size` = total nodes (incl. tombstones); `validnum` = live points. |
| Collect removed | `void acquire_removed_points(PointVector & removed_points)` | Drains the deleted-points cache; only populated on actual node reclamation during rebuild. |

**Constructor / config:**
```cpp
KD_TREE(float delete_param = 0.5, float balance_param = 0.6, float box_length = 0.2);
void InitializeKDTree(float delete_param = 0.5, float balance_param = 0.7, float box_length = 0.2);
void Set_delete_criterion_param(float delete_param);
void Set_balance_criterion_param(float balance_param);
void set_downsample_param(float box_length);   // = the downsample voxel edge
```
- `balance_param` is ikd-Tree's α (default 0.6 via ctor; **our default is 0.7**
  — to compare apples-to-apples set ikd-Tree's `balance_param = 0.7`).
- `delete_param` is the tombstone-fraction rebuild trigger (default 0.5; our
  `Config::tombstone_threshold` default is 0.25 — choose one and match both).
- `box_length` is the downsample voxel edge, **only consulted when
  `Add_Points(., downsample_on=true)`** (see §7). It is *not* an insert-time
  dedup like our `Config::resolution`; see fairness note.

**Downsampling behavior.** `Add_Points` runs the voxel-downsample branch only
when `downsample_on && DOWNSAMPLE_SWITCH` (`DOWNSAMPLE_SWITCH` is a compile-time
`#define true`). With `downsample_on=false` the branch is fully skipped — every
input point is inserted raw. For a fair "insert N points" comparison, **call
`Add_Points(batch, false)`**. If we instead want to mirror our
`Config::resolution` dedup, that is a *different* semantic (voxel snap-to-grid
vs. nearest-within-radius first-seen) and is documented as a mismatch in §7.

### 3. Background-rebuild control — exact knobs [HIGH, verified]

ikd-Tree's rebuild is decided in `Rebuild(KD_TREE_NODE ** root)` (`.cpp:625`):

```cpp
if ((*root)->TreeSize >= Multi_Thread_Rebuild_Point_Num) {
    ... hand the subtree to the background thread via Rebuild_Ptr ...
} else {
    ... rebuild this subtree synchronously, in-place (flatten + BuildTree) ...
}
```

- **`#define Multi_Thread_Rebuild_Point_Num 1500`** (`ikd_Tree.h:15`) is the
  single gate. Subtrees ≥ 1500 nodes are rebuilt off-thread; smaller ones
  synchronously.
- **Mode (a): single-threaded, BG rebuild OFF** — set this macro to `INT_MAX`
  (`2147483647`) before compiling `ikd_Tree.cpp`. Every rebuild then takes the
  synchronous else-branch. **Verified**: a variant compiled with
  `#define Multi_Thread_Rebuild_Point_Num 2147483647` produces byte-identical
  driver output and never hands work to the thread.
  - Caveat: the rebuild **pthread is still created** in the constructor
    (`KD_TREE()` → `start_thread()` → `pthread_create`, unconditional) and
    idle-spins (`usleep(100)` loop) doing nothing. To *also* eliminate the
    thread entirely (cleanest "pure single-thread" mode) you must additionally
    no-op `start_thread()`/`stop_thread()` — a small source edit. For wall-
    clock fairness the idle thread is harmless (it never holds a lock during
    operations once the macro is maxed), but the design stage should decide
    whether to also stub the thread for a truly clean baseline.
- **Mode (b): default, BG rebuild ON** — leave `Multi_Thread_Rebuild_Point_Num`
  at `1500`. The constructor starts the thread; large-subtree rebuilds run
  concurrently. This is the as-shipped FAST-LIO2 configuration.
- There is **no runtime setter** for the macro — it is a compile-time `#define`.
  Supporting both modes therefore means **two compilations** of `ikd_Tree.cpp`
  (e.g. two object targets with different `-D` overrides), or one build plus a
  source-level switch. Note: the macro is `#define`'d, so a plain `-D` on the
  command line will collide with the in-header definition; the clean route is a
  patched header per mode (the design stage's call).

Related, non-gating knobs:
- `ForceRebuildPercentage 0.2`, `Minimal_Unbalanced_Tree_Size 10` (`.h:13–17`).
- `Criterion_Check` triggers a rebuild when `invalid_point_num/TreeSize >
  delete_criterion_param` (tombstone fraction) **or** the heavier child's
  fraction exits `[1-α, α]` (imbalance).

### 4. Dimensionality — hard-wired 3D [HIGH]

ikd-Tree is **3D only**, structurally:

- Per-node range is three named pairs: `node_range_x[2]`, `node_range_y[2]`,
  `node_range_z[2]` (`ikd_Tree.h:79`).
- `BoxPointType` is `float vertex_min[3]; float vertex_max[3];` (`.h:32`).
- Build picks the longest of exactly three axes; split uses `point_cmp_x/y/z`
  (`.cpp:585–613`); descent branches on `division_axis ∈ {0,1,2}` reading
  `point.x/.y/.z` (`.cpp:727, 833`).
- `division_axis = (father_axis + 1) % 3` on incremental insert (`.cpp:823`).

**Consequence for the benchmark:** the comparison is **D=3 only**. Our 2D/4D
configurations have no ikd-Tree counterpart. The TODO already scopes the
comparison to SLAM (3D), so this is consistent — but the report should state it
explicitly and not run 2D/4D rows against ikd-Tree.

### 5. Integration mechanism — options & facts [HIGH]

ikd-Tree ships **no CMake package or installable target** — its top-level
`CMakeLists.txt` is a PCL demo driver (`find_package(PCL 1.8 REQUIRED)`, three
`add_executable`s) and is **not** what we'd consume. The consumable unit is two
loose files: `ikd-Tree/ikd_Tree.h` and `ikd-Tree/ikd_Tree.cpp`. **Exactly those
two files must compile** (the `examples/` and `documents/` are irrelevant).

Two viable mechanisms:

**Option A — `FetchContent_Declare` pinned to the commit + a hand-written target.**
```cmake
FetchContent_Declare(ikd_tree
  GIT_REPOSITORY https://github.com/hku-mars/ikd-Tree.git
  GIT_TAG        c0e36a16b6e4d557d3783b16911207f6398dd478)   # pinned
FetchContent_MakeAvailable(ikd_tree)   # or _Populate to avoid its CMakeLists
# then define our own target over ${ikd_tree_SOURCE_DIR}/ikd-Tree/ikd_Tree.cpp
```
- Pros: source not vendored into the repo; pin is explicit; upstream history
  reachable. Matches the existing Eigen/Catch2 dependency style.
- Cons: requires `FetchContent_Populate` (not `MakeAvailable`) or `SOURCE_SUBDIR`
  tricks so ikd-Tree's PCL-demo `CMakeLists.txt` is **not** added. Needs network
  at configure time. The **PCL-free tweaks** (§1) must still be applied — either
  by a `patch`/`file(READ|WRITE)` step or by compiling a thin shim that
  `#define`s/`#include`s around the include (see below).
- GPLv2 (see §License): fetching at build time, not redistributing source, is
  the lowest-friction posture.

**Option B — vendor the two files under `third_party/ikd-Tree/`.**
- Pros: hermetic, offline builds, the PCL-free edits live in-tree and are
  reviewable; no configure-time network. The TODO text itself says "Vendor or
  FetchContent ikd-tree under `third_party/`".
- Cons: copies GPLv2 source into the repo — **must retain the upstream LICENSE
  and a provenance note (commit hash)**; GPLv2's copyleft attaches to anything
  *distributed* that links it (benchmark binaries), though an internal,
  non-distributed benchmark is unaffected. Drift from upstream is manual.

**Applying the PCL-free tweaks under either option.** Three sub-options:
1. A 2-line patch file applied at configure/build (`<pcl/point_types.h>` →
   `<Eigen/Core>`; delete the three `pcl::` instantiations).
2. A wrapper TU that does **not** edit upstream: pre-include `<Eigen/Core>`,
   then `#define` a stub so the PCL header is skippable — fragile, not
   recommended (the include is unconditional).
3. Compile the unmodified `.cpp` but provide a fake `pcl/point_types.h` on the
   include path that only pulls in `<Eigen/Core>` — keeps upstream byte-for-byte
   but adds a shim header. Clean and patch-free.

(The decision among A/B and 1/2/3 is the design stage's. Facts only here.)

**Compile requirements recap:** `-std=c++14` minimum (the upstream uses C++14;
it compiles fine under newer standards too — our project is C++20+, no
conflict), `-pthread`, `-I` to Eigen. No other deps.

### 6. Memory-footprint measurement [HIGH for the model; method is a menu]

**ikd-Tree storage model.** Pointer-based tree, one heap `KD_TREE_NODE` per
point. **`sizeof(KD_TREE_NODE) = 136 bytes`** (measured): the point (12 B),
`division_axis`, several `int` counters (`TreeSize`, `invalid_point_num`,
`down_del_num`), seven `bool` flags, `radius_sq`, **a per-node
`pthread_mutex_t push_down_mutex_lock` (~40 B on glibc)**, three range pairs
(`node_range_{x,y,z}[2]` = 24 B), three child/parent pointers (24 B), and two
`float` alpha fields. So a tree of N live points costs **≈ 136·N bytes of node
storage** plus malloc overhead per node (another ~16–32 B/node typical), i.e.
roughly **150–170 B/point** resident. Tombstoned points still occupy nodes until
a rebuild reclaims them, so live-vs-resident diverges under churn.

On top of per-node storage, **each `KD_TREE` object carries a fixed ~42 MB**
(`Rebuild_Logger`'s `Operation_Logger_Type q[1000000]`, §1). This is constant,
not per-point — it dominates at N=10k but is amortized at N=1M. **Report it
separately** so the per-point slope is comparable.

**Our storage model (for contrast).** `copse::KDTree<3>` is a flat
SoA design: `PointStore` (contiguous `Eigen::Matrix<float,3,1>` = 12 B/point
plus a `uint32` generation and liveness bits), bucketed leaves, a flat
`std::vector<TreeNode>` (32 B/node, far fewer nodes than points because leaves
bucket B points), and `leaf_bboxes_`. No per-node mutex, no per-point pointer
triples. Expect **dramatically lower bytes/point** — this is likely a headline
result, so measure it carefully and fairly.

**How to measure resident footprint fairly (menu; design picks one):**
- **Process RSS delta** around the build/insert phase via
  `/proc/self/statm` or `getrusage(RUSAGE_SELF).ru_maxrss` — coarse but
  apples-to-apples at the OS level; captures malloc fragmentation and the
  fixed 42 MB. Subtract a baseline taken before constructing the tree.
- **`mallinfo2()` / jemalloc/tcmalloc stats** — heap bytes attributable to the
  allocator; finer than RSS, still counts the 42 MB.
- **Analytic model** — `node_count × sizeof(node) + container capacities`;
  exact but ignores allocator overhead; good as a cross-check.
- Recommended: report **both** an RSS-delta (truth-on-the-machine) and an
  analytic node-count estimate, and **state the fixed 42 MB ikd-Tree overhead
  explicitly** rather than letting it silently inflate small-N numbers.

### 7. Fairness caveats [HIGH — these will distort a naive comparison]

1. **Downsample vs. resolution-dedup.** ikd-Tree's `Add_Points(.,true)` snaps
   to a voxel grid of edge `box_length` and keeps the point nearest the voxel
   center, *deleting others in that voxel*. Our `Config::resolution` is a
   first-seen nearest-within-radius reject. These are **different point sets**
   for the same input. For a clean insert comparison, run ikd-Tree with
   `downsample_on=false` and **either** disable our dedup **or** accept that the
   two "insert N" operations retain different counts — and say which.
2. **Delete semantics: both lazy, but reclamation differs.** Both tombstone on
   delete (our `release`, ikd-Tree's `point_deleted`). ikd-Tree reclaims dead
   nodes only on a rebuild triggered by `delete_criterion_param`; ours frees
   slots immediately for reuse and partial-rebuilds on `tombstone_threshold`.
   A delete-heavy workload measures **different things** (mark cost vs. mark +
   eventual restructure). Trigger thresholds (`delete_param` vs.
   `tombstone_threshold`, `balance_param` vs. `alpha`) **must be matched**
   (ikd defaults 0.5/0.6; ours 0.25/0.7) or the rebuild cadence differs.
3. **Point layout & precision.** Both are `float` x/y/z (12 B). ikd-Tree is
   AoS pointer-chasing nodes; we are SoA contiguous. Same precision, very
   different cache behavior — that *is* the thing being benchmarked, so this is
   a fair difference to surface, not correct away.
4. **Bulk vs. incremental build.** ikd-Tree `Build` is one-shot balanced;
   `Add_Points` is point-by-point with incidental rebuilds. Compare like with
   like: our `insert(span)` batch vs. ikd-Tree `Add_Points` for the streaming
   workload, and our `rebuild_all`/initial `insert` vs. ikd-Tree `Build` for
   the bulk workload — **don't** pit our batch insert against ikd-Tree `Build`.
5. **BG thread: wall-clock vs CPU time.** In default mode ikd-Tree offloads
   large rebuilds to a second core, so **wall-clock insert latency drops but
   total CPU work rises**. Single-thread copse pays rebuild cost inline.
   **Report both wall-clock and CPU time** (`getrusage` `ru_utime+ru_stime`, or
   `clock()` vs `chrono`), and run all three modes, so an algorithmic win is
   not confused with a free-second-core win. Also note: ikd-Tree's idle thread
   spins with `usleep(100)` — negligible, but pin the harness to leave a core
   free (per project ops rules) so the BG thread has somewhere to run.
6. **Fixed 42 MB overhead** (§1/§6) skews memory at small N; report per-point
   slope and the constant separately.
7. **kNN result contract.** ikd-Tree returns squared distances and a `max_dist`
   *linear* cap; our `Neighbor::sq_dist` is squared and `hybrid_search` takes a
   linear radius. Align query parameters (pass `max_dist` = our radius) and
   compare squared distances to squared distances.
8. **Box half-openness.** ikd-Tree `Box_Search`/box-delete use `min <= p < max`.
   Match our `delete_box` boundary convention in the harness, or boundary
   points cause spurious count mismatches.
9. **Tie-break determinism.** ikd-Tree's `PointType_CMP` tie-breaks equal
   distances by `point.x`; ours may differ. For *correctness* cross-checks
   (not timing) compare *sets* within ε, not ordered sequences.

## Trade-offs (integration mechanism)

| Option | Win | Cost |
|---|---|---|
| FetchContent @ commit + custom target | No vendored GPL source; explicit pin; matches Eigen/Catch2 style | Must suppress ikd's PCL demo `CMakeLists` (use `Populate`/`SOURCE_SUBDIR`); needs configure-time network; PCL-free patch applied out-of-tree |
| Vendor under `third_party/` | Hermetic/offline; edits reviewable in-tree; matches TODO wording | Copies GPLv2 into repo (keep LICENSE + provenance); copyleft attaches to *distributed* linked binaries; manual upstream drift |
| Two compilations (BG on / off) | Both run modes from one source | `Multi_Thread_Rebuild_Point_Num` is a `#define` → two patched headers or two object targets |

## Anti-patterns (observed / inferred)

- **Stack-allocating `KD_TREE`.** Instant stack overflow — the object is ~42 MB
  (`Rebuild_Logger` fixed array). Verified crash. Always heap-allocate.
  [HIGH, verified]
- **Trying to `-D Multi_Thread_Rebuild_Point_Num=...` on the command line.** The
  macro is `#define`'d in the header, so a command-line define collides /
  redefines. Patch the header (or shadow it) instead. [HIGH]
- **Comparing default ikd-Tree (BG on) against single-thread copse as if
  equal.** Conflates a concurrency win with an algorithmic one; the TODO
  explicitly demands all three modes for exactly this reason. [HIGH]
- **Leaving ikd-Tree downsample on while measuring "raw insert".** Silently
  drops points and changes the tree size — counts won't match and per-point
  timings are over a different N. [HIGH]
- **Pinning every core during the run.** Per project ops notes a frozen system
  when all cores were pinned; the BG-rebuild thread needs a free core in
  default mode. Leave 1–2 cores free. [HIGH, project ops rule]
- **Believing the README/web that PCL removal is hard.** True for *payload-
  carrying* custom point types (issues #20, #31), false for the bare x/y/z
  core needed here — verified compiles. [HIGH]

## Code examples

Minimal PCL-free instantiation + driver core (attribution: derived from the
upstream `examples/ikd_Tree_demo.cpp`, GPLv2). Heap-allocate; `-pthread`,
Eigen on the include path:

```cpp
#include "ikd_Tree.h"   // header patched: <pcl/point_types.h> -> <Eigen/Core>
#include <memory>

using PointType = ikdTree_PointType;          // {float x,y,z;}
using PointVector = KD_TREE<PointType>::PointVector;

auto tree = std::make_shared<KD_TREE<PointType>>(/*delete*/0.5f, /*balance*/0.7f, /*box*/0.2f);

PointVector cloud = /* ... */;
tree->Build(cloud);                            // bulk build
tree->Add_Points(batch, /*downsample_on=*/false);   // raw incremental insert

PointVector knn; std::vector<float> sq_d;      // sq_d are SQUARED distances
tree->Nearest_Search(PointType(0,0,0), 5, knn, sq_d /*, max_dist = linear cap*/);

PointVector inside;
tree->Radius_Search(PointType(0,0,0), /*linear radius*/1.0f, inside);

BoxPointType box{ {-1,-1,-1}, {1,1,1} };       // min <= p < max
tree->Box_Search(box, inside);

tree->Delete_Points(to_delete);                // lazy tombstone
std::vector<BoxPointType> boxes{box};
int n_marked = tree->Delete_Point_Boxes(boxes);
```

Single-thread (BG-off) build: compile `ikd_Tree.cpp` against a header where
```cpp
#define Multi_Thread_Rebuild_Point_Num 2147483647   // was 1500
```
(verified to force all rebuilds synchronous).

## Recommendation

Integrate via **`FetchContent_Declare` pinned to
`c0e36a16b6e4d557d3783b16911207f6398dd478`, then build our own object/static
target over `ikd-Tree/ikd_Tree.cpp` only** (not upstream's PCL demo
`CMakeLists.txt` — use `FetchContent_Populate`/`SOURCE_SUBDIR` to skip it).
Apply the PCL-free fix with the **shim-header approach** (a local
`pcl/point_types.h` that just `#include <Eigen/Core>`) so the upstream `.cpp`
stays byte-for-byte and the patch surface is one tiny file — cleanest for an
auditable benchmark. Build the source **twice** (BG-on with
`Multi_Thread_Rebuild_Point_Num=1500`, BG-off with it set to `INT_MAX`) to get
the two ikd modes; the third mode is our own tree. Match
`balance_param=0.7`/`alpha`, the tombstone trigger, and downsample-off; report
**wall-clock and CPU time**, and **per-point memory plus the fixed 42 MB
constant** separately. **Switch to vendoring under `third_party/` if** offline/
hermetic builds are required or upstream availability is a concern — at the cost
of carrying GPLv2 source (retain LICENSE + commit provenance). Given the repo is
not distributing binaries, GPLv2 is not a blocker either way; if that changes,
prefer FetchContent so GPL source is never redistributed by us.

## Open questions

- **Run-mode count for ikd-Tree two-compile.** Does the design want the BG-off
  mode to *also* stub `start_thread()` for a truly thread-free baseline, or is
  "thread created but idle" acceptable? Affects whether one more source edit is
  needed. (Recommendation: stub it for the cleanest single-thread number; the
  effect on timing is small but the report is cleaner.)
- **Dedup parity policy.** Match point *sets* (run our dedup + ikd downsample
  with equal voxel/radius — but they differ in rule) or run both raw and accept
  different N? This is a methodology decision, not a fact to discover.
- **Which memory metric is canonical** for the report's headline number (RSS
  delta vs. analytic node bytes). Both should appear; pick one for the chart.
- **1M-point run feasibility** under the ~150–170 B/point ikd-Tree node cost
  (≈170 MB nodes) plus 42 MB fixed — fine in the container, but the harness
  must allocate per-iteration within headroom (project ops rule).

## References

1. ikd-Tree repository (pinned commit `c0e36a16…`) — https://github.com/hku-mars/ikd-Tree — accessed 2026-06-16. (Source read locally: `ikd-Tree/ikd_Tree.h`, `ikd-Tree/ikd_Tree.cpp`, `examples/ikd_Tree_demo.cpp`, `CMakeLists.txt`, `LICENSE`.)
2. ikd-Tree paper — Cai, Y., Xu, W., Zhang, F. *ikd-Tree: An Incremental K-D Tree for Robotic Applications*, arXiv:2102.10808, 2021 — https://arxiv.org/abs/2102.10808 — accessed 2026-06-16.
3. ikd-Tree README (API list, GPLv2 license statement) — https://github.com/hku-mars/ikd-Tree/blob/master/README.md — accessed 2026-06-16.
4. ikd-Tree issue #31 "Can I Change pcl::PointXYZ to other data structure?" — https://github.com/hku-mars/ikd-Tree/issues/31 — accessed 2026-06-16.
5. ikd-Tree issue #20 (custom point type / PCL removal discussion) — https://github.com/hku-mars/ikd-Tree/issues/20 — accessed 2026-06-16.
6. GNU GPL v2 — http://www.gnu.org/licenses/old-licenses/gpl-2.0.html — accessed 2026-06-16.
7. `copse::KDTree` public API — `include/copse/kd_tree.hpp`, `include/copse/impl/point_traits.hpp` (this repo) — read 2026-06-16.

---

## Research metadata
- Date: 2026-06-16
- Pinned commit verified: `c0e36a16b6e4d557d3783b16911207f6398dd478`
- Verification performed in-container (`admiring_payne`): PCL-free compile of
  `ikd_Tree.cpp` (gcc-13, `-std=c++14 -O2 -pthread -I/usr/local/include/eigen3`),
  full-API driver link + run, ASan stack-overflow diagnosis of the ~42 MB
  object, `sizeof` measurements (`KD_TREE`=44,000,464; `KD_TREE_NODE`=136;
  `Operation_Logger_Type`=44), and a single-thread-rebuild variant
  (`Multi_Thread_Rebuild_Point_Num=2147483647`) producing identical output.
- Search queries used:
  - "ikd-Tree hku-mars compile without PCL Eigen aligned_allocator point_types.h"
- Sources consulted: cloned ikd-Tree source (headers, cpp, example, CMake,
  LICENSE), ikd-Tree README, issues #20/#31, GPLv2 text, this repo's
  `kd_tree.hpp` / `point_traits.hpp` / `docs/TODO.md`.
- Scope: D=3 only (ikd-Tree is hard-wired 3D); research/findings only, no design.
