cmake_minimum_required(VERSION 3.20)

project(pkd_tree
    VERSION   0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

option(PKD_BUILD_TESTS      "Build pkd_tree test suite" ON)
option(PKD_BUILD_BENCHMARKS "Build pkd_tree benchmarks" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(Dependencies)
include(ClangTidy)

# --- Library ---

add_library(pkd_tree
    src/fixed_kd_tree.cpp
    src/fixed_kd_tree_impl.cpp
    src/point_store.cpp
    src/leaf_bucket.cpp
    src/tree_builder.cpp
    src/search_kernel.cpp
)
add_library(pkd_tree::pkd_tree ALIAS pkd_tree)

target_include_directories(pkd_tree
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(pkd_tree PUBLIC Eigen3::Eigen)

target_compile_options(pkd_tree PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

pkd_enable_clang_tidy_on(pkd_tree)

# --- Tests / benchmarks ---

if(PKD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PKD_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
