# --- Catch2 microbenchmarks (library only) ---
# Each binary is registered as a ctest test under the `bench` label, so
# `ctest -L bench` runs the benchmarks while `ctest -L unit` runs the fast
# functional suite. The ctest entry is a bounded smoke (1 sample, serial) that
# proves each benchmark still runs end-to-end; the measured regression check
# (tools/bench_regression.sh) drives the same `bench`-labelled set with more
# samples and the XML reporter. RUN_SERIAL keeps the large-allocation cases from
# overlapping in memory under `ctest -j`.

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
    add_test(NAME ${name} COMMAND ${name} "[!benchmark]" --benchmark-samples 1)
    set_tests_properties(${name} PROPERTIES LABELS bench RUN_SERIAL TRUE)
endforeach()
