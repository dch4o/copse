# CPack configuration for Debian (.deb) packages, built from the install rules.
#
# Split per Debian convention into two packages:
#
#   libcopse<soversion>  (runtime) — the versioned shared object only
#   libcopse-dev         (dev)     — headers, the .so devlink, and the CMake
#                                    package config; depends on the runtime
#                                    package at the exact same version
#
# Produce both from a shared build (needs dpkg-shlibdeps from dpkg-dev):
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
#   cmake --build build
#   cpack -G DEB --config build/CPackConfig.cmake -B dist

set(CPACK_PACKAGE_NAME             "copse")
set(CPACK_PACKAGE_VENDOR           "Dohoon Cho")
set(CPACK_PACKAGE_VERSION          "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Fast, dependency-free fixed-capacity kd-tree for streaming spatial data")
set(CPACK_PACKAGE_HOMEPAGE_URL     "https://github.com/dch4o/copse")
set(CPACK_PACKAGE_CONTACT          "Dohoon Cho <dohooncho@gmail.com>")
set(CPACK_RESOURCE_FILE_LICENSE    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# Install under /usr (apt-standard), not the CMake default /usr/local.
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

# DEB generator. Maintainer and Homepage default from CPACK_PACKAGE_CONTACT and
# CPACK_PACKAGE_HOMEPAGE_URL above; shlibdeps fills in the libc/libstdc++ deps.
set(CPACK_GENERATOR                "DEB")
set(CPACK_DEB_COMPONENT_INSTALL    ON)
set(CPACK_COMPONENTS_ALL           runtime dev)    # else a stray "Unspecified" package
set(CPACK_DEBIAN_FILE_NAME         "DEB-DEFAULT")   # lib...<ver>_<arch>.deb
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME    "libcopse${PROJECT_VERSION_MAJOR}")
set(CPACK_DEBIAN_RUNTIME_PACKAGE_SECTION "libs")

set(CPACK_DEBIAN_DEV_PACKAGE_NAME    "libcopse-dev")
set(CPACK_DEBIAN_DEV_PACKAGE_SECTION "libdevel")
set(CPACK_DEBIAN_DEV_PACKAGE_DEPENDS "libcopse${PROJECT_VERSION_MAJOR} (= ${PROJECT_VERSION})")

include(CPack)
