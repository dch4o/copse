cmake_minimum_required(VERSION 3.20)

project(topiary
    VERSION   0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

option(TOPIARY_BUILD_TESTS      "Build topiary test suite" ON)
option(TOPIARY_BUILD_BENCHMARKS "Build topiary benchmarks" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(Dependencies)
include(ClangTidy)

# --- Library ---

add_library(topiary
    src/kd_tree.cpp
    src/kd_tree_impl.cpp
    src/point_store.cpp
    src/leaf_bucket.cpp
    src/tree_builder.cpp
    src/search_kernel.cpp
)
add_library(topiary::topiary ALIAS topiary)

target_include_directories(topiary
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(topiary PUBLIC Eigen3::Eigen)

target_compile_options(topiary PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

topiary_enable_clang_tidy_on(topiary)

# --- Tests / benchmarks ---

if(TOPIARY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TOPIARY_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
