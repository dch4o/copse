cmake_minimum_required(VERSION 3.20)

project(copse
    VERSION   0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

option(COPSE_BUILD_TESTS      "Build copse test suite" ON)
option(COPSE_BUILD_BENCHMARKS "Build copse benchmarks" ON)
option(COPSE_BUILD_DEMOS      "Build copse demos (pulls Polyscope; needs GL/X11)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(Dependencies)
include(ClangTidy)

# --- Library ---

add_library(copse
    src/kd_tree.cpp
    src/kd_tree_impl.cpp
    src/point_store.cpp
    src/leaf_bucket.cpp
    src/tree_builder.cpp
    src/search_kernel.cpp
)
add_library(copse::copse ALIAS copse)

target_include_directories(copse
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(copse PUBLIC Eigen3::Eigen)

target_compile_options(copse PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

copse_enable_clang_tidy_on(copse)

# --- Tests / benchmarks ---

if(COPSE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(COPSE_BUILD_BENCHMARKS)
    include(IkdTree)
    add_subdirectory(benchmarks)
endif()

if(COPSE_BUILD_DEMOS)
    add_subdirectory(demo)
endif()
