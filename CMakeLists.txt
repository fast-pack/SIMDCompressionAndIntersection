cmake_minimum_required(VERSION 3.10)

project(SIMDCompressionAndIntersection
        VERSION 0.1.0
        LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Standards
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Is this the top-level project, or are we being added via add_subdirectory()?
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(SIMDCOMP_IS_TOP_LEVEL ON)
else()
  set(SIMDCOMP_IS_TOP_LEVEL OFF)
endif()

option(SIMDCOMP_BUILD_TESTS
  "Build the test, benchmark and example executables" ${SIMDCOMP_IS_TOP_LEVEL})
option(SIMDCOMP_INSTALL
  "Generate install rules for SIMDCompressionAndIntersection" ${SIMDCOMP_IS_TOP_LEVEL})

# Defines CMAKE_INSTALL_{LIB,INCLUDE,BIN}DIR; needed before they are referenced
# in the library's INSTALL_INTERFACE include directories below.
include(GNUInstallDirs)

# Release builds define NDEBUG (disabling assertions), which is the CMake
# default for the Release / RelWithDebInfo / MinSizeRel configurations.

# ---------------------------------------------------------------------------
# SIMD flags
#
# On x86/x64 the code is built for AVX (AVX itself is not required at runtime,
# but the default build assumes it, matching the Makefile). On AArch64 the SSE
# intrinsics are mapped to ARM NEON via include/neon_sse.h, and NEON is baseline
# so no architecture flag is needed.
# ---------------------------------------------------------------------------
set(SIMD_FLAGS "")
if(NOT MSVC)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "(^|;)(x86_64|amd64|AMD64|i.86|x86)$")
    set(SIMD_FLAGS -mavx)
  endif()
endif()

if(NOT MSVC)
  add_compile_options(
    ${SIMD_FLAGS}
    -Wall -Wextra -Wsign-compare -Wwrite-strings -Wpointer-arith
    -Winit-self -Wno-sign-conversion)
  # -Weffc++ and -pedantic only on the C++ sources (matches the Makefile).
  add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-Weffc++>
    $<$<COMPILE_LANGUAGE:CXX>:-pedantic>
    $<$<COMPILE_LANGUAGE:C>:-pedantic>)
else()
  add_compile_options(/arch:AVX)
endif()

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
set(SIMDCOMP_SOURCES
  src/codecfactory.cpp
  src/bitpacking.cpp
  src/integratedbitpacking.cpp
  src/simdbitpacking.cpp
  src/usimdbitpacking.cpp
  src/simdintegratedbitpacking.cpp
  src/intersection.cpp
  src/varintdecode.c
  src/streamvbyte.c
  src/simdpackedsearch.c
  src/simdpackedselect.c
  src/frameofreference.cpp
  src/for.c)

add_library(SIMDCompressionAndIntersection STATIC ${SIMDCOMP_SOURCES})
# Same-name namespaced alias so consumers can use the identical target name
# whether they add_subdirectory() this project or find_package() an install.
add_library(SIMDCompressionAndIntersection::SIMDCompressionAndIntersection
            ALIAS SIMDCompressionAndIntersection)
target_include_directories(SIMDCompressionAndIntersection PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/SIMDCompressionAndIntersection>)

# ---------------------------------------------------------------------------
# Test / benchmark / example executables (optional)
# ---------------------------------------------------------------------------
if(SIMDCOMP_BUILD_TESTS)
  foreach(prog unit testcodecs testintegration benchintersection benchsearch)
    add_executable(${prog} src/${prog}.cpp)
    target_link_libraries(${prog} PRIVATE SIMDCompressionAndIntersection)
  endforeach()

  add_executable(example example.cpp)
  target_link_libraries(example PRIVATE SIMDCompressionAndIntersection)

  # Advanced benchmarking tools
  foreach(prog simplesynth compress uncompress budgetedtest ramtocache entropy compflatstat)
    add_executable(${prog} advancedbenchmarking/src/${prog}.cpp)
    target_link_libraries(${prog} PRIVATE SIMDCompressionAndIntersection)
    target_include_directories(${prog} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/advancedbenchmarking/include)
  endforeach()

  enable_testing()
  add_test(NAME unit COMMAND unit)
  add_test(NAME testintegration COMMAND testintegration)
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
if(SIMDCOMP_INSTALL)
  include(CMakePackageConfigHelpers)

  set(SIMDCOMP_INSTALL_CMAKEDIR
      "${CMAKE_INSTALL_LIBDIR}/cmake/SIMDCompressionAndIntersection"
      CACHE STRING "Path (relative to prefix) to install the CMake package config")

  # Library + export set.
  install(TARGETS SIMDCompressionAndIntersection
          EXPORT SIMDCompressionAndIntersectionTargets
          ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
          LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

  # Public headers (flat layout, matching the in-tree include/ directory).
  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
          DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/SIMDCompressionAndIntersection"
          FILES_MATCHING PATTERN "*.h")

  # Exported targets file -> SIMDCompressionAndIntersection::SIMDCompressionAndIntersection
  install(EXPORT SIMDCompressionAndIntersectionTargets
          FILE SIMDCompressionAndIntersectionTargets.cmake
          NAMESPACE SIMDCompressionAndIntersection::
          DESTINATION "${SIMDCOMP_INSTALL_CMAKEDIR}")

  # Package config + version files, so find_package() works.
  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SIMDCompressionAndIntersectionConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SIMDCompressionAndIntersectionConfig.cmake"
    INSTALL_DESTINATION "${SIMDCOMP_INSTALL_CMAKEDIR}")

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SIMDCompressionAndIntersectionConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SIMDCompressionAndIntersectionConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SIMDCompressionAndIntersectionConfigVersion.cmake"
    DESTINATION "${SIMDCOMP_INSTALL_CMAKEDIR}")
endif()
