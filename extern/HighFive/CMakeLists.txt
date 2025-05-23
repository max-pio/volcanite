cmake_minimum_required(VERSION 3.13)
if(${CMAKE_VERSION} VERSION_LESS 3.13)
  cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
else()
  cmake_policy(VERSION 3.13)
endif()

project(HighFive VERSION 2.6.2)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/highfive/H5Version.hpp.in
               ${CMAKE_CURRENT_BINARY_DIR}/include/highfive/H5Version.hpp)
# INCLUDES
list(APPEND CMAKE_MODULE_PATH
  ${PROJECT_SOURCE_DIR}/CMake
  ${PROJECT_SOURCE_DIR}/CMake/portability
  ${PROJECT_SOURCE_DIR}/CMake/config)

# OPTIONS
# Compatibility within Highfive 2.x series
set(USE_BOOST OFF CACHE BOOL "Enable Boost Support")
set(USE_EIGEN OFF CACHE BOOL "Enable Eigen testing")
set(USE_XTENSOR OFF CACHE BOOL "Enable xtensor testing")
set(USE_OPENCV OFF CACHE BOOL "Enable OpenCV testing")
mark_as_advanced(USE_BOOST USE_EIGEN USE_XTENSOR)

set(HIGHFIVE_UNIT_TESTS AUTO CACHE STRING "Enable unit tests (requires Catch2 to be present)")
set_property(CACHE HIGHFIVE_UNIT_TESTS PROPERTY STRINGS AUTO ON OFF)

option(HIGHFIVE_USE_BOOST "Enable Boost Support" ${USE_BOOST})
option(HIGHFIVE_USE_HALF_FLOAT "Enable half-precision floats" ${USE_HALF_FLOAT})
option(HIGHFIVE_USE_EIGEN "Enable Eigen testing" ${USE_EIGEN})
option(HIGHFIVE_USE_OPENCV "Enable OpenCV testing" ${USE_OPENCV})
option(HIGHFIVE_USE_XTENSOR "Enable xtensor testing" ${USE_XTENSOR})
option(HIGHFIVE_EXAMPLES "Compile examples" ON)
option(HIGHFIVE_PARALLEL_HDF5 "Enable Parallel HDF5 support" OFF)
option(HIGHFIVE_BUILD_DOCS "Enable documentation building" ON)

# In deployments we probably don't want/cant have dynamic dependencies
option(HIGHFIVE_USE_INSTALL_DEPS "End applications by default use detected dependencies here" OFF)
mark_as_advanced(HIGHFIVE_USE_INSTALL_DEPS)


# Check compiler cxx_std requirements
# -----------------------------------

if(CMAKE_CXX_STANDARD EQUAL 98)
    message(FATAL_ERROR "HighFive needs to be compiled with at least C++11")
endif()

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

if(HIGHFIVE_USE_XTENSOR)
    set(CMAKE_CXX_STANDARD 14)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()


# Search dependencies (hdf5, boost, eigen, xtensor, mpi) and build target highfive_deps
include(${PROJECT_SOURCE_DIR}/CMake/HighFiveTargetDeps.cmake)

# Set-up HighFive to be used in 3rd party project using exports. Create a HighFive target
include(${PROJECT_SOURCE_DIR}/CMake/HighFiveTargetExport.cmake)

# Installation of headers (HighFive is only interface)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION "include"
  PATTERN "*.in" EXCLUDE)

# Installation of configured headers
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/
  DESTINATION "include")


# Preparing local building (tests, examples)
# ------------------------------------------

# Disable test if Boost was expressly disabled, or if HighFive is a sub-project
if (NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  if(HIGHFIVE_UNIT_TESTS)
    message(WARNING "Unit tests have been DISABLED.")
  endif()
  set(HIGHFIVE_UNIT_TESTS FALSE)
endif()

if(HIGHFIVE_UNIT_TESTS)
  if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/deps/catch2/CMakeLists.txt)
    add_subdirectory(deps/catch2 EXCLUDE_FROM_ALL)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/deps/catch2/contrib)
  else()
    find_package(Catch2)
    if(NOT HIGHFIVE_UNIT_TESTS STREQUAL "AUTO" AND HIGHFIVE_UNIT_TESTS AND NOT Catch2_FOUND)
      message(FATAL_ERROR "Please provide a Catch2 installation or clone the submodule")
    elseif(NOT Catch2_FOUND)
      message(WARNING "No Catch2 installation was found; Disabling unit tests.")
      set(HIGHFIVE_UNIT_TESTS OFF)
    endif()
  endif()
endif()


if(CMAKE_CXX_COMPILER_ID MATCHES "Intel")
  # ICC gets mad if we shorten "int"s
  add_definitions("-wd1682")
endif()

# Set compile time flags _after_ including required dependencies
include(ReleaseDebugAutoFlags)

if(HIGHFIVE_EXAMPLES)
  add_subdirectory(src/examples)
endif()

if(HIGHFIVE_UNIT_TESTS)
  enable_testing()
  add_subdirectory(tests/unit)
endif()

if(HIGHFIVE_BUILD_DOCS)
  add_subdirectory(doc)
endif()
