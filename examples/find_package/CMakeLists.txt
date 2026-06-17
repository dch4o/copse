# Standalone example: consume an installed copse via find_package(copse).
# Build it against an install prefix, e.g.:
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/copse/prefix
#   cmake --build build && ./build/example
cmake_minimum_required(VERSION 3.20)
project(copse_find_package_example CXX)

find_package(copse REQUIRED)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE copse::copse)
