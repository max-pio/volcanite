cmake_minimum_required(VERSION 3.7)

project(TCLAP VERSION 1.4.0 LANGUAGES CXX)
set("PROJECT_DESCRIPTION" "A header-only templatized C++ command line parser library")
set("PROJECT_HOMEPAGE_URL" "https://sourceforge.net/projects/tclap/")

set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED True)
add_definitions(-DHAVE_TCLAP_CONFIG_H)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <strstream>
int main() { std::istrstream iss; }" TCLAP_HAVE_STRSTREAM)

check_cxx_source_compiles("#include <sstream>
int main() { std::istringstream iss; }" TCLAP_HAVE_SSTREAM)

set(GENERATED_CONFIG_NAME "TCLAPConfig.h")
set(GENERATED_CONFIG_BUILD_DIR "${PROJECT_BINARY_DIR}/tclap")
set(GENERATED_CONFIG_BUILD_PATH "${GENERATED_CONFIG_BUILD_DIR}/${GENERATED_CONFIG_NAME}")

configure_file(config.h.in "${GENERATED_CONFIG_BUILD_PATH}")

add_library(TCLAP INTERFACE)
target_include_directories(TCLAP INTERFACE
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
)


add_subdirectory(include/tclap)

set(CONFIG_FILE_NAME_WITHOUT_EXT "${PROJECT_NAME}Config")
set(CMAKE_CONFIG_VERSION_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_FILE_NAME_WITHOUT_EXT}-version.cmake")
set(CMAKE_CONFIG_FILE_NAME "${CONFIG_FILE_NAME_WITHOUT_EXT}.cmake")
set(CMAKE_CONFIG_DESTINATION "${CMAKE_INSTALL_LIBDIR_ARCHIND}/cmake/${PROJECT_NAME}")

if(${CMAKE_VERSION} VERSION_GREATER "3.14")
	set(OPTIONAL_ARCH_INDEPENDENT "ARCH_INDEPENDENT")
endif()

include(CMakePackageConfigHelpers)
write_basic_package_version_file("${CMAKE_CONFIG_VERSION_FILE_NAME}"
    COMPATIBILITY AnyNewerVersion
    ${OPTIONAL_ARCH_INDEPENDENT}
)
