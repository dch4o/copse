# Remove benchmark

> Run date: 2026-05-14 · Source: `benchmarks/bench_remove.cpp`

Per-call cost of `FixedKdTree3::remove` across query-list sizes at a
1M live-point tree. Exercises the remove path: per-query
`collect_indices_within(resolution²)` → `tombstone_index` →
`PointStore::release` → end-of-batch `maybe_partial_rebuild`.

## Methodology

- **D = 3, scalar = float**, random points uniform in `[0, 1)^3`.
- **Tree setup:** `capacity = 1M`, prefilled to 1M.
- **Queries:** coordinates sampled directly from the prefill so every
  query is guaranteed to match a live point (avoids the "uniform random
  queries miss the resolution-radius sphere" trap).
- **RNG:** `std::mt19937_64` with fixed seeds; `resolution = 1e-6f`.
- **Bench harness:** Catch2 v3.5.4, 5 samples per row.
- **Environment:** Intel Core Ultra 5 235 · Linux 6.17 x86_64 ·
  g++ 13.3.0 · CMake 3.31.9 · Release `-O3`.

## Results

5 samples per row. Mean per `tree.remove(queries)` call.

| Query count | Mean / call |  Stddev | Per-query (mean) |
| ----------- | ----------: | ------: | ---------------: |
|       1,000 |    4.444 ms |   551 µs|          4.44 µs |
|      10,000 |    16.65 ms |  1.99 ms|          1.67 µs |

## What this tells us

**Per-query throughput improves as the batch grows.** At 1k queries
the per-query mean is ~4.4 µs; at 10k queries it falls to ~1.7 µs.
The fixed end-of-batch cost (the recursive `maybe_partial_rebuild`
sweep + any subtree rebuilds the sweep fires) amortizes across more
matches in the larger batch.

**Both rows sit well inside the 100 ms budget.** At a 10 Hz cadence,
the remove path comfortably handles 10k removes per batch on a 1M-point
tree. The earlier sparse-batch anomaly (a single small batch of
removes incurring a 100+ ms partial-rebuild bill from residual
imbalance left by the incremental fill) is gone in the current
release: the end-of-batch sweep resolves every violator each time
through, so there are no residual scapegoats waiting for the next
batch to pay for.
