set(TOPIARY_BENCH_SOURCES
    bench_insert.cpp
    bench_search.cpp
    bench_rebuild.cpp
    bench_remove.cpp
)

foreach(src IN LISTS TOPIARY_BENCH_SOURCES)
    get_filename_component(name "${src}" NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE topiary Catch2::Catch2WithMain)
    topiary_enable_clang_tidy_on(${name})
endforeach()

# ikd-Tree comparison harness: a plain-main executable (not Catch2), linking our
# tree plus both ikd object targets (BG off / BG on). The object targets carry
# the facade adapter and put the clean `ikd_facade.h` on the include path, so the
# harness never sees the upstream ikd headers. See cmake/IkdTree.cmake.
add_executable(bench_perf_ikd bench_perf_ikd.cpp)
target_link_libraries(bench_perf_ikd PRIVATE
    topiary
    ikd_tree_bg_off
    ikd_tree_bg_on
    Eigen3::Eigen
    Threads::Threads
)
topiary_enable_clang_tidy_on(bench_perf_ikd)
