# Optional clang-tidy integration.
#
# When COPSE_ENABLE_CLANG_TIDY is ON, clang-tidy is invoked as part of the build
# for first-party targets only (i.e., not for FetchContent'd Catch2). CMake
# wires clang-tidy through CXX_CLANG_TIDY which runs per translation unit, so
# incremental builds re-tidy only the TUs that actually recompile.
#
# Use:
#   include(clang_tidy)
#   copse_enable_clang_tidy_on(<target> [<target>...])

option(COPSE_ENABLE_CLANG_TIDY "Run clang-tidy on first-party targets during the build" OFF)

if(COPSE_ENABLE_CLANG_TIDY)
    find_program(COPSE_CLANG_TIDY_EXE
        NAMES clang-tidy
        DOC "Path to clang-tidy"
        REQUIRED
    )
    message(STATUS "clang-tidy enabled: ${COPSE_CLANG_TIDY_EXE}")
endif()

function(copse_enable_clang_tidy_on)
    if(NOT COPSE_ENABLE_CLANG_TIDY)
        return()
    endif()
    foreach(tgt IN LISTS ARGN)
        # TODO: optionally pass --warnings-as-errors=* once the codebase is clean.
        set_target_properties(${tgt} PROPERTIES
            CXX_CLANG_TIDY "${COPSE_CLANG_TIDY_EXE}"
        )
    endforeach()
endfunction()
