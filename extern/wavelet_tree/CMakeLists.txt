################################################################################
# CMakeLists.txt
#
# Copyright (C) 2021 Florian Kurpicz <florian@kurpicz.org>
#
# pasta::wavelet_tree is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# pasta::wavelet_tree is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with pasta::wavelet_tree.  If not, see <http://www.gnu.org/licenses/>.
#
################################################################################

cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(pasta_wavelet_tree)

# Options when compiling pasta::wavelet_tree
## Build tests
option(PASTA_WAVELET_TREE_BUILD_TESTS
  "Build pasta::wavelet_tree's tests." OFF)
## Build benchmark tools
option(PASTA_WAVELET_TREE_BUILD_BENCHMAKRS
  "Build pasta::wavelet_tree's benchmarks." OFF)
## Build pasta::wavelet_tree with code coverage options
option(PASTA_WAVELET_TREE_COVERAGE_REPORTING
  "Enable coverage reporting for pasta::wavelet_tree" OFF)

# Add tlx subdirectory only, if it has not been added by another project already
if(NOT TARGET tlx)
  add_subdirectory(extlib/tlx)
endif()

# Add pasta::utils subdirectory only, if it has not been added by another
# project already
if (NOT TARGET pasta_bit_vector)
  add_subdirectory(extlib/bit_vector)
endif()

# Add pasta::utils subdirectory only, if it has not been added by another
# project already
if (NOT TARGET pasta_utils)
  add_subdirectory(extlib/utils)
endif()


# Optional code coverage (library compile options are only set if coverage
# reporting is enabled
add_library(pasta_wavelet_tree_coverage_config INTERFACE)
if(PASTA_BIT_VECTOR_COVERAGE_REPORTING)
  target_compile_options(pasta_wavelet_tree_coverage_config INTERFACE
    -fprofile-arcs
    -ftest-coverage)
  target_link_libraries(pasta_wavelet_tree_coverage_config INTERFACE
    gcov)
endif()

# pasta::bit_vector interface definitions
add_library(pasta_wavelet_tree INTERFACE)
target_include_directories(pasta_wavelet_tree INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${TLX_INCLUDE_DIRS})
target_link_libraries(pasta_wavelet_tree INTERFACE
  pasta_bit_vector
  pasta_utils
  pasta_wavelet_tree_coverage_config)

# Optional test
if(PASTA_WAVELET_TREE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

add_executable(benchmark
  benchmarks/benchmark.cpp)
target_link_libraries(benchmark PUBLIC
  pasta_wavelet_tree
  tlx
  pasta_utils
  optimized pasta_memory_monitor)

# Optional benchmarks

if(PASTA_WAVELET_TREE_BUILD_BENCHMAKRS)
  # find_library(SDSL_LIBRARY sdsl
  #   PATHS ${SDSL_DIR})

  # if(SDSL_LIBRARY)
  #   add_executable(bit_vector_benchmark
  #     benchmarks/bit_vector_benchmark.cpp)

  #   target_link_libraries(bit_vector_benchmark PUBLIC
  #     pasta_bit_vector
  #     tlx
  #     sdsl
  #     pasta_utils
  #     pasta_memory_monitor)
  # else()
  #   message(SEND_ERROR
  #     "SDSL-lite not found, please install the library or disable the\
  #      benchmark construction")
  # endif()
endif()

################################################################################
