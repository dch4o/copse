# Find-module shim for Catch2 v3.
#
# Resolution order:
#   1. Try config-mode: `find_package(Catch2 3 CONFIG QUIET)` so a system-installed
#      Catch2 (Catch2Config.cmake) wins. CONFIG mode does *not* recurse into this
#      find-module, so there is no infinite loop.
#   2. Fall back to FetchContent so the build is self-contained on hosts without it.
#
# Either path leaves `Catch2::Catch2`, `Catch2::Catch2WithMain`, and the
# `Catch.cmake` extras (for `catch_discover_tests`) available to callers.
#
# Use:
#   list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
#   find_package(Catch2 REQUIRED)   # module mode picks this file up.

find_package(Catch2 3 CONFIG QUIET)

if(NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    set(Catch2_FOUND TRUE)
endif()

# `extras/Catch.cmake` provides `catch_discover_tests`. It ships with both the
# system install and the FetchContent payload; locate it via the imported
# target's source dir when possible, otherwise rely on the package's own
# CMAKE_MODULE_PATH contribution from CONFIG mode.
if(DEFINED catch2_SOURCE_DIR)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
include(Catch)
