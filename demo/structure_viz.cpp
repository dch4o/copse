// Demo 2 — kd-tree structure viewer (3D, Polyscope).
//
// Reassembles the kd-tree's internal building blocks in the demo (the library is
// unchanged) so the node pool, leaf cells, and points are readable, then renders
// the live cloud (colored by leaf) and the leaf-cell AABBs as a wireframe. With
// "animate" on, an insert / delete is applied a chunk per frame so you watch the
// cells re-partition as the rebuild trigger fires. Run with `dump` for a headless
// text snapshot (no window).

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "imgui.h"
#include "inspectable_kd_tree.hpp"
#include "polyscope/curve_network.h"
#include "polyscope/point_cloud.h"
#include "polyscope/polyscope.h"

namespace {

using Point    = topiary::KDTree<3>::Point;
using TreeNode = topiary::internal::TreeNode;
using Tree     = demo::InspectableKdTree<3>;
using Vec3     = std::array<float, 3>;
using Box      = topiary::BBox<3>;

constexpr float kExtent = 100.0f;

Tree*        g_tree = nullptr;
std::mt19937 g_rng{1234};
bool         g_animate = false;
// When animating, an operation is queued and drained a step per frame.
std::vector<Point> g_pending_insert;
std::vector<Box>   g_pending_delete;

Vec3 to_vec(const Point& point) {
    return {point.x(), point.y(), point.z()};
}

std::vector<Point> random_cloud(std::mt19937& rng, std::size_t count) {
    std::uniform_real_distribution<float> coord{0.0f, kExtent};
    std::vector<Point>                    cloud;
    cloud.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        cloud.emplace_back(coord(rng), coord(rng), coord(rng));
    }
    return cloud;
}

// Live points (with their leaf index for coloring) and the leaf-cell AABB edges,
// gathered by walking the reachable tree from the demo-owned node pool.
struct Snapshot {
    std::vector<Vec3>                       points;
    std::vector<int>                        point_leaf;
    std::vector<Vec3>                       cell_nodes;
    std::vector<std::array<std::size_t, 2>> cell_edges;
};

void add_cell(Snapshot& snap, const Point& min_corner, const Point& max_corner) {
    const std::size_t base  = snap.cell_nodes.size();
    const float       lo[3] = {min_corner.x(), min_corner.y(), min_corner.z()};
    const float       hi[3] = {max_corner.x(), max_corner.y(), max_corner.z()};
    for (int corner = 0; corner < 8; ++corner) {
        snap.cell_nodes.push_back(
            {(corner & 1) ? hi[0] : lo[0], (corner & 2) ? hi[1] : lo[1], (corner & 4) ? hi[2] : lo[2]});
    }
    static const int edges[12][2] = {
        {0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 3}, {2, 6}, {3, 7}, {4, 5}, {4, 6}, {5, 7}, {6, 7}};
    for (const auto& edge : edges) {
        snap.cell_edges.push_back(
            {base + static_cast<std::size_t>(edge[0]), base + static_cast<std::size_t>(edge[1])});
    }
}

void walk(const Tree& tree, std::uint32_t node_idx, int& leaf_counter, Snapshot& snap) {
    if (node_idx == TreeNode::INVALID) {
        return;
    }
    const TreeNode& node = tree.nodes()[node_idx];
    if (node.is_leaf) {
        const int leaf_id  = leaf_counter++;
        bool      has_live = false;
        Point     min_corner;
        Point     max_corner;
        for (const auto& entry : tree.leaf_buckets().view(node.bucket_offset, node.bucket_size)) {
            if (tree.points().generation(entry.index) != entry.gen || !tree.points().is_live(entry.index)) {
                continue;
            }
            const auto& point = tree.points().point(entry.index);
            snap.points.push_back(to_vec(point));
            snap.point_leaf.push_back(leaf_id);
            if (!has_live) {
                min_corner = point;
                max_corner = point;
                has_live   = true;
            } else {
                min_corner = min_corner.cwiseMin(point);
                max_corner = max_corner.cwiseMax(point);
            }
        }
        // Build the cell from live points only: a leaf emptied by a delete draws no
        // wireframe, and a partially-cleared leaf shrinks to what remains.
        if (has_live) {
            add_cell(snap, min_corner, max_corner);
        }
        return;
    }
    walk(tree, node.left, leaf_counter, snap);
    walk(tree, node.right, leaf_counter, snap);
}

Snapshot snapshot(const Tree& tree) {
    Snapshot snap;
    int      leaf_counter = 0;
    walk(tree, tree.root(), leaf_counter, snap);
    return snap;
}

// Remove the query overlays so a stale highlight never lingers past the query it
// belongs to. Called on every refresh and before each new query.
void clear_overlays() {
    for (const char* name : {"query point", "knn hits", "radius hits", "hybrid hits"}) {
        polyscope::removeStructure(name, false);
    }
}

void refresh() {
    const Snapshot snap  = snapshot(*g_tree);
    auto*          cloud = polyscope::registerPointCloud("live points", snap.points);
    cloud->addScalarQuantity("leaf", snap.point_leaf);
    polyscope::registerCurveNetwork("leaf cells", snap.cell_nodes, snap.cell_edges)->setRadius(0.0008);
    clear_overlays();
}

// Drain one step of a queued animated operation (a chunk of inserts, or one
// delete slice). Each step mutates the tree — possibly triggering a rebuild — and
// refreshes, so the cells visibly re-partition.
void step_animation() {
    if (!g_pending_insert.empty()) {
        const std::size_t  chunk = std::min<std::size_t>(60, g_pending_insert.size());
        std::vector<Point> part(g_pending_insert.end() - static_cast<std::ptrdiff_t>(chunk),
                                g_pending_insert.end());
        g_pending_insert.erase(g_pending_insert.end() - static_cast<std::ptrdiff_t>(chunk),
                               g_pending_insert.end());
        g_tree->insert(part);
        refresh();
    } else if (!g_pending_delete.empty()) {
        const Box box = g_pending_delete.back();
        g_pending_delete.pop_back();
        g_tree->delete_box(box);
        refresh();
    }
}

// Shared query state, used by every query / radius control.
struct Query {
    float point[3] = {50.0f, 50.0f, 50.0f};
    int   k        = 10;
    float radius   = 8.0f;
    Point as_point() const { return Point{point[0], point[1], point[2]}; }
};

// Mark the query location with a small (non-occluding) green dot.
void mark_query(const Query& query) {
    polyscope::registerPointCloud("query point",
                                  std::vector<Vec3>{{query.point[0], query.point[1], query.point[2]}})
        ->setPointColor({0.1, 1.0, 0.1})
        ->setPointRadius(0.015, true);
}

std::vector<Vec3> coords_of(const std::vector<Tree::Neighbor>& neighbors) {
    std::vector<Vec3> out;
    out.reserve(neighbors.size());
    for (const auto& neighbor : neighbors) {
        out.push_back(to_vec(neighbor.coord));
    }
    return out;
}

// Register a query-result highlight. Drawn larger than the base cloud on purpose:
// the hits sit on the exact same coordinates as live points, and Polyscope draws
// structures in alphabetical name order, so "radius hits" (after "live points")
// would lose the equal-depth test and vanish at the base point size.
void show_hits(const char* name, const std::vector<Vec3>& pts, std::array<float, 3> color) {
    polyscope::registerPointCloud(name, pts)
        ->setPointColor({color[0], color[1], color[2]})
        ->setPointRadius(0.012, true);
}

void user_callback() {
    static Query query;

    step_animation();

    ImGui::Text("live points: %zu / capacity %zu", g_tree->size(), g_tree->capacity());

    if (ImGui::CollapsingHeader("insert", ImGuiTreeNodeFlags_DefaultOpen)) {
        static int batch_n = 1000;
        ImGui::SliderInt("batch##insert", &batch_n, 10, 5000);
        if (ImGui::Button("insert random batch")) {
            auto points = random_cloud(g_rng, static_cast<std::size_t>(batch_n));
            if (g_animate) {
                g_pending_insert.insert(g_pending_insert.end(), points.begin(), points.end());
            } else {
                g_tree->insert(points);
                refresh();
            }
        }
    }

    if (ImGui::CollapsingHeader("query", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat3("point", query.point, 0.0f, kExtent);
        ImGui::SliderInt("k", &query.k, 1, 64);
        ImGui::SliderFloat("radius", &query.radius, 0.5f, 40.0f);
        if (ImGui::Button("knn")) {
            clear_overlays();
            show_hits(
                "knn hits", coords_of(g_tree->knn_search(query.as_point(), query.k)), {1.0f, 0.2f, 0.2f});
            mark_query(query);
        }
        if (ImGui::Button("radius search")) {
            clear_overlays();
            // magenta: off the viridis leaf colormap so it reads against the base cloud.
            show_hits("radius hits",
                      coords_of(g_tree->radius_search(query.as_point(), query.radius)),
                      {1.0f, 0.15f, 0.9f});
            mark_query(query);
        }
        if (ImGui::Button("hybrid search (k within radius)")) {
            clear_overlays();
            show_hits("hybrid hits",
                      coords_of(g_tree->hybrid_search(query.as_point(), query.k, query.radius)),
                      {1.0f, 0.6f, 0.1f});
            mark_query(query);
        }
    }

    if (ImGui::CollapsingHeader("delete", ImGuiTreeNodeFlags_DefaultOpen)) {
        static float box_min[3] = {0.0f, 0.0f, 0.0f};
        static float box_max[3] = {50.0f, kExtent, kExtent};
        ImGui::SliderFloat3("box min", box_min, 0.0f, kExtent);
        ImGui::SliderFloat3("box max", box_max, 0.0f, kExtent);
        if (ImGui::Button("delete box")) {
            const Point lo{box_min[0], box_min[1], box_min[2]};
            const Point hi{box_max[0], box_max[1], box_max[2]};
            if (g_animate) {
                // Sweep the box along x so the cells visibly clear band by band.
                constexpr int slices = 12;
                for (int i = slices - 1; i >= 0; --i) {
                    const float x0 = lo.x() + (hi.x() - lo.x()) * static_cast<float>(i) / slices;
                    const float x1 = lo.x() + (hi.x() - lo.x()) * static_cast<float>(i + 1) / slices;
                    g_pending_delete.push_back(Box{Point{x0, lo.y(), lo.z()}, Point{x1, hi.y(), hi.z()}});
                }
            } else {
                g_tree->delete_box(Box{lo, hi});
                refresh();
            }
        }
        // Crop radius is its own control: the search radius (single digits) reused
        // here would clear all but a sliver of the cloud.
        static float crop_radius = 50.0f;
        ImGui::SliderFloat("crop radius", &crop_radius, 5.0f, kExtent);
        if (ImGui::Button("delete outside crop radius (around query point)")) {
            g_tree->delete_outside_radius(query.as_point(), crop_radius);
            refresh();
        }
    }

    if (ImGui::CollapsingHeader("animate / rebuild", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("animate insert / delete (watch rebuilds)", &g_animate);
        if (!g_pending_insert.empty() || !g_pending_delete.empty()) {
            ImGui::Text("animating: %zu inserts, %zu delete steps left",
                        g_pending_insert.size(),
                        g_pending_delete.size());
        }
        if (ImGui::Button("rebuild_all (re-partition cells)")) {
            g_tree->rebuild_all();
            refresh();
        }
    }
}

int run_dump() {
    std::printf("before     : live=%zu / capacity=%zu  leaf cells=%zu\n",
                g_tree->size(),
                g_tree->capacity(),
                snapshot(*g_tree).cell_nodes.size() / 8);
    g_tree->insert(random_cloud(g_rng, 5000)); // push well past capacity -> FIFO eviction + rebuilds
    const Snapshot snap = snapshot(*g_tree);
    std::printf("after +5000: live=%zu / capacity=%zu (FIFO-capped)  leaf cells=%zu\n",
                g_tree->size(),
                g_tree->capacity(),
                snap.cell_nodes.size() / 8);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    topiary::KDTree<3>::Config cfg;
    cfg.capacity   = 3000; // small so inserting past it triggers FIFO eviction (and its rebuilds)
    cfg.resolution = 1e-4f;
    static Tree tree{cfg};
    g_tree = &tree;

    tree.insert(random_cloud(g_rng, 2000));

    if (argc > 1 && std::string{argv[1]} == "dump") {
        return run_dump();
    }

    polyscope::init();
    polyscope::view::upDir = polyscope::UpDir::ZUp;
    refresh();

    if (argc > 1 && std::string{argv[1]} == "screenshot") {
        polyscope::view::resetCameraToHomeView();
        polyscope::screenshot("/tmp/structure_viz.png");
        return 0;
    }

    polyscope::state::userCallback = user_callback;
    polyscope::show();
    return 0;
}
