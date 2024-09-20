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

cmake_minimum_required(VERSION 3.16)
project(volcanite VERSION 0.2.0 DESCRIPTION "Volcanite Renderers" LANGUAGES CXX)

# set all sources and headers
set(VOLCANITE_HEADERS
        include/volcanite/CSGVPathUtils.hpp
        include/volcanite/VolcaniteArgs.hpp
        include/volcanite/StratifiedPixelSequence.hpp
        data/shader/cpp_glsl_include/csgv_constants.h
        include/volcanite/benchmark/CSGVBenchmarkPass.hpp
        include/volcanite/compression/VolumeCompressionBase.hpp
        include/volcanite/compression/encoder/CSGVBrickEncoder.hpp
        include/volcanite/compression/encoder/CSGVSerialBrickEncoder.hpp
        include/volcanite/compression/encoder/NibbleEncoder.hpp
        include/volcanite/compression/encoder/RangeANSEncoder.hpp
        include/volcanite/compression/encoder/WaveletMatrixEncoder.hpp
        include/volcanite/compression/CompressedSegmentationVolume.hpp
        include/volcanite/compression/CompSegVolHandler.hpp
        include/volcanite/compression/CSGVChunkMerger.hpp
        include/volcanite/compression/CSGVDatabase.hpp
        include/volcanite/compression/pack_nibble.hpp
        include/volcanite/compression/pack_rans.hpp
        include/volcanite/compression/pack_wavelet_matrix.hpp
        include/volcanite/compression/wavelet_tree/bit_reversal_permutation.hpp
        include/volcanite/compression/wavelet_tree/BitVector.hpp
        include/volcanite/compression/wavelet_tree/HuffmanWaveletMatrix.hpp
        include/volcanite/compression/wavelet_tree/WaveletMatrix.hpp
        include/volcanite/compression/wavelet_tree/WaveletMatrixBase.hpp
        include/volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp
        include/volcanite/renderer/CompressedSegmentationVolumeBrickViewer.hpp
        include/volcanite/renderer/PassCompSegVolRender.hpp
        include/volcanite/utility/segmentation_volume_synthesis.hpp
        )

set(VOLCANITE_SOURCES
        src/volcanite/benchmark/CSGVBenchmarkPass.cpp
        src/volcanite/compression/encoder/CSGVBrickEncoder.cpp
        src/volcanite/compression/encoder/CSGVSerialBrickEncoder.cpp
        src/volcanite/compression/encoder/NibbleEncoder.cpp
        src/volcanite/compression/encoder/RangeANSEncoder.cpp
        src/volcanite/compression/encoder/WaveletMatrixEncoder.cpp
        src/volcanite/compression/CompressedSegmentationVolume.cpp
        src/volcanite/compression/CompressedSegmentationVolumeStats.cpp
        src/volcanite/compression/CompressedSegmentationVolumeParallel.cpp
        src/volcanite/compression/pack_nibble.cpp
        src/volcanite/compression/pack_rans.cpp
        src/volcanite/compression/pack_wavelet_matrix.cpp
        src/volcanite/compression/wavelet_tree/prefix_counting.cpp
        src/volcanite/compression/wavelet_tree/HuffmanWaveletMatrix.cpp
        src/volcanite/compression/wavelet_tree/WaveletMatrix.cpp
        src/renderer/CompressedSegmentationVolumeRenderer.cpp
        src/renderer/CompressedSegmentationVolumeBrickViewer.cpp
        src/renderer/PassCompSegVolRender.cpp
        )

makeLibrary(libvolcanite ${VOLCANITE_HEADERS} ${VOLCANITE_SOURCES})
# add data/ folder as target_property to install() by each project
set_target_properties(libvolcanite PROPERTIES INTERFACE_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)

makeExecutable(csgv_brick_viewer)
installExecutable(csgv_brick_viewer)

makeExecutable(volcanite)
installExecutable(volcanite)

makeExecutable(csgv_benchmark)
installExecutable(csgv_benchmark)

makeExecutable(volcanite_synth_volume)
installExecutable(volcanite_synth_volume)
