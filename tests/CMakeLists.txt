# One test executable per public-API feature of KDTree<Dim>. Black-box only:
# these exercise the public facade in include/copse/, never impl/ internals.
# Discovered under the `unit` ctest label — run with `ctest -L unit`.

set(COPSE_UNIT_TEST_SOURCES
    test_construction.cpp
    test_insert.cpp
    test_search.cpp
    test_remove.cpp
    test_delete.cpp
    test_rebuild.cpp
)

foreach(src IN LISTS COPSE_UNIT_TEST_SOURCES)
    get_filename_component(name "${src}" NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE copse Catch2::Catch2WithMain)
    copse_enable_clang_tidy_on(${name})
    catch_discover_tests(${name} PROPERTIES LABELS unit)
endforeach()
