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


macro(makeTest name)
    add_executable(${name} ${name}.cpp)

    target_link_libraries(${name} PRIVATE LibVVV::libvvv libryg-rans tclap::tclap libvolcanite)

    target_compile_definitions(${name} PRIVATE
            -DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 -DVULKAN_HPP_STORAGE_SHARED=1
            -DTEST_${name}=1, -DVOLCANITE_VERSION=\"${CMAKE_PROJECT_VERSION}\")

    if(HEADLESS)
        target_compile_definitions(${name} PUBLIC -DHEADLESS=1)
    endif()

    add_test(NAME ${name}  COMMAND ${name} ${ARGN})
endmacro(makeTest)

makeTest(csgv_test)
makeTest(csgv_random_access_test)
