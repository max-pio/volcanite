cmake_minimum_required(VERSION 3.16)
project(vvvwindow VERSION 0.1 DESCRIPTION "GFLW GUI window for VVV applications" LANGUAGES CXX)

set(HEADERS
        include/vvvwindow/App.hpp
        include/vvvwindow/GuiImgui.hpp
        include/vvvwindow/entrypoint.hpp
        include/vvvwindow/tf/TransferFunction1DWidget.hpp
        include/vvvwindow/tf/TransferFunction2DWidget.hpp
        include/vvvwindow/tf/TransferFunctionSegmentedVolumeWidget.hpp
        )

set(SOURCES
        src/App.cpp
        src/GuiImgui.cpp
        src/entrypoint.cpp
        src/tf/TransferFunction1DWidget.cpp
        src/tf/TransferFunction2DWidget.cpp
        src/tf/TransferFunctionSegmentedVolumeWidget.cpp
        )

# create library target
add_library(libvvvwindow STATIC ${HEADERS} ${SOURCES})
# add an alias so that library can be used inside the build tree, e.g. when testing
add_library(LibVVV::libvvvwindow ALIAS libvvvwindow)

# add defines
target_compile_definitions(libvvvwindow PRIVATE -DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_compile_definitions(libvvvwindow PRIVATE -DVULKAN_HPP_STORAGE_SHARED=1)

# find other packages
#set(THREADS_PREFER_PTHREAD_FLAG ON)
#find_package(Threads REQUIRED)
find_package(glfw3 REQUIRED)
#if(NOT GLFW_FOUND)
#    # stop the CMake creation if GLFW is not available here
#    message(FATAL_ERROR "Cannot find required GLFW3 package. Set the CMake option HEADLESS for building without any GUI Application requirements.")
#endif()
target_compile_definitions(libvvvwindow PUBLIC -DGLFW=1)

# link libraries
target_link_libraries(libvvvwindow
        PUBLIC LibVVV::libvvv
        PRIVATE ${GLFW_LIBRARIES}
        PRIVATE ${CMAKE_DL_LIBS}
        PRIVATE tclap::tclap
#        PRIVATE Threads::Threads
        )
if (TARGET imgui)
    target_link_libraries(libvvvwindow PUBLIC imgui PRIVATE glfw)
else()
    message(WARNING "target imgui not found")
endif()

target_include_directories(libvvvwindow
        PRIVATE
        imgui
        PUBLIC
        # include paths depend on installed or build library
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )

# add data/ folder as target_property to install() by each project
set_target_properties(libvvvwindow PROPERTIES DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
