################################################################################
# tests/CMakeLists.txt
#
# Copyright (C) 2021 Florian Kurpicz <florian@kurpicz.org>
#
# pasta::wavelet_tree is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# pasta::wavelet_tree is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with pasta::wavelet_tree.  If not, see <http://www.gnu.org/licenses/>.
#
################################################################################

macro(pasta_build_test TESTNAME)
  string(REPLACE "/" "_" TESTNAME_REPLACED "${TESTNAME}")
  add_executable(${TESTNAME_REPLACED}
    ${TESTNAME}.cpp)
  target_link_libraries(${TESTNAME_REPLACED} PRIVATE
    pasta_wavelet_tree
    tlx)
  add_test(
    NAME ${TESTNAME_REPLACED}
    COMMAND ${TESTNAME_REPLACED} ${ARGN})
endmacro(pasta_build_test)

pasta_build_test(wavelet_tree/wavelet_tree_test)

################################################################################
