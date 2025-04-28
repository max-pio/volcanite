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

cmake_minimum_required(VERSION 3.21)

project(volcanite VERSION 0.6.0 DESCRIPTION "Volcanite Segmentation Volume Rendering Framework" LANGUAGES C CXX)

set(PROJECT_COMPANY_NAME "Karlsruhe Institute of Technology")
set(PROJECT_COMPANY_NAMESPACE "edu.kit.ivd.cg")

# normalize install paths in newer CMake version
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

# specify install directories here to make them available in system dependent install commands
# use /opt/ on Linux since volcanite is self-contained and contains a shader asset folder
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_INSTALL_PREFIX "/opt/volcanite" CACHE PATH "..." FORCE)
    set(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
endif()


if (MSVC)
    message(FATAL_ERROR "MSVC compiler is currently not supported as it only provides OpenMP version < 4.5")
endif (MSVC)


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

# enable OpenMP
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fopenmp")

# Optionally enable the Address Sanitzer for efficient checks for memory leaks, invalid pointers etc.
option(ENABLE_ADDRESS_SANITIZER "Enable Address Sanitizer" OFF)
if (ENABLE_ADDRESS_SANITIZER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g -DLSAN_OPTIONS=verbosity=1:log_threads=1")
endif()

# In headless mode, no windowing system integration is build.
option(HEADLESS "Build without GUI and windowing functionality to run only in --headless mode." OFF)

# Fetch all dependencies, including the libraries in the ./extern subdirectory.
include(CMake/Dependencies.cmake)

# Use target folders to organize the copy interface targets. This allows to hide the targets easier in certain IDEs.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
# helper functions to create volcanite executables / libraries with all CMake and compile dependencies
include(makeExecutable)

# add Volcanite executable with its sources, includes, shaders
add_subdirectory(volcanite)

# TESTS ----------------------------------------------------------------------------------------------------------------

option(VOLCANITE_BUILD_TESTS "Build Tests" ON)
if (VOLCANITE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# PACKAGING ------------------------------------------------------------------------------------------------------------

set(CPACK_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")

# cpack is used for packaging. only the executable is packaged yet.
# run `cpack --config CPackConfig.cmake` or `ninja package` in build directory to generate packages
set(CPACK_SOURCE_PACKAGE_FILE_NAME ${CPACK_PACKAGE_NAME})
set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CPACK_PACKAGE_NAME})
set(CPACK_PACKAGE_VERSION_MAJOR ${CMAKE_PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${CMAKE_PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${CMAKE_PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_VENDOR "Max Piochowiak (Karlsruhe Institute of Technology)")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_LIST_DIR}/LICENSE)
set(CPACK_ARCHIVE_COMPONENT_INSTALL TRUE)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
set(CPACK_COMPONENTS_ALL applications)
set(CPACK_COMPONENT_APPLICATIONS_REQUIRED TRUE)
set(CPACK_COMPONENT_APPLICATIONS_DESCRIPTION "The volcanite segmentation volume renderer executable.")
set(CPACK_COMPONENT_LIBRARIES_REQUIRED TRUE)
set(CPACK_COMPONENT_APPLICATIONS_DESCRIPTION "Runtime libraries.")
set(CPACK_PACKAGE_EXECUTABLES volcanite;Volcanite)

# Linux settings for DEB generator
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_DEB_COMPONENT_INSTALL ON)
    # for some reason, the component specific CPACK_DEBIAN_APPLICATIONS_PACKAGE_* has no effect here
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION Graphics)
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_BINARY_DIR}/volcanite/postinst;${CMAKE_CURRENT_BINARY_DIR}/volcanite/prerm")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Max Piochowiak (Karlsruhe Institute of Technology)")
# Windows settings for NSIS generator
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CPACK_NSIS_DISPLAY_NAME ${CMAKE_PROJECT_NAME})
    set(CPACK_NSIS_PACKAGE_NAME ${CPACK_PACKAGE_NAME})
    set(CPACK_NSIS_MANIFEST_DPI_AWARE TRUE)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL YES)
    set(CPACK_NSIS_MODIFY_PATH TRUE)
    set(CPACK_NSIS_CONTACT "Max Piochowiak (Karlsruhe Institute of Technology)")
    set(CPACK_NSIS_BRANDING_TEXT "Volcanite (c) Max Piochowiak")
    set(CPACK_NSIS_WELCOME_TITLE "Volcanite Segmentation Volume Renderer")
    set(CPACK_NSIS_FINISH_TITLE "Volcanite Segmentation Volume Renderer")
    set(CPACK_NSIS_MUI_HEADERIMAGE "${CMAKE_CURRENT_LIST_DIR}\\volcanite\\package_assets\\windows\\nsis_banner_top.bmp")
    set(CPACK_NSIS_MUI_WELCOMEFINISHPAGE_BITMAP "${CMAKE_CURRENT_LIST_DIR}\\volcanite\\package_assets\\windows\\nsis_banner_side.bmp")
    set(CPACK_NSIS_MUI_UNWELCOMEFINISHPAGE_BITMAP "${CMAKE_CURRENT_LIST_DIR}\\volcanite\\package_assets\\windows\\nsis_banner_side.bmp")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_LIST_DIR}\\volcanite\\package_assets\\icons\\volcanite_icon_installer_256.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_LIST_DIR}\\volcanite\\package_assets\\icons\\volcanite_icon_uninstaller_256.ico")
endif()

# Generator selection per platform
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_GENERATOR TGZ DEB)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CPACK_GENERATOR ZIP NSIS)
else ()
    set(CPACK_GENERATOR ZIP)
endif ()

include(CPack)
