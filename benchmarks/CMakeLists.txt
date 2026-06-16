# --- Catch2 microbenchmarks (library only) ---

set(COPSE_BENCH_SOURCES
    bench_insert.cpp
    bench_search.cpp
    bench_rebuild.cpp
    bench_remove.cpp
)

foreach(src IN LISTS COPSE_BENCH_SOURCES)
    get_filename_component(name "${src}" NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE copse Catch2::Catch2WithMain)
    copse_enable_clang_tidy_on(${name})
endforeach()

# --- HKU MARS ikd-Tree comparison baseline (benchmark only) ---
#
# Fetched, never vendored: the source lands in `_deps/` and is consumed as two
# object libraries defined here. Upstream ships only a PCL-demo `CMakeLists.txt`,
# so we populate WITHOUT `add_subdirectory` and compile the two loose files
# (`ikd-Tree/ikd_Tree.{h,cpp}`) ourselves.
#
# Two out-of-tree fixes make the bare x/y/z core build without PCL and let the
# rebuild gate be selected per mode (research: docs/research/ikd_tree_benchmark.md):
#
#   1. PCL-free. A shim `pcl/point_types.h` (-> `<Eigen/Core>`) on the include
#      path satisfies the upstream `#include <pcl/point_types.h>`. The three
#      `template class KD_TREE<pcl::Point*>;` explicit instantiations reference
#      types the shim does not define, so the PATCH_COMMAND deletes those three
#      lines (the `ikdTree_PointType` instantiation is kept).
#   2. Dual macro. `Multi_Thread_Rebuild_Point_Num` is `#define`d in the header,
#      so a plain `-D` collides. The PATCH_COMMAND deletes that `#define`, and a
#      per-mode force-included shim (`ikd_bg_off.h` / `ikd_bg_on.h`) supplies the
#      value: INT_MAX (mode b, BG off) or 1500 (mode c, BG on, as shipped). The
#      same shim also renames `KD_TREE` per mode so the two object targets emit
#      distinct symbols and link into one binary without ODR collision.
#
# Each object target also compiles its facade adapter (`ikd_adapter_bg_*.cpp`),
# which binds the renamed class to the type-erased `ikd_facade::Tree`. The
# harness includes only `ikd_facade.h` — never the ~42 MB-object header.
#
# The PATCH_COMMAND edits only the populated copy under `_deps/`; the host tree
# is never touched. Targets build with `-pthread`, not `-Werror`, and are
# excluded from clang-tidy (upstream third-party source).

include(FetchContent)
find_package(Threads REQUIRED)

# `sed` edits the fetched copy in place: delete the header's rebuild-gate
# `#define` (every compilation force-includes a per-mode shim that supplies the
# value), and drop the three pcl:: explicit instantiations.
FetchContent_Declare(ikd_tree
    GIT_REPOSITORY https://github.com/hku-mars/ikd-Tree.git
    GIT_TAG        c0e36a16b6e4d557d3783b16911207f6398dd478
    PATCH_COMMAND
        sed -i
            "/^#define Multi_Thread_Rebuild_Point_Num 1500/d"
            ikd-Tree/ikd_Tree.h
        COMMAND sed -i
            "/^template class KD_TREE<pcl::Point/d"
            ikd-Tree/ikd_Tree.cpp
)

# Populate without adding upstream's PCL-demo CMakeLists.
FetchContent_GetProperties(ikd_tree)
if(NOT ikd_tree_POPULATED)
    FetchContent_Populate(ikd_tree)
endif()

set(COPSE_IKD_SOURCE "${ikd_tree_SOURCE_DIR}/ikd-Tree/ikd_Tree.cpp")
set(COPSE_IKD_INCLUDE_DIR "${ikd_tree_SOURCE_DIR}/ikd-Tree")
set(COPSE_IKD_SHIM_DIR "${CMAKE_CURRENT_LIST_DIR}/ikd_shim")

# Each mode is an OBJECT library over the upstream `.cpp` plus its facade adapter,
# both compiled with the per-mode `-include` shim (rebuild-gate macro + `KD_TREE`
# rename). The shim dir precedes the upstream dir so `pcl/point_types.h` resolves
# to our `<Eigen/Core>` shim. The rename makes the two modes' symbols distinct so
# they link into one binary; the facade keeps the upstream header out of the
# harness TU.
function(copse_add_ikd_object target shim adapter)
    add_library(${target} OBJECT
        "${COPSE_IKD_SOURCE}"
        "${COPSE_IKD_SHIM_DIR}/${adapter}"
    )
    target_include_directories(${target} PUBLIC
        "${COPSE_IKD_SHIM_DIR}"
        "${COPSE_IKD_INCLUDE_DIR}"
    )
    target_link_libraries(${target} PUBLIC Eigen3::Eigen Threads::Threads)
    target_compile_options(${target} PRIVATE
        -pthread
        -include "${COPSE_IKD_SHIM_DIR}/${shim}"
    )
    # Upstream third-party source: no -Werror, no clang-tidy.
    set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "")
endfunction()

copse_add_ikd_object(ikd_tree_bg_off ikd_bg_off.h ikd_adapter_bg_off.cpp) # mode (b): INT_MAX, BG off
copse_add_ikd_object(ikd_tree_bg_on  ikd_bg_on.h  ikd_adapter_bg_on.cpp)  # mode (c): 1500, BG on

# --- ikd comparison harness (plain int main(), not Catch2) ---

add_executable(bench_perf_ikd bench_perf_ikd.cpp)
target_link_libraries(bench_perf_ikd PRIVATE
    copse
    ikd_tree_bg_off
    ikd_tree_bg_on
    Eigen3::Eigen
    Threads::Threads
)
copse_enable_clang_tidy_on(bench_perf_ikd)
