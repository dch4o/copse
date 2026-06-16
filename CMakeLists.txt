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
option(COPSE_INSTALL          "Generate install and package-config rules"         ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(ClangTidy)
include(GNUInstallDirs)

# --- Dependencies ---

# Eigen — benchmark-only: the ikd-Tree comparison baseline (and its PCL-free
# shim) need it. The library, tests, and Catch2 benches are Eigen-free.
if(COPSE_BUILD_BENCHMARKS)
    find_package(Eigen3 5.0 REQUIRED NO_MODULE)
endif()

# Catch2 v3 — unit tests and benchmarks. Resolved via cmake/FindCatch2.cmake
# (module mode): tries CONFIG first, falls back to FetchContent so the build is
# self-contained.
if(COPSE_BUILD_TESTS OR COPSE_BUILD_BENCHMARKS)
    find_package(Catch2 REQUIRED)
endif()

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
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Propagate the C++20 requirement to consumers (set(CMAKE_CXX_STANDARD) does not).
target_compile_features(copse PUBLIC cxx_std_20)

# A generator expression is one ;-list: quote it and separate flags with ; so
# each reaches the compiler as its own argument (a space-joined string would be
# passed as a single bad option).
set(copse_warning_flags -Wall -Wextra -Wpedantic)
target_compile_options(copse PRIVATE
    "$<$<CXX_COMPILER_ID:GNU,Clang>:${copse_warning_flags}>"
)

copse_enable_clang_tidy_on(copse)

# --- Tests / benchmarks / demos ---

if(COPSE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(COPSE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(COPSE_BUILD_DEMOS)
    add_subdirectory(demo)
endif()

# --- Install / package config ---
#
# Lets downstreams consume copse identically via find_package(copse), or via
# add_subdirectory()/FetchContent (the copse::copse ALIAS works in-tree). copse
# has no public dependencies, so the generated config just pulls in the targets.

if(COPSE_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS copse
        EXPORT  copseTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(DIRECTORY include/copse DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    set(COPSE_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/copse)
    install(EXPORT copseTargets
        NAMESPACE   copse::
        FILE        copseTargets.cmake
        DESTINATION ${COPSE_CMAKE_DIR}
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copseConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/copseConfig.cmake
        INSTALL_DESTINATION ${COPSE_CMAKE_DIR}
    )
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/copseConfigVersion.cmake
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/copseConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/copseConfigVersion.cmake
        DESTINATION ${COPSE_CMAKE_DIR}
    )
endif()
