# Eigen — header-only linear algebra; the canonical point type.
find_package(Eigen3 5.0 REQUIRED NO_MODULE)

# Catch2 v3 — used for both unit tests and benchmarks.
# Resolved via `cmake/FindCatch2.cmake` (module mode): tries CONFIG first,
# falls back to FetchContent so the build is self-contained.
if(TOPIARY_BUILD_TESTS OR TOPIARY_BUILD_BENCHMARKS)
    find_package(Catch2 REQUIRED)
endif()
