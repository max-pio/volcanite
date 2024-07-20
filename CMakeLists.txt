cmake_minimum_required(VERSION 3.16)
project(vvv-volcanite VERSION 0.3.0 DESCRIPTION "Volcanite Segmentation Volume Rendering Framework" LANGUAGES C CXX)
if (MSVC)
    message(FATAL_ERROR "MSVC compiler is currently not supported as it only provides OpenMP version < 4.5")
endif (MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fopenmp")

# GENERAL BUILD CONFIG -------------------------------------------------------------------------------------------------

# Require out-of-source builds
file(TO_CMAKE_PATH "${PROJECT_BINARY_DIR}/CMakeLists.txt" LOC_PATH)
if (EXISTS "${LOC_PATH}")
    message(FATAL_ERROR "You cannot build in a source directory (or any directory with a CMakeLists.txt file).
                         Please make a build subdirectory. Feel free to remove CMakeCache.txt and CMakeFiles.")
endif ()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/CMake)

add_compile_definitions(PROJECT_VERSION=${PROJECT_VERSION} PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
                        PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR} PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
                        PROJECT_VERSION_TWEAK=${PROJECT_VERSION_TWEAK})

# [DEPRECATED] ToDo: python binding support in CMake files
# Note: all SKBUILD blocks are python-binding specific
#if (SKBUILD)
#    # Scikit-Build does not add your site-packages to the search path automatically,
#    # so we need to add it _or_ the pybind11 specific directory here.
#    execute_process(
#            COMMAND
#            "${PYTHON_EXECUTABLE}" -c
#            "import pybind11; print(pybind11.get_cmake_dir())"
#            OUTPUT_VARIABLE _tmp_dir
#            OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ECHO STDOUT)
#    list(APPEND CMAKE_PREFIX_PATH "${_tmp_dir}")
#    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
#    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
#endif ()

# Use C++20 in all targets
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optionally enable the Address Sanitzer for efficient checks for memory leaks, invalid pointers etc.
option(ENABLE_ADDRESS_SANITIZER "Enable Address Sanitizer" OFF)
if (ENABLE_ADDRESS_SANITIZER)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g -DLSAN_OPTIONS=verbosity=1:log_threads=1")
endif()

# we use target folders to organize the copy targets. This allows to hide the targets easier in certain IDEs
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
# the makeExecutable is used to create project executables with all CMake and compile dependencies
include(makeExecutable)

option(HEADLESS "Build without GUI and windowing functionality to run only in --headless mode." OFF)

# LIBRARIES ------------------------------------------------------------------------------------------------------------

# TODO: replace shipped libraries with CMake FetchContent calls

# GLM
add_subdirectory(extern/glm)

# add extern public imgui library
if(NOT HEADLESS)
    add_subdirectory(extern/imgui)
endif()

# add extern HighFive simplified hdf5 library if libhdf5-dev is installed
option(USE_HDF5_SUPPORT  "Includes the hdf5 library for importing and exporting .hdf5 files" ON)
if (USE_HDF5_SUPPORT)
    find_package(HDF5)
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
        message(WARNING "USE_HDF5_SUPPORT was set but hdf5 library could not be found!")
    endif ()
endif ()


# add vtk library to load vti volume/image files
option(USE_VTK_SUPPORT  "Includes the vtk library for importing and exporting .vti files" ON)
if (USE_VTK_SUPPORT)
    find_package(VTK COMPONENTS CommonCore IOXML)
    if (VTK_FOUND)
        # ...
    else ()
        message(WARNING "USE_VTK_SUPPORT was set but vtk library could not be found!")
    endif ()
endif ()

# add extern SQLiteCpp library if libsqlite3-dev is installed
option(USE_SQLite3 "Includes the SQLite3 library for importing and exporting .sqlite files" ON)
if (USE_SQLite3)
    find_package(SQLite3)
    if (SQLite3_FOUND)
        set(SQLITECPP_RUN_CPPLINT OFF CACHE INTERNAL "")
        add_subdirectory(extern/SQLiteCpp)
    else ()
        message(WARNING "USE_SQLite3 was set but SQLite3 library could not be found")
    endif ()
endif ()

# TCLAP for command line argument parsing (used by multiple subprojects)
add_subdirectory(extern/tclap)
add_library(tclap::tclap ALIAS TCLAP)

# Add vvv and vvv glfw application libraries
add_subdirectory(vvv)
if(NOT HEADLESS)
    find_package(glfw3 REQUIRED)
#    # GLFW for windowing system integration
#    set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
#    set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "")
#    set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "")
#    set(GLFW_INSTALL OFF CACHE INTERNAL "")
#    add_subdirectory(extern/glfw)
    add_subdirectory(vvv-glfw-app)
endif()

option(MAKE_EXAMPLES "Build examples for the VVV Framework" ON)

if (MAKE_EXAMPLES)
    if (HEADLESS)
        message(WARNING "MAKE_EXAMPLES is ON but cannot be used in combination with HEADLESS. Ignoring.")
    else()
        add_subdirectory(projects/examples)
    endif()
endif()

# add extern rANS encoding library
add_subdirectory(extern/ryg_rans)

# add extern portable file dialog
if(NOT HEADLESS)
    add_subdirectory(extern/portable-file-dialogs)
endif()

# PROJECTS  ------------------------------------------------------------------------------------------------------------

add_subdirectory(projects/volcanite)


# PACKAGING ------------------------------------------------------------------------------------------------------------

# cpack is used for packaging. run `ninja package` in build directory to generate packages
include(CPack)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/opt/volcanite" CACHE PATH "..." FORCE)
endif()
