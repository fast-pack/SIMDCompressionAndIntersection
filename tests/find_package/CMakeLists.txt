# Minimal downstream project that consumes an installed
# SIMDCompressionAndIntersection via find_package(). Used by CI to verify that
# the install tree (library + headers + CMake package config) is usable.
#
# Point CMAKE_PREFIX_PATH at the install prefix, e.g.:
#   cmake -S tests/find_package -B build_consumer -DCMAKE_PREFIX_PATH=/your/prefix
#   cmake --build build_consumer
#   ./build_consumer/consumer
cmake_minimum_required(VERSION 3.10)
project(simdcomp_find_package_test CXX)

find_package(SIMDCompressionAndIntersection REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE
  SIMDCompressionAndIntersection::SIMDCompressionAndIntersection)
