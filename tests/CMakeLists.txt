# One test executable per testable class/struct/free function in the library.
# Tests under impl/ include internal headers directly; no extra plumbing needed
# because include/pkd_tree/impl/ is already on pkd_tree's PUBLIC include path.

set(PKD_TEST_SOURCES
    test_fixed_kd_tree.cpp
    test_point_store.cpp
    test_leaf_bucket.cpp
    test_tree_node.cpp
    test_tree_builder.cpp
    test_search_kernel.cpp
)

foreach(src IN LISTS PKD_TEST_SOURCES)
    get_filename_component(name "${src}" NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE pkd_tree Catch2::Catch2WithMain)
    pkd_enable_clang_tidy_on(${name})
    catch_discover_tests(${name})
endforeach()
