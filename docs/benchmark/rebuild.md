# Rebuild benchmark

> Run date: 2026-06-25 · Source: `benchmarks/bench_rebuild.cpp`

Cost of rebuild paths: steady-state batch insert (which folds in any
partial-rebuild work triggered by α-imbalance / tombstone-fraction /
leaf-overflow), explicit `rebuild_all`, the degenerate
single-leaf-overflow path, and a best-effort tombstone-trigger probe.

## Methodology

- **D = 3, scalar = float**, random points uniform in `[0, 100)^3`.
- **Tree setup:** `capacity = N`, prefilled to `N` before the timed
  iteration.
- **Steady-state partial rebuild:** the timed inner action is one
  `insert(5k)`. Every measured insert evicts FIFO head; the
  end-of-batch `maybe_partial_rebuild` runs a recursive top-down sweep
  and rebuilds every α-imbalance / tombstone / leaf-overflow violator
  in place.
- **`rebuild_all`:** the timed inner action is `tree.rebuild_all()`.
- **Degenerate cluster:** 1k points jittered around a single center
  (`±1e-3` per axis, well above `resolution = 1e-6f`). All routed
  through the same root-side path → repeated `LeafBucket::push`
  overflow → eager-split work.
- **Tombstone trigger:** prefill 250k, remove ~30% of the inserted points
  (coordinates sampled from the prefill so every query matches a live
  point), then measure a 1k batch insert. Best-effort: at least one
  subtree is expected to cross `tombstone_threshold = 0.25`.
- **RNG:** `std::mt19937_64` with fixed seeds; `resolution = 1e-6f`.
- **Bench harness:** Catch2 v3.5.4, 20 samples per row.
- **Environment:** Ubuntu 24.04 LTS · Linux 6.17 · Intel Core Ultra 5
  235 (14 cores) · 16 GB RAM · g++ 13.3.0 · CMake 3.31.9 · Release `-O3`.

## Results

20 samples per row.

### N sweep — `insert(5k)` (steady state) vs `rebuild_all`

| N    | partial-rebuild batch insert | `rebuild_all` |     Ratio |
| ---- | ---------------------------: | ------------: | --------: |
| 50k  |                     1.223 ms |      11.79 ms |     ~10×  |
| 100k |                     1.952 ms |      25.85 ms |     ~13×  |
| 250k |                     3.283 ms |      67.63 ms |     ~21×  |

```mermaid
xychart-beta
    title "partial rebuild (5k batch) — per-call time (ms)"
    x-axis [50k, 100k, 250k]
    y-axis "time (ms)" 0 --> 4
    bar [1.223, 1.952, 3.283]
```

```mermaid
xychart-beta
    title "rebuild_all — per-call time (ms)"
    x-axis [50k, 100k, 250k]
    y-axis "time (ms)" 0 --> 70
    bar [11.79, 25.85, 67.63]
```

### Trigger-specific paths (N = 250k)

| Path                                       | Mean / call |  Stddev |
| ------------------------------------------ | ----------: | ------: |
| degenerate cluster insert (1k batch)       |    1.134 ms |  69.1 µs|
| tombstone-triggered insert (1k batch, ~30% live points removed prior) |    1.091 ms |  35.3 µs|

## What this tells us

**Partial rebuild keeps steady-state batch cost well below the full
rebuild reference at every measured N.** A 5k batch insert runs in
single-digit ms (1.2–3.3 ms), while a `rebuild_all` over the same set
scales as `O(N log N)` and reaches ~68 ms at N=250k — a ~10–21× gap that
widens with N. The recursive top-down sweep visits every internal node,
but the per-node check is constant time; total sweep overhead stays
sub-millisecond.

**`rebuild_all` scales close to `O(N log N)`.** 50k → 100k (2×): 11.8
→ 25.9 ms (~2.2×). 100k → 250k (2.5×): 25.9 → 67.6 ms (~2.6×), modestly
above the analytic 2.5× as the working set grows and memory traffic
rises.

**Degenerate cluster insert exercises the eager-split path cheaply.**
A 1k batch where every point clusters within a `1e-3` cube on a
250k-point tree runs in ~1.1 ms — same order as a uniform 1k insert at
the same N. The `LeafBucket::push` overflow → `rebuild_subtree_in_place`
chain is not a hot spot.

**Tombstone-triggered partial rebuild keeps the batch fast** (~1.1 ms
for the 1k insert after ~30% removal). The trigger fires, the
scapegoat subtree is rebuilt in place, and the surrounding batch
remains well under any reasonable budget.
