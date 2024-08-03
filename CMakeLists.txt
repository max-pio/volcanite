cmake_minimum_required(VERSION 3.16)

project(vvv-volcanite VERSION 0.3.0 DESCRIPTION "Volcanite Segmentation Volume Rendering Framework" LANGUAGES C CXX)

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

# Use target folders to organize the copy interface targets. This allows to hide the targets easier in certain IDEs.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
# The makeExecutable is used to create project executables with all CMake and compile dependencies
include(makeExecutable)

# In headless mode, no windowing system integration is build.
option(HEADLESS "Build without GUI and windowing functionality to run only in --headless mode." OFF)

# Fetch all dependencies, including the libraries in the ./extern subdirectory.
include(CMake/Dependencies.cmake)

# PROJECTS  ------------------------------------------------------------------------------------------------------------

option(MAKE_EXAMPLES "Build examples for the VVV Framework" ON)
if (MAKE_EXAMPLES)
    if (HEADLESS)
        message(WARNING "MAKE_EXAMPLES is ON but cannot be used in combination with HEADLESS. Ignoring.")
    else()
        add_subdirectory(projects/examples)
    endif()
endif()


add_subdirectory(volcanite)


# PACKAGING ------------------------------------------------------------------------------------------------------------

# cpack is used for packaging. run `ninja package` in build directory to generate packages
include(CPack)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/opt/volcanite" CACHE PATH "..." FORCE)
endif()
