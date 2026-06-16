# ikd-tree comparative benchmark

> Design date: 2026-06-16 · Slug: bench-vs-ikd
> Companion: `docs/research/ikd_tree_benchmark.md`

This document captures the design for a microbenchmark that compares
`copse::KDTree<3>` against the HKU MARS ikd-Tree on SLAM workloads,
across three run modes, at N ∈ {10k, 100k}. It implements the
`docs/TODO.md` → "ikd-tree comparative benchmark" slice. All numbers
are factually grounded in the companion research doc (pinned commit
`c0e36a16`, PCL-free compile verified in-container).

The TODO lists N up to 1M, but 1M is dropped from the *run*: it is the
dominant cost (build and radius-query time scale ~10× from 100k, then the
timing loop multiplies that by modes × samples), while the comparative
signal is already saturated by 100k — per-point insert and knn cost are
flat 100k→1M in the existing `docs/benchmark/` results. N=1M memory is
projected analytically in the report rather than measured.

## Goal

A single benchmark executable, `bench_perf_ikd`, that measures the five
workloads the TODO names — per-batch insert latency, kNN throughput,
radius search, spatial delete, memory footprint — for three trees:

- **(a) copse** — `copse::KDTree3`, single-thread.
- **(b) ikd single-thread** — ikd-Tree with background rebuild OFF.
- **(c) ikd default** — ikd-Tree as shipped (background rebuild ON).

and a companion report `docs/benchmark/vs_ikd_tree.md` that presents the
results side by side with the fairness caveats spelled out, so an
algorithmic win is never confused with a free-second-core win.

## Non-goals

- Any D other than 3. ikd-Tree is hard-wired 3D (research §4); the 2D/4D
  configurations of `copse::KDTree` have no counterpart and are not run.
- New workloads or run modes beyond the three the TODO specifies. No
  box-search-only row, no concurrency-scaling sweep, no ikd `Build`
  vs. our batch-insert apples-to-oranges row.
- Correctness cross-checks as a gating test. Set-level agreement may be
  asserted informally during bring-up, but the deliverable is timing +
  memory, not a conformance suite.
- Vendoring ikd-Tree source into the repo. Integration is FetchContent
  (see Dependencies); the `third_party/` option from the TODO text is
  recorded under Alternatives but not taken.
- Distributing any binary. GPLv2 attaches to *distributed* linked
  artifacts; this benchmark is internal-only, so the license is not a
  blocker. Noted so a future packaging change revisits it.

## Design

### Run modes and what each measures

| Mode | Tree | Rebuild | Threads | Measures |
|---|---|---|---|---|
| (a) | `copse::KDTree3` | inline partial (scapegoat) | 1 | our algorithm, single core |
| (b) | `KD_TREE<ikdTree_PointType>`, `Multi_Thread_Rebuild_Point_Num = INT_MAX` | synchronous, inline | 1 (see decision below) | ikd algorithm, single core |
| (c) | `KD_TREE<ikdTree_PointType>`, `Multi_Thread_Rebuild_Point_Num = 1500` | off-thread for subtrees ≥ 1500 | 2 (worker + BG) | ikd as-shipped, second core in play |

Modes (b) and (c) are the **same source compiled twice** with a
different `Multi_Thread_Rebuild_Point_Num` (research §3). The macro is
`#define`d in `ikd_Tree.h`, so a command-line `-D` collides; the build
selects the value with a **per-mode shim header** (see Dependencies).

### Point type and parity

Both trees hold `float` x/y/z (12 B). ikd-Tree uses its bundled
`ikdTree_PointType {float x,y,z;}`; we use `Eigen::Matrix<float,3,1>`.
The harness generates one `std::vector` of raw `(x,y,z)` triples per
measurement and converts to each tree's point type at the boundary, so
both see the *same* coordinates.

Config parity (research §2, §7):

- ikd `balance_param = 0.7` to match our `Config::alpha = 0.7`.
- ikd `delete_param = 0.25` to match our `Config::tombstone_threshold = 0.25`
  (we override ikd's 0.5 default so the rebuild cadence matches).
- ikd `Add_Points(batch, /*downsample_on=*/false)` for raw insert; we set
  our `Config::resolution = 1e-6f` so our dedup never fires either. Both
  insert every input point → identical live counts → per-point timings
  are over the same N. The voxel-vs-radius dedup mismatch is thereby
  avoided, not papered over.
- Box deletes use ikd's half-open `min <= p < max`; the harness matches
  our `delete_box` corner convention to that, so boundary points do
  not cause spurious count drift.
- kNN: ikd returns *squared* distances and takes a *linear* `max_dist`
  cap; our `Neighbor::sq_dist` is squared and `hybrid_search`/`radius_search`
  take a linear radius. Query parameters are aligned at the call site.

### Workload → method mapping

Per research §2. "Bulk build" is **out** (the TODO does not list it and
it invites the unfair `Build` vs. batch-insert row); the streaming
insert workload is the only insert row.

| Workload | copse | ikd-Tree |
|---|---|---|
| Per-batch insert latency | `insert(span)` | `Add_Points(batch, false)` |
| kNN throughput | `knn_search(q, k)` | `Nearest_Search(q, k, pts, sqd, max_dist)` |
| Radius search | `radius_search(q, r)` | `Radius_Search(q, r, pts)` |
| Spatial delete (box sweep) | `delete_box(box)` ×64 | `Delete_Point_Boxes({box})` ×64 |
| Bulk delete (5× insert+big delete) | `insert` + `delete_box(half)` | `Add_Points` + `Delete_Point_Boxes(half)` |
| Mixed SLAM cycle | `insert` + `knn_search` + periodic `delete_box` | `Add_Points` + `Nearest_Search` + periodic `Delete_Point_Boxes` |
| Memory footprint | RSS delta + analytic | RSS delta + analytic (report 42 MB constant separately) |

Two delete workloads cover the two regimes the wall-vs-CPU split exposes: the
**box sweep** (many sequential deletes, where ikd's background thread has work to
overlap) and **bulk churn** (repeated insert + one big delete — a single big
delete alone is unmeasurable for bg-on since it defers the rebuild past the
call). The **mixed cycle** is the realistic steady-churn case where per-frame
rebuilds stay below ikd's offload threshold.

For spatial delete we use the box variant on both sides (we have
`delete_box`; ikd has `Delete_Point_Boxes`). `delete_outside_radius`
has no clean ikd analog, so it is not benched here.

### Timing model: wall-clock AND CPU time

Mode (c) offloads large rebuilds to a second core, trading wall-clock for
total CPU. Reporting wall-clock alone would credit ikd default with an
algorithmic win it has not earned (research §7.5). Each timed row reports
**both**:

- **Wall-clock** — `std::chrono::steady_clock` around the measured action
  (the existing benches use Catch2 `Chronometer`; see below for why this
  bench does not).
- **CPU time** — `getrusage(RUSAGE_SELF)` `ru_utime + ru_stime` delta
  across the same action, summed over all threads of the process.

Mode (c)'s wall-clock should dip below its CPU time when the BG thread is
active; modes (a) and (b) should have wall ≈ CPU. The report charts
wall-clock as the headline and tabulates CPU time alongside.

### Why a hand-rolled timing loop, not Catch2 `BENCHMARK`

The existing benches use Catch2 `BENCHMARK`/`BENCHMARK_ADVANCED`. That
harness measures wall-clock only and offers no per-row CPU-time hook,
which this comparison requires. It also auto-tunes sample counts, which
fights the per-iteration memory discipline below (it may hold many
iterations' state). So `bench_perf_ikd` is a **plain `int main()`** with a
fixed-sample timing loop (warmup + N samples, report mean/median/stddev),
not a Catch2 TU. This is a deliberate, documented departure from the
sibling benches — flagged as a decision, not an oversight. The executable
is still registered in `benchmarks/CMakeLists.txt` next to the others.

### Memory-footprint measurement

Two numbers per tree (research §6):

1. **RSS delta** — `getrusage(...).ru_maxrss` (or `/proc/self/statm`)
   sampled before constructing the tree and after the build/insert phase,
   differenced. Captures allocator overhead and the fixed 42 MB.
2. **Analytic node estimate** — for ikd, `node_count × 136 B`; for us,
   `node_count × 32 B + PointStore/bucket capacities`. Cross-check.

The **fixed ~42 MB per `KD_TREE` object** (`Rebuild_Logger`'s
`Operation_Logger_Type q[1000000]`) is reported as a *separate constant*,
not folded into the per-point slope, so the slope is comparable across N.
At N=10k it dominates; by N=100k it is amortizing, and the report projects
the 1M crossover analytically rather than measuring it.

### Memory safety of the harness (project ops rule)

The ikd node cost is ~150–170 B/point; at the largest run size N=100k that
is ~17 MB of nodes plus the 42 MB constant ≈ ~60 MB per ikd object — well
inside container headroom (the dropped 1M would have been ~210 MB). Ours is
far smaller. The discipline below still holds:

- **One tree alive at a time.** The harness benches mode (a), then (b),
  then (c) **sequentially**, destroying each tree (and its input vectors)
  before constructing the next. Modes' structures never coexist.
- **`KD_TREE` is heap-allocated** (`std::unique_ptr` / `make_unique`).
  A stack local segfaults instantly (~42 MB object; research §1, verified).
- **Per-iteration allocation only.** Input clouds and query pools are
  generated per measurement phase and freed when the phase ends; no bulk
  pre-allocation of all-N-all-modes data up front (the OOM-class freeze
  the project ops rule warns against).
- **Leave a core free.** Mode (c)'s BG thread needs somewhere to run;
  the harness is single-worker-threaded and the operator runs it without
  pinning every core (project ops rule).

### KEY OPEN DECISION — stub `start_thread()` in mode (b)?

ikd-Tree's constructor calls `start_thread()` → `pthread_create`
**unconditionally**, even when `Multi_Thread_Rebuild_Point_Num = INT_MAX`.
So mode (b) as built from the macro alone still spawns a background
pthread that idle-spins (`usleep(100)` loop) and never receives work.

Two options:

- **(b-idle) Accept the idle thread.** Mode (b) is "BG rebuild disabled"
  via the macro; the spawned thread does nothing during operations (it
  never holds a lock once the macro is maxed — verified). Zero extra patch
  surface; the shim is purely the macro override.
- **(b-stub) Also stub `start_thread()`/`stop_thread()`** so no second
  thread exists at all — a truly single-threaded baseline. Requires one
  more small source edit beyond the macro (the shim would need to suppress
  the thread launch, which means touching more than the macro `#define`).

**Recommendation: (b-idle) — accept the idle thread.** Rationale:

1. The timing impact is negligible. With the macro maxed the worker never
   hands a subtree to the BG thread, so the idle thread does no work
   concurrent with the measured operation; its `usleep(100)` spin costs a
   trivial slice of a *second* core that the worker is not using anyway.
   Wall-clock and CPU time of mode (b) are unaffected to within noise.
2. It keeps the patch surface to **one tiny shim** (the macro override +
   the PCL shim header), with upstream `.cpp`/`.h` byte-for-byte. Stubbing
   the thread means intercepting `start_thread`, which is a private method
   wired through the constructor — a more invasive, more fragile edit that
   buys a cleaner *description* but not a cleaner *number*.
3. The report already separates wall-clock from CPU time, so any
   second-core effect would be visible if it existed; it does not.

This is the decision the caller must ratify. If the caller prefers the
maximally faithful "zero threads" baseline for the report's narrative,
switch to (b-stub) and the plan grows one source-patch step (documented
in the shim) — the harness and report are otherwise unchanged.

## File layout

New files (skeletons, written after confirmation):

| Path | Purpose |
|---|---|
| `benchmarks/bench_perf_ikd.cpp` | The comparison harness: `int main()`, point generation, mode dispatch, timing loop (wall + CPU), memory sampling, result table emission. |
| `cmake/IkdTree.cmake` | FetchContent of ikd-Tree @ `c0e36a16` + the two object targets (BG-on / BG-off) over `ikd_Tree.cpp`, with the PCL shim and per-mode macro shim wired. |
| `benchmarks/ikd_shim/pcl/point_types.h` | Shim header: `#include <Eigen/Core>`. Placed on the include path so upstream `#include <pcl/point_types.h>` resolves without PCL; upstream `.cpp`/`.h` stay byte-for-byte (bar the patched-out instantiations). |
| `benchmarks/ikd_shim/ikd_bg_off.h` / `ikd_bg_on.h` | Per-mode `-include` shims: `#define Multi_Thread_Rebuild_Point_Num` (INT_MAX / 1500) **and** `#define KD_TREE KD_TREE_BG_OFF`/`KD_TREE_BG_ON` so the two object targets emit distinct symbols and link into one binary. |
| `benchmarks/ikd_shim/ikd_facade.h` | Type-erased `ikd_facade::Tree` interface + `make_bg_off`/`make_bg_on` factories. The harness includes only this; the ~42 MB-object header never enters the harness TU (project rule: type erasure over exposing the template). |
| `benchmarks/ikd_shim/ikd_adapter.inc` + `ikd_adapter_bg_off.cpp` / `ikd_adapter_bg_on.cpp` | Shared adapter body bound per mode (each `.cpp` carries the mode's `-include` shim and is compiled into that mode's object target). Glue, not benchmark logic. |
| `docs/benchmark/vs_ikd_tree.md` | The report (methodology, results tables, Mermaid charts, fairness caveats). Skeleton/outline only until numbers exist. |

Modified files:

| Path | Change |
|---|---|
| `CMakeLists.txt` | `include(IkdTree)` (guarded by `COPSE_BUILD_BENCHMARKS`), mirroring the existing `include(Dependencies)` / `include(ClangTidy)` lines. |
| `benchmarks/CMakeLists.txt` | Register `bench_perf_ikd` as a standalone target (not in the Catch2 `foreach`, since it is a plain-`main` executable) linking `copse`, the two ikd object targets (which carry the facade adapter and put `ikd_facade.h` on the include path), `Eigen3::Eigen`, and `Threads::Threads`. clang-tidy is **not** run on upstream ikd sources (the object targets set `CXX_CLANG_TIDY ""`). |

## Dependencies

- **ikd-Tree** — HKU MARS, pinned to commit
  `c0e36a16b6e4d557d3783b16911207f6398dd478`, via `FetchContent_Declare`
  + `FetchContent_MakeAvailable` (or populate-without-add-subdirectory so
  ikd's PCL-demo `CMakeLists.txt` is not pulled in). Matches the
  Eigen/Catch2 dependency style. GPLv2 — internal, non-distributed, fine.
- **Eigen** — already a project dep; also satisfies the PCL shim
  (`<Eigen/Core>`) and ikd's `aligned_allocator`.
- **Threads** — `find_package(Threads REQUIRED)`; ikd needs `-pthread`.
- **`<sys/resource.h>` `getrusage`** — POSIX, present in-container.

### How the PCL-free + dual-macro build is wired (finalized)

The research verified two source tweaks. The skeleton applies them with a
**configure-time `PATCH_COMMAND`** (two `sed` hunks) on the fetched copy
plus include-path and `-include` shims — the host tree is never touched,
and the patch lives entirely under `build/_deps/`. (`FetchContent`
re-applies the patch on a clean populate; the verified patch is
idempotent on a fresh checkout.)

1. **PCL-free** — `benchmarks/ikd_shim/` precedes the upstream dir on the
   include path; its `pcl/point_types.h` resolves upstream's
   `#include <pcl/point_types.h>` to `#include <Eigen/Core>` (research §5
   sub-option 3). The three `template class KD_TREE<pcl::Point*>;`
   instantiations reference types the shim does not define, so the
   `PATCH_COMMAND` deletes exactly those three lines
   (`sed '/^template class KD_TREE<pcl::Point/d'`); the
   `ikdTree_PointType` instantiation is kept.

2. **Dual macro + symbol split** — two OBJECT targets compile the same
   `ikd_Tree.cpp`. The `PATCH_COMMAND` deletes the header's
   `#define Multi_Thread_Rebuild_Point_Num 1500` line; every compilation
   force-includes a per-mode `-include` shim that supplies the value:
   `ikd_bg_off.h` → INT_MAX (mode b), `ikd_bg_on.h` → 1500 (mode c). Each
   shim **also** `#define`s
   `KD_TREE` to a mode-specific name (`KD_TREE_BG_OFF` / `KD_TREE_BG_ON`)
   so the only strong symbols — the explicit `KD_TREE<ikdTree_PointType>`
   instantiation — differ between the two objects and link into one binary
   without ODR collision. (`KD_TREE_NODE` is a distinct token, untouched;
   implicitly-instantiated shared templates like
   `MANUAL_Q<Operation_Logger_Type>` are weak/COMDAT and the linker merges
   them.)

3. **Harness isolation** — each object target also compiles its facade
   adapter (`ikd_adapter_bg_*.cpp`, sharing `ikd_adapter.inc`) under the
   same `-include` shim, exposing `ikd_facade::make_bg_off/on`. The harness
   includes only `ikd_facade.h`, so the rename macros and the ~42 MB-object
   template never enter the harness TU.

**Everything downloads into the in-container `build/_deps/` by
construction.** No host clone, no host scratch, no `third_party/`. All
configure/build runs via `docker exec admiring_payne bash -lc '...'`.

## Build and test

- `bench_perf_ikd` builds only under `COPSE_BUILD_BENCHMARKS=ON`. It is a
  plain executable (no Catch2), so it runs directly:
  `docker exec admiring_payne bash -lc 'build/benchmarks/bench_perf_ikd'`.
- Build with capped parallelism: `-j $(nproc --ignore=1)` (project ops).
- ikd object targets compile at `-O2`/`-O3` `-pthread`; warnings from
  upstream are not `-Werror`'d and clang-tidy is not applied to them.
- No unit test is added — this is a benchmark slice, and the project's
  test convention is one-per-class for library code, not for benches.
- The report `docs/benchmark/vs_ikd_tree.md` is filled in after the run.
  Per project rule it describes **current** behaviour: latest numbers
  only, no historical comparison, no rev markers, no reproduce section.

### Report skeleton outline (`docs/benchmark/vs_ikd_tree.md`)

Mirrors `docs/benchmark/insert.md` structure:

- **Header** — title, run date, source `benchmarks/bench_perf_ikd.cpp`.
- **Methodology** — D=3 float; the three modes and what each measures;
  config parity (α/balance, tombstone/delete, downsample-off); wall-clock
  *and* CPU-time definitions; memory method (RSS delta + analytic, 42 MB
  constant called out); environment line (CPU/OS/compiler/CMake/`-O3`);
  sample count.
- **Results** — one subsection per workload (insert / kNN / radius /
  spatial delete / bulk delete / mixed cycle / memory), each a table with rows = N ∈ {10k,100k} ×
  the three modes, columns = wall-clock mean, CPU-time mean, stddev,
  per-point. Mermaid `xychart-beta` bar/line per workload (bars = mode a,
  lines = ikd modes), matching the insert report's chart idiom.
- **Memory** — per-point slope table + the 42 MB ikd constant stated
  separately; a short note on SoA-vs-pointer-chasing as the cause.
- **Fairness caveats** — the research §7 list, condensed: downsample-off,
  delete reclamation differs, BG thread wall-vs-CPU, fixed 42 MB,
  half-open box, tie-break determinism (sets not sequences).
- **What this tells us** — algorithmic vs. concurrency separation, the
  headline memory result, SLAM-budget framing (per the insert report's
  closing style).

### Harness skeleton outline (`benchmarks/bench_perf_ikd.cpp`)

- File-local `make_points(count, seed, extent)` and per-tree converters
  (`to_ikd_cloud`, points already native for ours).
- A small `Timing { double wall_ms; double cpu_ms; }` and a
  `time_action(fn)` helper that brackets `fn` with `steady_clock` and
  `getrusage`, runs warmup + fixed samples, returns aggregated `Timing`.
- A `MemSample` helper (RSS via `getrusage`/`statm`, analytic node count
  from each tree's `size()`/node accounting).
- One workload function per measure, each parameterized by N and mode,
  each constructing exactly one tree, measuring, then tearing it down
  before returning (memory discipline above).
- `main()` — iterate N ∈ {10k,100k} × modes {a,b,c} × workloads in an
  order that keeps one tree alive at a time; collect rows; emit a plain
  table to stdout for transcription into the report.

All bodies left as `// TODO: <intent>` in the skeleton per the
plan-then-implement workflow.

## Alternatives considered

- **Vendor ikd-Tree under `third_party/`** (the TODO's first option).
  Hermetic/offline, edits reviewable in-tree, but copies GPLv2 source into
  the repo (must retain LICENSE + provenance) and drifts from upstream
  manually. Rejected in favor of FetchContent, which matches the existing
  Eigen/Catch2 style and never redistributes GPL source. Revisit only if
  offline/hermetic builds become a hard requirement.
- **Reuse Catch2 `BENCHMARK` for this bench.** Rejected: no per-row
  CPU-time hook (mode c needs it) and auto-tuned sample counts fight the
  per-iteration memory discipline. A plain `main()` timing loop is used
  instead, documented as a deliberate departure.
- **Command-line `-D Multi_Thread_Rebuild_Point_Num=...`.** Rejected:
  the macro is `#define`d in the header, so a command-line define collides
  (research anti-pattern). Per-mode shim header / patched copy instead.
- **One ikd compile + runtime mode switch.** Not possible: the rebuild
  gate is a compile-time `#define` with no runtime setter. Two compiles
  are mandatory.
- **Including ikd `Build` (bulk) as an insert row.** Rejected: pits a
  one-shot balanced build against our streaming batch insert — an unfair
  shape (research §7.4). Only the streaming insert workload is benched.

## Open questions

All three resolved at skeleton time.

1. **Mode (b) thread stub — RESOLVED: b-idle.** Accept ikd's idle
   background rebuild thread; do **not** stub `start_thread()`. The BG-off
   shim is purely the `Multi_Thread_Rebuild_Point_Num = INT_MAX` macro
   override (plus the PCL shim and the per-mode `KD_TREE` rename). Upstream
   `.cpp`/`.h` stay byte-for-byte except the unavoidable removal of the
   three `pcl::Point*` instantiation lines on the fetched copy. Verified:
   both modes spawn the idle thread ("Multi thread started") and run clean.
2. **Memory headline metric — RESOLVED: RSS delta.** RSS delta
   (`getrusage` `ru_maxrss`) is the chart's headline; the analytic
   node-bytes estimate is tabulated alongside as a cross-check.
3. **Timing-loop sample count — RESOLVED: 3 warmup + 5 measured.** Report
   mean / median / stddev over the 5 measured samples. Kept small to bound
   run time; matches the existing benches' 5-sample convention.
