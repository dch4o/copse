// ikd-Tree comparative benchmark — plain `int main()` harness.
//
// Compares `copse::KDTree3` against the HKU MARS ikd-Tree across three run
// modes for seven workloads (insert / kNN / radius / spatial delete / bulk delete
// / mixed cycle / memory); the N sweep is selectable via argv, report runs {10k,100k}.
// This is a deliberate departure from the sibling
// Catch2 benches: a fixed-sample timing loop reports BOTH wall-clock and CPU
// time (mode c offloads rebuilds to a second core), which the Catch2 harness
// cannot. See docs/design/bench-vs-ikd.md.
//
// Memory discipline (project ops rule): exactly one tree is alive at a time and
// every tree is heap-allocated — a stack ikd `KD_TREE` is ~42 MB and overflows.
// Input clouds and query pools are generated per phase and freed when it ends.
//
// `smoke()` (run via the `smoke` arg) exercises our API and BOTH ikd modes so
// the FetchContent + dual-macro link is proven; the full sweep writes the
// numbers transcribed into docs/benchmark/vs_ikd_tree.md.

#include "copse/copse.hpp"

#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numbers>
#include <numeric>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ikd_facade.h"

namespace {

// --- Config parity with the design (research §2, §7) ---
constexpr float kAlpha              = 0.7f;  // ikd balance_param == our Config::alpha
constexpr float kTombstoneThreshold = 0.25f; // ikd delete_param == our Config::tombstone_threshold
constexpr float kResolution         = 1e-6f; // our dedup never fires; ikd downsample stays off
constexpr float kBoxLength          = 0.2f;  // ikd downsample voxel edge (unused with downsample off)

// --- Timing loop (decision: 3 warmup + 5 measured samples; see design doc) ---
constexpr int kWarmupSamples   = 3;
constexpr int kMeasuredSamples = 5;

// --- Workload parameters (same coordinates fed to both trees) ---
constexpr float       kExtent      = 100.0f; // points uniform in [0, kExtent)^3 for every N
constexpr std::size_t kInsertBatch = 10'000; // streaming insert batch (mirrors bench_insert)
constexpr int         kKnnK        = 10;     // kNN k, aligned across both trees
constexpr std::size_t kQueryPool   = 1'000;  // queries per kNN / radius measured action
constexpr int         kDeleteGrid  = 4;      // 4x4x4 = 64 box-delete cells over the lower half-extent

// --- Mixed SLAM cycle: per frame = insert a scan + run queries + periodic box delete ---
constexpr std::size_t kCycleCount       = 10;    // frames per measured action
constexpr std::size_t kCycleInsertBatch = 1'000; // fresh points inserted per frame
constexpr std::size_t kCycleQueries     = 200;   // kNN queries per frame (<= kQueryPool)
constexpr std::size_t kDeleteEvery      = 3;     // box-delete every Nth frame (sustained churn → rebuilds)

// --- Bulk churn: repeated (insert a batch + delete a large box) ---
constexpr std::size_t kBulkIters = 5; // insert+delete iterations per measured action

enum class Mode {
    Copse,      // (a) copse::KDTree3, single-thread
    IkdBgOff, // (b) ikd, background rebuild OFF
    IkdBgOn,  // (c) ikd, background rebuild ON (as shipped)
};

struct Timing {
    double wall_ms;
    double wall_median_ms;
    double wall_stddev_ms;
    double cpu_ms;
};

struct MemSample {
    double rss_delta_bytes;     // headline metric (getrusage ru_maxrss delta)
    double analytic_node_bytes; // cross-check: node_count * per-node bytes
};

using Point = copse::KDTree3::Point;

std::vector<Point> make_points(std::size_t count, std::uint64_t seed, float extent) {
    std::mt19937_64                       rng{seed};
    std::uniform_real_distribution<float> dist{0.0f, extent};
    std::vector<Point>                    out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.emplace_back(dist(rng), dist(rng), dist(rng));
    }
    return out;
}

std::vector<ikd_facade::Point> to_ikd_cloud(const std::vector<Point>& points) {
    std::vector<ikd_facade::Point> out;
    out.reserve(points.size());
    for (const auto& point : points) {
        out.push_back(ikd_facade::Point{point.x(), point.y(), point.z()});
    }
    return out;
}

double cpu_time_ms() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    const auto to_ms = [](const timeval& tv) {
        return static_cast<double>(tv.tv_sec) * 1e3 + static_cast<double>(tv.tv_usec) * 1e-3;
    };
    return to_ms(usage.ru_utime) + to_ms(usage.ru_stime);
}

double current_rss_bytes() {
    // statm over ru_maxrss: the latter is a monotone high-water mark and reads ~0
    // once a larger allocation set the peak; statm tracks live resident pages, the
    // faithful per-phase delta when several phases share one process.
    std::FILE* statm = std::fopen("/proc/self/statm", "r");
    if (statm == nullptr) {
        return 0.0;
    }
    long      total_pages    = 0;
    long      resident_pages = 0;
    const int scanned        = std::fscanf(statm, "%ld %ld", &total_pages, &resident_pages);
    std::fclose(statm);
    if (scanned != 2) {
        return 0.0;
    }
    return static_cast<double>(resident_pages) * static_cast<double>(::sysconf(_SC_PAGESIZE));
}

double mean(const std::vector<double>& xs) {
    return std::accumulate(xs.begin(), xs.end(), 0.0) / static_cast<double>(xs.size());
}

double median(std::vector<double> xs) {
    std::sort(xs.begin(), xs.end());
    const std::size_t mid = xs.size() / 2;
    return (xs.size() % 2 == 0) ? 0.5 * (xs[mid - 1] + xs[mid]) : xs[mid];
}

double stddev(const std::vector<double>& xs, double mu) {
    if (xs.size() < 2) {
        return 0.0;
    }
    double acc = 0.0;
    for (const double x : xs) {
        acc += (x - mu) * (x - mu);
    }
    return std::sqrt(acc / static_cast<double>(xs.size() - 1));
}

template <typename Setup, typename Action>
Timing time_action(Setup&& setup, Action&& action) {
    std::vector<double> wall_samples;
    std::vector<double> cpu_samples;
    wall_samples.reserve(kMeasuredSamples);
    cpu_samples.reserve(kMeasuredSamples);

    const int total_reps = kWarmupSamples + kMeasuredSamples;
    for (int rep = 0; rep < total_reps; ++rep) {
        auto state = setup();

        const double cpu_before  = cpu_time_ms();
        const auto   wall_before = std::chrono::steady_clock::now();
        action(state);
        const auto   wall_after = std::chrono::steady_clock::now();
        const double cpu_after  = cpu_time_ms();

        if (rep >= kWarmupSamples) {
            const double wall_ms =
                std::chrono::duration<double, std::milli>(wall_after - wall_before).count();
            wall_samples.push_back(wall_ms);
            cpu_samples.push_back(cpu_after - cpu_before);
        }
    }

    const double wall_mean = mean(wall_samples);
    return Timing{wall_mean, median(wall_samples), stddev(wall_samples, wall_mean), mean(cpu_samples)};
}

// --- Tree construction: one alive at a time, all heap-allocated ---

std::unique_ptr<copse::KDTree3> make_copse(std::size_t capacity) {
    copse::KDTree3::Config cfg;
    cfg.capacity            = capacity;
    cfg.resolution          = kResolution;
    cfg.alpha               = kAlpha;
    cfg.tombstone_threshold = kTombstoneThreshold;
    return std::make_unique<copse::KDTree3>(cfg);
}

std::unique_ptr<ikd_facade::Tree> make_ikd(Mode mode) {
    return std::unique_ptr<ikd_facade::Tree>(
        mode == Mode::IkdBgOff ? ikd_facade::make_bg_off(kTombstoneThreshold, kAlpha, kBoxLength)
                               : ikd_facade::make_bg_on(kTombstoneThreshold, kAlpha, kBoxLength));
}

// --- Workloads: one function per measure, each builds ONE tree then tears it down ---

bool is_ikd(Mode mode) {
    return mode != Mode::Copse;
}

std::unique_ptr<copse::KDTree3> build_copse(const std::vector<Point>& points) {
    auto tree = make_copse(points.size());
    tree->insert(points); // no bulk-build API; stream so live counts match ikd's one-shot build
    return tree;
}

std::unique_ptr<ikd_facade::Tree> build_ikd(Mode mode, const std::vector<ikd_facade::Point>& cloud) {
    auto tree = make_ikd(mode);
    tree->build(cloud);
    return tree;
}

std::vector<Point> make_query_pool(std::uint64_t seed) {
    return make_points(kQueryPool, seed, kExtent);
}

float density_radius(std::size_t n) {
    // Solve expected = (N / extent^3) * (4/3) * pi * r^3 == kKnnK for r, so radius
    // search returns ~kKnnK neighbors and stays meaningful as density grows with N.
    const double volume   = static_cast<double>(kExtent) * kExtent * kExtent;
    const double per_unit = static_cast<double>(n) / volume;
    const double r_cubed  = static_cast<double>(kKnnK) / (per_unit * (4.0 / 3.0) * std::numbers::pi);
    return static_cast<float>(std::cbrt(r_cubed));
}

std::vector<ikd_facade::Box> make_delete_boxes() {
    const float                  cell = (kExtent * 0.5f) / static_cast<float>(kDeleteGrid);
    std::vector<ikd_facade::Box> boxes;
    boxes.reserve(static_cast<std::size_t>(kDeleteGrid) * kDeleteGrid * kDeleteGrid);
    for (int i = 0; i < kDeleteGrid; ++i) {
        for (int j = 0; j < kDeleteGrid; ++j) {
            for (int k = 0; k < kDeleteGrid; ++k) {
                const float lo[3] = {
                    static_cast<float>(i) * cell, static_cast<float>(j) * cell, static_cast<float>(k) * cell};
                boxes.push_back(
                    ikd_facade::Box{{lo[0], lo[1], lo[2]}, {lo[0] + cell, lo[1] + cell, lo[2] + cell}});
            }
        }
    }
    return boxes;
}

std::size_t delete_box_count() {
    return static_cast<std::size_t>(kDeleteGrid) * kDeleteGrid * kDeleteGrid;
}

// ikd's `Add_Points` null-derefs on an empty root (`Root_Node->division_axis`),
// so a one-point seed builds the root in setup (untimed) and the rest streams
// under measurement. copse is seeded with the same point for parity.
constexpr std::size_t kInsertSeed = 1;

std::size_t insert_streamed(std::size_t n) {
    return (n > kInsertSeed) ? (n - kInsertSeed) : n;
}

Timing bench_insert(Mode mode, std::size_t n) {
    const auto cloud = make_points(n, /*seed=*/0xB1A5E1ULL, kExtent);

    // Seed [0, kInsertSeed): ikd's Add_Points requires a non-null root, so this
    // tiny seed builds the tree in setup (untimed); both trees then stream the
    // remaining points under measurement, so per-point is over the same N.
    const std::size_t              seed_len = std::min(kInsertSeed, n);
    std::vector<ikd_facade::Point> ikd_seed;

    // Pre-slice the streamed ikd batches once: the adapter copies each batch to
    // native anyway, so slicing here keeps the timed region the insert call.
    std::vector<std::vector<ikd_facade::Point>> ikd_batches;
    if (is_ikd(mode)) {
        ikd_seed.reserve(seed_len);
        for (std::size_t i = 0; i < seed_len; ++i) {
            ikd_seed.push_back(ikd_facade::Point{cloud[i].x(), cloud[i].y(), cloud[i].z()});
        }
        for (std::size_t off = seed_len; off < n; off += kInsertBatch) {
            const std::size_t              len = std::min(kInsertBatch, n - off);
            std::vector<ikd_facade::Point> batch;
            batch.reserve(len);
            for (std::size_t i = off; i < off + len; ++i) {
                batch.push_back(ikd_facade::Point{cloud[i].x(), cloud[i].y(), cloud[i].z()});
            }
            ikd_batches.push_back(std::move(batch));
        }
    }

    struct State {
        std::unique_ptr<copse::KDTree3> copse;
        std::unique_ptr<ikd_facade::Tree> ikd;
    };

    const auto setup = [&] {
        State state;
        if (is_ikd(mode)) {
            state.ikd = make_ikd(mode);
            state.ikd->build(ikd_seed); // seed the root (untimed)
        } else {
            state.copse = make_copse(n);
            state.copse->insert(std::span<const Point>{cloud.data(), seed_len}); // seed (untimed)
        }
        return state;
    };

    const auto stream = [&](State& state) {
        if (is_ikd(mode)) {
            for (const auto& batch : ikd_batches) {
                state.ikd->add_points(batch);
            }
        } else {
            for (std::size_t off = seed_len; off < n; off += kInsertBatch) {
                const std::size_t len = std::min(kInsertBatch, n - off);
                state.copse->insert(std::span<const Point>{cloud.data() + off, len});
            }
        }
    };

    return time_action(setup, stream);
}

Timing bench_knn(Mode mode, std::size_t n) {
    const auto cloud     = make_points(n, /*seed=*/0xB1A5E1ULL, kExtent);
    const auto ikd_cloud = is_ikd(mode) ? to_ikd_cloud(cloud) : std::vector<ikd_facade::Point>{};
    const auto queries   = make_query_pool(/*seed=*/0x9EE71EULL);
    // Both sides run unbounded kNN: the adapter calls Nearest_Search with the
    // INFINITY max_dist default, matching our unbounded knn_search.

    struct State {
        std::unique_ptr<copse::KDTree3> copse;
        std::unique_ptr<ikd_facade::Tree> ikd;
    };

    const auto setup = [&] {
        State state;
        if (is_ikd(mode)) {
            state.ikd = build_ikd(mode, ikd_cloud);
        } else {
            state.copse = build_copse(cloud);
        }
        return state;
    };

    const auto run = [&](State& state) {
        std::size_t        found = 0;
        std::vector<float> sq_dists;
        for (const auto& query : queries) {
            if (is_ikd(mode)) {
                found += state.ikd->knn(ikd_facade::Point{query.x(), query.y(), query.z()}, kKnnK, sq_dists);
            } else {
                found += state.copse->knn_search(query, kKnnK).size();
            }
        }
        (void)found;
    };

    return time_action(setup, run);
}

Timing bench_radius(Mode mode, std::size_t n) {
    const auto  cloud     = make_points(n, /*seed=*/0xB1A5E1ULL, kExtent);
    const auto  ikd_cloud = is_ikd(mode) ? to_ikd_cloud(cloud) : std::vector<ikd_facade::Point>{};
    const auto  queries   = make_query_pool(/*seed=*/0x9EE71EULL);
    const float radius    = density_radius(n);

    struct State {
        std::unique_ptr<copse::KDTree3> copse;
        std::unique_ptr<ikd_facade::Tree> ikd;
    };

    const auto setup = [&] {
        State state;
        if (is_ikd(mode)) {
            state.ikd = build_ikd(mode, ikd_cloud);
        } else {
            state.copse = build_copse(cloud);
        }
        return state;
    };

    const auto run = [&](State& state) {
        std::size_t                    found = 0;
        std::vector<ikd_facade::Point> in_radius;
        for (const auto& query : queries) {
            if (is_ikd(mode)) {
                found +=
                    state.ikd->radius(ikd_facade::Point{query.x(), query.y(), query.z()}, radius, in_radius);
            } else {
                found += state.copse->radius_search(query, radius).size();
            }
        }
        (void)found;
    };

    return time_action(setup, run);
}

Timing bench_spatial_delete(Mode mode, std::size_t n) {
    const auto cloud     = make_points(n, /*seed=*/0xB1A5E1ULL, kExtent);
    const auto ikd_cloud = is_ikd(mode) ? to_ikd_cloud(cloud) : std::vector<ikd_facade::Point>{};
    const auto boxes     = make_delete_boxes();

    // Pre-build the copse BBox list once; the batched delete runs the whole grid as a
    // single rebuild-triggering call rather than one trigger per box.
    std::vector<copse::BBox<3>> copse_boxes;
    if (!is_ikd(mode)) {
        copse_boxes.reserve(boxes.size());
        for (const auto& box : boxes) {
            copse_boxes.push_back(copse::BBox<3>{Point{box.min[0], box.min[1], box.min[2]},
                                                 Point{box.max[0], box.max[1], box.max[2]}});
        }
    }

    struct State {
        std::unique_ptr<copse::KDTree3> copse;
        std::unique_ptr<ikd_facade::Tree> ikd;
    };

    // Each rep rebuilds the full tree (delete mutates it), then sweeps the boxes.
    const auto setup = [&] {
        State state;
        if (is_ikd(mode)) {
            state.ikd = build_ikd(mode, ikd_cloud);
        } else {
            state.copse = build_copse(cloud);
        }
        return state;
    };

    const auto sweep = [&](State& state) {
        if (is_ikd(mode)) {
            state.ikd->delete_boxes(boxes);
        } else {
            state.copse->delete_boxes(std::span<const copse::BBox<3>>{copse_boxes.data(), copse_boxes.size()});
        }
    };

    return time_action(setup, sweep);
}

// Bulk churn: each iteration inserts a fresh batch then deletes the x < extent/2
// half (≈ a large fraction) in one call, measured over kBulkIters iterations.
// A SINGLE bulk delete is meaningless for bg-on — it schedules the rebuild on the
// background thread and returns, so the work escapes the measured call (wall AND
// CPU read low). Repeating insert+delete keeps each deferred rebuild inside the
// window — it must overlap with, or block, the next iteration — so the background
// thread's offload (or its absence) is captured honestly. Per iteration.
Timing bench_bulk_delete(Mode mode, std::size_t n) {
    const std::size_t batch       = n / 2;
    const auto        base_cloud  = make_points(n, /*seed=*/0xB1A5E1ULL, kExtent);
    const auto        insert_pool = make_points(kBulkIters * batch, /*seed=*/0xC0FFEEULL, kExtent);
    const auto        base_ikd = is_ikd(mode) ? to_ikd_cloud(base_cloud) : std::vector<ikd_facade::Point>{};
    const ikd_facade::Box big_box{{0.0f, 0.0f, 0.0f}, {kExtent * 0.5f, kExtent, kExtent}};

    std::vector<std::vector<ikd_facade::Point>> ikd_batches;
    if (is_ikd(mode)) {
        for (std::size_t i = 0; i < kBulkIters; ++i) {
            std::vector<ikd_facade::Point> ikd_batch;
            ikd_batch.reserve(batch);
            for (std::size_t j = 0; j < batch; ++j) {
                const auto& point = insert_pool[i * batch + j];
                ikd_batch.push_back(ikd_facade::Point{point.x(), point.y(), point.z()});
            }
            ikd_batches.push_back(std::move(ikd_batch));
        }
    }

    struct State {
        std::unique_ptr<copse::KDTree3> copse;
        std::unique_ptr<ikd_facade::Tree> ikd;
    };

    const auto setup = [&] {
        State state;
        if (is_ikd(mode)) {
            state.ikd = build_ikd(mode, base_ikd);
        } else {
            state.copse = make_copse(n + kBulkIters * batch); // no FIFO eviction over the churn
            state.copse->insert(base_cloud);
        }
        return state;
    };

    const auto churn = [&](State& state) {
        for (std::size_t i = 0; i < kBulkIters; ++i) {
            if (is_ikd(mode)) {
                state.ikd->add_points(ikd_batches[i]);
                state.ikd->delete_box(big_box);
            } else {
                const std::size_t off = i * batch;
                state.copse->insert(std::span<const Point>{insert_pool.data() + off, batch});
                state.copse->delete_box(
                    copse::BBox<3>{Point{big_box.min[0], big_box.min[1], big_box.min[2]},
                                     Point{big_box.max[0], big_box.max[1], big_box.max[2]}});
            }
        }
    };

    return time_action(setup, churn);
}

// A SLAM-shaped loop on a pre-built map: each frame inserts a fresh scan, runs a
// query burst, and (periodically) deletes a box. Sustained insert+delete churn is
// the one workload that keeps ikd's background rebuild thread continuously busy, so
// it is where mode (c)'s second core can pay off across the whole run, not just on
// a one-shot delete. Per-frame over kCycleCount frames.
Timing bench_mixed(Mode mode, std::size_t n) {
    const auto base_cloud  = make_points(n, /*seed=*/0xB1A5E1ULL, kExtent);
    const auto insert_pool = make_points(kCycleCount * kCycleInsertBatch, /*seed=*/0xC0FFEEULL, kExtent);
    const auto queries     = make_query_pool(/*seed=*/0x9EE71EULL);
    const auto boxes       = make_delete_boxes();
    const auto ikd_base    = is_ikd(mode) ? to_ikd_cloud(base_cloud) : std::vector<ikd_facade::Point>{};

    // Pre-slice the per-frame ikd insert batches (the adapter copies to native anyway).
    std::vector<std::vector<ikd_facade::Point>> ikd_batches;
    if (is_ikd(mode)) {
        for (std::size_t c = 0; c < kCycleCount; ++c) {
            std::vector<ikd_facade::Point> batch;
            batch.reserve(kCycleInsertBatch);
            for (std::size_t i = 0; i < kCycleInsertBatch; ++i) {
                const auto& point = insert_pool[c * kCycleInsertBatch + i];
                batch.push_back(ikd_facade::Point{point.x(), point.y(), point.z()});
            }
            ikd_batches.push_back(std::move(batch));
        }
    }

    struct State {
        std::unique_ptr<copse::KDTree3> copse;
        std::unique_ptr<ikd_facade::Tree> ikd;
    };

    const auto setup = [&] {
        State state;
        if (is_ikd(mode)) {
            state.ikd = build_ikd(mode, ikd_base);
        } else {
            // Capacity holds the base map plus every frame's inserts — no FIFO eviction,
            // so both trees see identical insert/delete sequences.
            state.copse = make_copse(n + kCycleCount * kCycleInsertBatch);
            state.copse->insert(base_cloud);
        }
        return state;
    };

    const auto run = [&](State& state) {
        std::size_t        found = 0;
        std::vector<float> sq_dists;
        for (std::size_t c = 0; c < kCycleCount; ++c) {
            if (is_ikd(mode)) {
                state.ikd->add_points(ikd_batches[c]);
            } else {
                const std::size_t off = c * kCycleInsertBatch;
                state.copse->insert(std::span<const Point>{insert_pool.data() + off, kCycleInsertBatch});
            }
            for (std::size_t q = 0; q < kCycleQueries; ++q) {
                const auto& query = queries[q];
                if (is_ikd(mode)) {
                    found +=
                        state.ikd->knn(ikd_facade::Point{query.x(), query.y(), query.z()}, kKnnK, sq_dists);
                } else {
                    found += state.copse->knn_search(query, kKnnK).size();
                }
            }
            if (c % kDeleteEvery == 0) {
                const auto& box = boxes[c % boxes.size()];
                if (is_ikd(mode)) {
                    state.ikd->delete_box(box);
                } else {
                    state.copse->delete_box(copse::BBox<3>{Point{box.min[0], box.min[1], box.min[2]},
                                                              Point{box.max[0], box.max[1], box.max[2]}});
                }
            }
        }
        (void)found;
    };

    return time_action(setup, run);
}

MemSample bench_memory(Mode mode, std::size_t n) {
    const auto cloud     = make_points(n, /*seed=*/0xB1A5E1ULL, kExtent);
    const auto ikd_cloud = is_ikd(mode) ? to_ikd_cloud(cloud) : std::vector<ikd_facade::Point>{};

    // RSS measured around construction + build, with the per-rep input vectors
    // already allocated above so their pages do not pollute the delta.
    const double rss_before = current_rss_bytes();

    double      analytic_node_bytes = 0.0;
    std::size_t node_count          = 0;
    if (is_ikd(mode)) {
        auto tree  = build_ikd(mode, ikd_cloud);
        node_count = static_cast<std::size_t>(tree->size());
        // Per-node ~136 B plus the fixed ~42 MB Rebuild_Logger array (q[1000000]
        // of 44 B). The constant is reported separately from the slope.
        constexpr double kIkdNodeBytes = 136.0;
        analytic_node_bytes            = static_cast<double>(node_count) * kIkdNodeBytes;
        const double rss_after         = current_rss_bytes();
        return MemSample{rss_after - rss_before, analytic_node_bytes};
    }

    auto tree  = build_copse(cloud);
    node_count = tree->size();
    // Flat SoA: a PointStore slot (~17 B: 12 B point + 4 B generation + liveness)
    // plus the bucketed TreeNode array (~32 B/node, far fewer nodes than points).
    constexpr double kCopseSlotBytes = 17.0;
    constexpr double kCopseNodeBytes = 32.0;
    analytic_node_bytes =
        static_cast<double>(node_count) * kCopseSlotBytes + static_cast<double>(node_count) * kCopseNodeBytes;
    const double rss_after = current_rss_bytes();
    return MemSample{rss_after - rss_before, analytic_node_bytes};
}

int smoke() {
    constexpr std::size_t kSmokeN = 256;
    const auto            cloud   = make_points(kSmokeN, /*seed=*/0x5A1A11ULL, /*extent=*/10.0f);
    const auto            query   = Point{1.0f, 1.0f, 1.0f};

    // (a) copse.
    {
        auto tree = make_copse(/*capacity=*/kSmokeN * 2);
        tree->insert(cloud);
        const auto knn    = tree->knn_search(query, 4);
        const auto radius = tree->radius_search(query, 1.0f);
        const auto erased =
            tree->delete_box(copse::BBox<3>{Point{-1.0f, -1.0f, -1.0f}, Point{0.0f, 0.0f, 0.0f}});
        std::printf("smoke copse:      size=%zu knn=%zu radius=%zu erased=%zu\n",
                    tree->size(),
                    knn.size(),
                    radius.size(),
                    erased);
    }

    // (b)/(c) ikd-tree, each mode in turn — one tree alive at a time.
    const auto ikd_cloud = to_ikd_cloud(cloud);
    for (const Mode mode : {Mode::IkdBgOff, Mode::IkdBgOn}) {
        auto tree = make_ikd(mode);
        tree->build(ikd_cloud);
        std::vector<float>             sq_dists;
        std::vector<ikd_facade::Point> in_radius;
        const auto knn    = tree->knn(ikd_facade::Point{query.x(), query.y(), query.z()}, 4, sq_dists);
        const auto radius = tree->radius(ikd_facade::Point{query.x(), query.y(), query.z()}, 1.0f, in_radius);
        const ikd_facade::Box box{{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, 0.0f}};
        const auto            erased = tree->delete_box(box);
        std::printf("smoke ikd %s: size=%d valid=%d knn=%zu radius=%zu erased=%d\n",
                    mode == Mode::IkdBgOff ? "bg-off" : "bg-on ",
                    tree->size(),
                    tree->valid(),
                    knn,
                    radius,
                    erased);
    }

    return 0;
}

const char* mode_label(Mode mode) {
    switch (mode) {
    case Mode::Copse:
        return "(a) copse";
    case Mode::IkdBgOff:
        return "(b) ikd bg-off";
    case Mode::IkdBgOn:
        return "(c) ikd bg-on";
    }
    return "?";
}

const char* n_label(std::size_t n) {
    switch (n) {
    case 10'000:
        return "10k";
    case 100'000:
        return "100k";
    case 1'000'000:
        return "1M";
    default:
        return "?";
    }
}

constexpr Mode kModes[] = {Mode::Copse, Mode::IkdBgOff, Mode::IkdBgOn};

void emit_timed(const char* title,
                Timing (*workload)(Mode, std::size_t),
                const std::vector<std::size_t>& sizes,
                double (*per_unit_div)(std::size_t),
                const char* unit) {
    std::printf("\n## %s\n", title);
    std::printf(
        "%-6s %-16s %12s %12s %12s %14s\n", "N", "Mode", "wall_mean_ms", "cpu_mean_ms", "wall_sd_ms", unit);
    for (const std::size_t n : sizes) {
        for (const Mode mode : kModes) {
            const Timing t        = workload(mode, n);
            const double per_unit = t.wall_ms * 1e6 / per_unit_div(n); // ns per unit
            std::printf("%-6s %-16s %12.4f %12.4f %12.4f %12.1f ns\n",
                        n_label(n),
                        mode_label(mode),
                        t.wall_ms,
                        t.cpu_ms,
                        t.wall_stddev_ms,
                        per_unit);
            std::fflush(stdout);
        }
    }
}

void emit_memory(const std::vector<std::size_t>& sizes) {
    std::printf("\n## memory\n");
    std::printf(
        "%-6s %-16s %14s %18s %14s\n", "N", "Mode", "rss_delta_MB", "analytic_node_MB", "bytes/point");
    for (const std::size_t n : sizes) {
        for (const Mode mode : kModes) {
            const MemSample m = bench_memory(mode, n);
            std::printf("%-6s %-16s %14.2f %18.2f %14.1f\n",
                        n_label(n),
                        mode_label(mode),
                        m.rss_delta_bytes / (1024.0 * 1024.0),
                        m.analytic_node_bytes / (1024.0 * 1024.0),
                        m.analytic_node_bytes / static_cast<double>(n));
            std::fflush(stdout);
        }
    }
}

double per_point(std::size_t n) {
    return static_cast<double>(insert_streamed(n));
}
double per_query(std::size_t) {
    return static_cast<double>(kQueryPool);
}
double per_box(std::size_t) {
    return static_cast<double>(delete_box_count());
}
double per_cycle(std::size_t) {
    return static_cast<double>(kCycleCount);
}
double per_bulk_iter(std::size_t) {
    return static_cast<double>(kBulkIters);
}

} // namespace

// Args: optional max-N selector ("10k" | "100k" | "1m") to cap the sweep for the
// memory-safety validation pass; "smoke" runs only the integration smoke. Default
// runs the full N in {10k, 100k, 1M} sweep.
int main(int argc, char** argv) {
    std::vector<std::size_t> sizes = {10'000, 100'000, 1'000'000};
    if (argc > 1) {
        const std::string arg = argv[1];
        if (arg == "smoke") {
            return smoke();
        }
        if (arg == "10k") {
            sizes = {10'000};
        } else if (arg == "100k") {
            sizes = {10'000, 100'000};
        } else if (arg == "1m") {
            sizes = {10'000, 100'000, 1'000'000};
        }
    }

    std::printf(
        "# bench_perf_ikd — %d warmup + %d measured samples per row\n", kWarmupSamples, kMeasuredSamples);
    emit_timed("insert (per-point)", bench_insert, sizes, per_point, "per_point");
    emit_timed("knn (per-query)", bench_knn, sizes, per_query, "per_query");
    emit_timed("radius (per-query)", bench_radius, sizes, per_query, "per_query");
    emit_timed("spatial_delete (per-box)", bench_spatial_delete, sizes, per_box, "per_box");
    emit_timed("bulk_delete (per-iteration)", bench_bulk_delete, sizes, per_bulk_iter, "per_iter");
    emit_timed("mixed_cycle (per-frame)", bench_mixed, sizes, per_cycle, "per_frame");
    emit_memory(sizes);
    return 0;
}
