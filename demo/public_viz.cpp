// Demo 1 — public-API client (3D, Polyscope).
//
// Uses only copse::KDTree<3>. The live cloud is enumerated through the public
// API (a radius search wide enough to cover the extent), and insert / delete /
// query are driven from the panel. Shows the index from a user's view — no
// internal structure. Run with `dump` for a headless text snapshot.

#include "copse/kd_tree.hpp"

#include <array>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "imgui.h"
#include "polyscope/point_cloud.h"
#include "polyscope/polyscope.h"

namespace {

using KdTree = copse::KDTree<3>;
using Point  = KdTree::Point;
using Vec3   = std::array<float, 3>;

constexpr float kExtent = 100.0f;

KdTree*      g_tree = nullptr;
std::mt19937 g_rng{1234};

Vec3 to_vec(const Point& point) {
    return {point[0], point[1], point[2]};
}

std::vector<Point> random_cloud(std::mt19937& rng, std::size_t count) {
    std::uniform_real_distribution<float> coord{0.0f, kExtent};
    std::vector<Point>                    cloud;
    cloud.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        cloud.push_back(Point{coord(rng), coord(rng), coord(rng)});
    }
    return cloud;
}

// Enumerate the live cloud through the public API alone.
std::vector<Vec3> live_cloud(const KdTree& tree) {
    const auto        neighbors = tree.radius_search(Point{kExtent / 2, kExtent / 2, kExtent / 2}, 1.0e9f);
    std::vector<Vec3> cloud;
    cloud.reserve(neighbors.size());
    for (const auto& neighbor : neighbors) {
        cloud.push_back(to_vec(neighbor.coord));
    }
    return cloud;
}

std::vector<Vec3> coords_of(const std::vector<KdTree::Neighbor>& neighbors) {
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

// Remove the query overlays so a stale highlight never lingers past the query it
// belongs to. Called on every refresh and before each new query.
void clear_overlays() {
    for (const char* name : {"query point", "knn hits", "radius hits", "hybrid hits"}) {
        polyscope::removeStructure(name, false);
    }
}

void refresh() {
    polyscope::registerPointCloud("live points", live_cloud(*g_tree));
    clear_overlays();
}

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

void user_callback() {
    static Query query;

    ImGui::Text("live points: %zu / capacity %zu", g_tree->size(), g_tree->capacity());

    if (ImGui::CollapsingHeader("insert", ImGuiTreeNodeFlags_DefaultOpen)) {
        static int batch_n = 1000;
        ImGui::SliderInt("batch##insert", &batch_n, 10, 5000);
        if (ImGui::Button("insert random batch")) {
            g_tree->insert(random_cloud(g_rng, static_cast<std::size_t>(batch_n)));
            refresh();
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
            // magenta: contrasts with the base cloud.
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
            g_tree->box_delete({copse::BBox<3>{
                Point{box_min[0], box_min[1], box_min[2]},
                Point{box_max[0], box_max[1], box_max[2]}}});
            refresh();
        }
        // Crop radius is its own control: the search radius (single digits) reused
        // here would clear all but a sliver of the cloud.
        static float crop_radius = 50.0f;
        ImGui::SliderFloat("crop radius", &crop_radius, 5.0f, kExtent);
        if (ImGui::Button("delete outside crop radius (around query point)")) {
            g_tree->radius_crop(query.as_point(), crop_radius);
            refresh();
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    KdTree::Config cfg;
    cfg.capacity   = 3000; // small so inserting past it triggers FIFO eviction
    cfg.resolution = 1e-4f;
    static KdTree tree{cfg};
    g_tree = &tree;

    tree.insert(random_cloud(g_rng, 2000));

    if (argc > 1 && std::string{argv[1]} == "dump") {
        std::printf("live points=%zu\n", live_cloud(tree).size());
        return 0;
    }

    polyscope::init();
    polyscope::view::upDir = polyscope::UpDir::ZUp;
    refresh();
    polyscope::state::userCallback = user_callback;
    polyscope::show();
    return 0;
}
