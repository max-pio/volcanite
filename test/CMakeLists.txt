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


macro(makeVolcaniteTest name)
    add_executable(${name} ${name}.cpp)

    target_link_libraries(${name} PRIVATE LibVVV::libvvv libryg-rans tclap::tclap libvolcanite)

    target_compile_definitions(${name} PRIVATE
            -DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 -DVULKAN_HPP_STORAGE_SHARED=1
            -DTEST_${name}=1, -DVOLCANITE_VERSION=\"${CMAKE_PROJECT_VERSION}\")

    if(HEADLESS)
        target_compile_definitions(${name} PUBLIC -DHEADLESS=1)
    endif()

    # add compile time list of paths to data/ directories:
    set(data_dirs "")
    # search for DATA_DIR property in link_library dependencies
    get_target_property(dependency_libs "${name}" LINK_LIBRARIES)
    foreach(lib IN LISTS dependency_libs)
        get_target_property(${lib}_data_dir ${lib} INTERFACE_DATA_DIR)
        if (NOT ${lib}_data_dir STREQUAL ${lib}_data_dir-NOTFOUND)
            list(APPEND data_dirs ${${lib}_data_dir})
        endif()
    endforeach()
    # add default [project]/data/ and arguments to data_dirs
    list(APPEND data_dirs ${CMAKE_CURRENT_LIST_DIR}/data)
    # list(APPEND data_dirs ${ARGN})
    message("data paths for ${name}: ${data_dirs}")
    # these are used to find data/ files when binary is run without installing or packaging
    list(JOIN data_dirs "\;" data_dirs_escaped)
    target_compile_definitions(${name} PRIVATE "-DDATA_DIRS=\"${data_dirs_escaped}\"")

    add_test(NAME ${name}  COMMAND ${name} ${ARGN})
endmacro(makeVolcaniteTest)

makeVolcaniteTest(csgv_test)
makeVolcaniteTest(csgv_gpu_test)
makeVolcaniteTest(volcanite_render_test)
