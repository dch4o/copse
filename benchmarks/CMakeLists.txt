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
