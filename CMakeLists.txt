#  Copyright (C) 2024, Max Piochowiak, Karlsruhe Institute of Technology
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <https:#www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.16)

project(vvv-volcanite VERSION 0.4.0 DESCRIPTION "Volcanite Segmentation Volume Rendering Framework" LANGUAGES C CXX)

if (MSVC)
    message(FATAL_ERROR "MSVC compiler is currently not supported as it only provides OpenMP version < 4.5")
endif (MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fopenmp")

# BUILD SETUP ----------------------------------------------------------------------------------------------------------

# Require out-of-source builds
file(TO_CMAKE_PATH "${PROJECT_BINARY_DIR}/CMakeLists.txt" LOC_PATH)
if (EXISTS "${LOC_PATH}")
    message(FATAL_ERROR "Builds in source directories are permitted. Create a separate build directory.")
endif ()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/CMake)

add_compile_definitions(PROJECT_VERSION=${PROJECT_VERSION} PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
                        PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR} PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
                        PROJECT_VERSION_TWEAK=${PROJECT_VERSION_TWEAK})

# Use C++20 in all targets
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optionally enable the Address Sanitzer for efficient checks for memory leaks, invalid pointers etc.
option(ENABLE_ADDRESS_SANITIZER "Enable Address Sanitizer" OFF)
if (ENABLE_ADDRESS_SANITIZER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g -DLSAN_OPTIONS=verbosity=1:log_threads=1")
endif()

# Use target folders to organize the copy interface targets. This allows to hide the targets easier in certain IDEs.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
# The makeExecutable is used to create project executables with all CMake and compile dependencies
include(makeExecutable)

# In headless mode, no windowing system integration is build.
option(HEADLESS "Build without GUI and windowing functionality to run only in --headless mode." OFF)

# Fetch all dependencies, including the libraries in the ./extern subdirectory.
include(CMake/Dependencies.cmake)

# add Volcanite executable with its sources, includes, shaders
add_subdirectory(volcanite)

# TESTS ----------------------------------------------------------------------------------------------------------------

option(VOLCANITE_BUILD_TESTS "Build Tests" ON)
if (VOLCANITE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# PACKAGING ------------------------------------------------------------------------------------------------------------

# cpack is used for packaging. run `ninja package` in build directory to generate packages
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
SET(CPACK_COMPONENTS_ALL applications)
include(CPack)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/opt/volcanite" CACHE PATH "..." FORCE)
endif()
