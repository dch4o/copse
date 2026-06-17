# Search benchmark

> Run date: 2026-06-17 · Source: `benchmarks/bench_search.cpp`

Per-query latency of `KDTree3::knn_search`, `radius_search`,
`hybrid_search`, plus N-sweep + radius-sweep, plus mixed insert/query
cycle throughput.

## Methodology

- **D = 3, scalar = float**. Points uniform in `[0, 100)^3`.
- **Single-query rows:** pre-built tree (`capacity = 1M`, prefilled with
  100,000 points unless noted). Query pool of 256 points drawn from the
  same uniform distribution with a different seed. The timed inner
  action picks a query via atomic round-robin to avoid both RNG cost in
  the hot path and degenerate cache reuse of a single fixed query.
- **N sweep:** tree capacity matches prefill (50k / 100k / 500k / 1M);
  `knn_search(q, k=8)` repeatedly with the same 256-point query pool.
- **Radius sweep:** N = 100k tree; `radius_search` at r ∈ {0.5, 2.0,
  5.0, 10.0}.
- **Mixed-cycle rows:** one cycle = one `insert(1k)` followed by 10,000
  `knn_search(q, k=8)` queries. Reported time is the full cycle. Two
  settings: cap = 100k prefilled to 10k; cap = 1M prefilled to 500k.
- **RNG:** `std::mt19937_64` with fixed seeds; `resolution = 1e-6f`.
- **Bench harness:** Catch2 v3.5.4, 5 samples per row.
- **Environment:** Ubuntu 24.04 LTS · Linux 6.17 · Intel Core Ultra 5
  235 (14 cores) · 16 GB RAM · g++ 13.3.0 · CMake 3.31.9 · Release `-O3`.

## Results

5 samples per row.

### Single-query latency (N = 100k prefill)

| Query                       | Mean / call |  Stddev |
| --------------------------- | ----------: | ------: |
| `knn_search` k = 1          |    1.249 µs |  61.2 ns|
| `knn_search` k = 8          |    3.886 µs |   476 ns|
| `knn_search` k = 32         |    7.858 µs |  1.293 µs|
| `radius_search` r = 5.0     |    9.305 µs |   623 ns|
| `hybrid_search` k=32, r=5.0 |    6.172 µs |   420 ns|

### knn k=8 across live-point count

| N    | Mean / call |  Stddev |
| ---- | ----------: | ------: |
| 50k  |    2.038 µs |   343 ns|
| 100k |    3.024 µs |   149 ns|
| 500k |    5.189 µs |   197 ns|
| 1M   |    5.569 µs |   160 ns|

```mermaid
xychart-beta
    title "knn k=8 — mean per-query time (µs) vs N"
    x-axis [50k, 100k, 500k, 1M]
    y-axis "time (µs)" 0 --> 7
    bar [2.038, 3.024, 5.189, 5.569]
```

### radius_search across r (N = 100k)

| r    | Mean / call |  Stddev |
| ---- | ----------: | ------: |
| 0.5  |      779 ns |   157 ns|
| 2.0  |    2.175 µs |   264 ns|
| 5.0  |    8.810 µs |   376 ns|
| 10.0 |    48.24 µs |  1.055 µs|

### Mixed cycle (1 × insert(1k) + 10,000 × knn_search k=8)

| Prefill | Mean / cycle |   Stddev | Inferred per-query knn |
| ------- | -----------: | -------: | ---------------------: |
|     10k |    10.34 ms  |  99.7 µs |              ~1.0 µs   |
|    500k |    30.53 ms  |   661 µs |              ~3.1 µs   |

## What this tells us

**knn scales sublinearly with both `k` and `N`.** From k=1 to k=32
(32×) is ~6× cost (1.25 → 7.86 µs); from N=50k to N=1M (20×) is ~2.7×
cost (2.04 → 5.57 µs). Both trends point at the same mechanism: the
bounded max-heap fills early and tightens the leaf-scan skip threshold,
pruning most of the remaining tree.

**`radius_search` cost scales with the radius (result count).** Over
r ∈ {0.5, 2.0, 5.0, 10.0} on 100k points in `[0, 100)^3` the per-query
mean climbs from ~779 ns to ~48.2 µs (~62×). A larger radius clears the
per-axis split-plane prune test (`diff*diff` vs `sq_radius`) on more
internal nodes, so the traversal descends into — and leaf-scans — a
growing fraction of the tree and collects more matches. At small r the
squared split-plane gap exceeds the sq_radius early, so the descent
stays shallow and most of the tree is pruned.

**`hybrid_search` adds a `k` cap on top of the radius bound.** With
`(k = 32, r = 5.0)` the per-call time (~6.2 µs) sits just under both
`knn k = 32` (~7.9 µs) and `radius_search r = 5.0` (~9.3 µs): the `k`
cap lets the bounded heap tighten the prune threshold sooner than the
radius alone would. The margin over `radius_search` is modest at this
radius (~1.5×) because radius_search is already cheap here — but the
cap matters most when `r` is large enough that the unbounded result set
would balloon (radius_search alone is already ~48 µs at r=10.0).

**Mixed cycle is dominated by the query burst.** The 10k searches
account for the bulk of cycle time at both prefill levels; the 1k
insert contributes a few milliseconds at most. Per-query knn (k=8)
grows from ~1.0 µs at N=10k to ~3.1 µs at N=500k — sublinear in live
count, consistent with the `O(log N)` descent shape with cache-pressure
adjustments at larger working sets.
