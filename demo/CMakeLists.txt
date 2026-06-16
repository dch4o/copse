# Demos use Polyscope (native 3D viewer); fetched only when COPSE_BUILD_DEMOS is
# ON. Needs GL + X11 dev libraries on the system (see Dockerfile).
include(FetchContent)
FetchContent_Declare(polyscope
    GIT_REPOSITORY https://github.com/nmwsharp/polyscope.git
    GIT_TAG        v2.6.1
)
FetchContent_MakeAvailable(polyscope)

# Demo 1 — public-API client. Uses only copse::KDTree<3>.
add_executable(public_viz public_viz.cpp)
target_link_libraries(public_viz PRIVATE copse polyscope)

# Demo 2 — tree-structure viewer. Reassembles the kd-tree's internal building
# blocks in the demo itself (library unchanged) to read the node pool / leaf
# cells / points for visualization.
add_executable(structure_viz structure_viz.cpp)
target_link_libraries(structure_viz PRIVATE copse polyscope)
target_include_directories(structure_viz PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
