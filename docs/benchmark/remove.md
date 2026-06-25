# Remove benchmark

> Run date: 2026-06-25 · Source: `benchmarks/bench_remove.cpp`

Per-call cost of `KDTree3::remove` across query-list sizes at a
250k live-point tree. Exercises the remove path: per-query
`collect_indices_within(resolution²)` → `tombstone_index` →
`PointStore::release` → end-of-batch `maybe_partial_rebuild`.

## Methodology

- **D = 3, scalar = float**, random points uniform in `[0, 1)^3`.
- **Tree setup:** `capacity = 250k`, prefilled to 250k.
- **Queries:** coordinates sampled directly from the prefill so every
  query is guaranteed to match a live point (avoids the "uniform random
  queries miss the resolution-radius sphere" trap).
- **RNG:** `std::mt19937_64` with fixed seeds; `resolution = 1e-6f`.
- **Bench harness:** Catch2 v3.5.4, 20 samples per row.
- **Environment:** Ubuntu 24.04 LTS · Linux 6.17 · Intel Core Ultra 5
  235 (14 cores) · 16 GB RAM · g++ 13.3.0 · CMake 3.31.9 · Release `-O3`.

## Results

20 samples per row. Mean per `tree.remove(queries)` call.

| Query count | Mean / call |  Stddev | Per-query (mean) |
| ----------- | ----------: | ------: | ---------------: |
|       1,000 |    1.660 ms |   127 µs|          1.66 µs |
|      10,000 |    12.22 ms |   417 µs|          1.22 µs |

## What this tells us

**Per-query throughput improves as the batch grows.** At 1k queries
the per-query mean is ~1.66 µs; at 10k queries it falls to ~1.22 µs.
The fixed end-of-batch cost (the recursive `maybe_partial_rebuild`
sweep + any subtree rebuilds the sweep fires) amortizes across more
matches in the larger batch.

**Both rows sit well inside the 100 ms budget.** At a 10 Hz cadence,
the remove path comfortably handles 10k removes per batch on a 250k-point
tree (~12.2 ms). The end-of-batch sweep resolves every violator each
pass, so no residual scapegoat is left waiting for a later batch to pay
for — a small sparse batch of removes incurs only its own cost, not a
deferred partial-rebuild bill.
