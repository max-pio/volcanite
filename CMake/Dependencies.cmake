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

# TODO: we could replace shipped libraries with CMake FetchContent calls, but this would add git, ssh as dependencies

# extern GLM
add_subdirectory(extern/glm)

# extern rANS encoding library
add_subdirectory(extern/ryg_rans)

# extern TCLAP for command line argument parsing
add_subdirectory(extern/tclap)
add_library(tclap::tclap ALIAS TCLAP)

# extern HighFive simplified hdf5 library if libhdf5-dev is installed
option(ENABLE_HDF5_SUPPORT  "Includes the hdf5 library for importing and exporting .hdf5 files" ON)
if (ENABLE_HDF5_SUPPORT)
    find_package(HDF5 QUIET)
    if (HDF5_FOUND)
        # set HighFive CMake cache options
        set(USE_BOOST OFF CACHE INTERNAL "")
        set(USE_EIGEN OFF CACHE INTERNAL "")
        set(USE_XTENSOR OFF CACHE INTERNAL "")
        set(USE_OPENCV OFF CACHE INTERNAL "")
        mark_as_advanced(USE_BOOST USE_EIGEN USE_XTENSOR)

        set(HIGHFIVE_UNIT_TESTS OFF CACHE INTERNAL "")
        set(HIGHFIVE_EXAMPLES OFF CACHE INTERNAL "")
        set(HIGHFIVE_BUILD_DOCS OFF CACHE INTERNAL "")
        set(HDF5_PREFER_PARALLEL ON CACHE INTERNAL "")
        add_subdirectory(extern/HighFive)
    else ()
        message(WARNING "ENABLE_HDF5_SUPPORT was set but hdf5 library could not be found.")
    endif ()
endif ()

# vtk library to load vti volume/image files
option(ENABLE_VTK_SUPPORT  "Includes the vtk library for importing and exporting .vti files" ON)
if (ENABLE_VTK_SUPPORT)
    find_package(VTK COMPONENTS CommonCore IOXML QUIET)
    if (VTK_FOUND)
        # ...
    else ()
        message(WARNING "ENABLE_VTK_SUPPORT was set but vtk library could not be found.")
    endif ()
endif ()

# extern SQLiteCpp library if libsqlite3-dev is installed
option(ENABLE_SQLITE3_SUPPORT "Includes the SQLite3 library for importing and exporting .sqlite files" ON)
if (ENABLE_SQLITE3_SUPPORT)
    find_package(SQLite3 QUIET)
    if (SQLite3_FOUND)
        set(SQLITECPP_RUN_CPPLINT OFF CACHE INTERNAL "")
        add_subdirectory(extern/SQLiteCpp)
    else ()
        message(WARNING "ENABLE_SQLITE3_SUPPORT was set but SQLite3 library could not be found.")
    endif ()
endif ()

# Vulkan framework vvv for basic Vulkan integration
add_subdirectory(lib/vvv)

if(NOT HEADLESS)
    # GLFW libraries
    # find_package(glfw3 REQUIRED)
    # GLFW for windowing system integration
    set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
    set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "")
    set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "")
    set(GLFW_INSTALL OFF CACHE INTERNAL "")
    set(GLFW_BUILD_WAYLAND OFF)
    add_subdirectory(extern/glfw)

    # platform independent file dialogs
    add_subdirectory(extern/portable-file-dialogs)

    # extern IMGUI
    add_subdirectory(extern/imgui)

    # GLFW application
    add_subdirectory(lib/vvv-glfw-app)
endif()

