#  Copyright (C) 2024, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
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
project(vvvwindow VERSION 0.1 DESCRIPTION "GFLW GUI window for VVV applications" LANGUAGES CXX)

set(HEADERS
        include/vvvwindow/App.hpp
        include/vvvwindow/GLFWCameraController.hpp
        include/vvvwindow/GuiImgui.hpp
        include/vvvwindow/entrypoint.hpp
        include/vvvwindow/tf/TransferFunction1DWidget.hpp
        include/vvvwindow/tf/TransferFunctionSegmentedVolumeWidget.hpp
)

set(SOURCES
        src/App.cpp
        src/GLFWCameraController.cpp
        src/GuiImgui.cpp
        src/entrypoint.cpp
        src/tf/TransferFunction1DWidget.cpp
        src/tf/TransferFunctionSegmentedVolumeWidget.cpp
)

# create library target
add_library(libvvvwindow STATIC ${HEADERS} ${SOURCES})
# add an alias so that library can be used inside the build tree, e.g. when testing
add_library(LibVVV::libvvvwindow ALIAS libvvvwindow)

# add defines
target_compile_definitions(libvvvwindow PRIVATE -DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_compile_definitions(libvvvwindow PRIVATE -DVULKAN_HPP_STORAGE_SHARED=1)
target_compile_definitions(libvvvwindow PRIVATE -DGLFW_INCLUDE_NONE)

# find other packages
#set(THREADS_PREFER_PTHREAD_FLAG ON)
#find_package(Threads REQUIRED)
#find_package(glfw3 REQUIRED)
#if(NOT GLFW_FOUND)
#    # stop the CMake creation if GLFW is not available here
#    message(FATAL_ERROR "Cannot find required GLFW3 package. Set the CMake option HEADLESS for building without any GUI Application requirements.")
#endif()
target_compile_definitions(libvvvwindow PUBLIC -DGLFW=1)

# link libraries
target_link_libraries(libvvvwindow
        PUBLIC LibVVV::libvvv
        PRIVATE glfw ${CMAKE_DL_LIBS} tclap::tclap portable_file_dialogs fmt::fmt
        #        PRIVATE Threads::Threads
)
if (TARGET imgui)
    target_link_libraries(libvvvwindow PUBLIC imgui PRIVATE glfw)
else ()
    message(WARNING "target imgui not found")
endif ()

target_include_directories(libvvvwindow
        PRIVATE imgui
        PUBLIC
        # include paths depend on installed or build library
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# add data/ folder as target_property to install() by each project
set_target_properties(libvvvwindow PROPERTIES INTERFACE_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)

