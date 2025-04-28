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
project(volcanite-renderer VERSION 0.6.0 DESCRIPTION "Volcanite Renderer Compression" LANGUAGES CXX)

# set all sources and headers
set(VOLCANITE_HEADERS
        include/volcanite/CSGVPathUtils.hpp
        include/volcanite/VolcaniteArgs.hpp
        include/volcanite/StratifiedPixelSequence.hpp
        data/shader/cpp_glsl_include/csgv_constants.incl
        include/volcanite/eval/CSGVBenchmarkPass.hpp
        include/volcanite/eval/EvaluationLogExport.hpp
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
        include/volcanite/util/args_and_csgv_provider.hpp
        include/volcanite/util/segmentation_volume_synthesis.hpp
)

set(VOLCANITE_SOURCES
        src/volcanite/eval/CSGVBenchmarkPass.cpp
        src/volcanite/eval/EvaluationLogExport.cpp
        src/volcanite/compression/encoder/CSGVBrickEncoder.cpp
        src/volcanite/compression/encoder/CSGVSerialBrickEncoder.cpp
        src/volcanite/compression/encoder/NibbleEncoder.cpp
        src/volcanite/compression/encoder/RangeANSEncoder.cpp
        src/volcanite/compression/encoder/WaveletMatrixEncoder.cpp
        src/volcanite/compression/CompressedSegmentationVolume.cpp
        src/volcanite/compression/CompressedSegmentationVolumeStats.cpp
        src/volcanite/compression/CompressedSegmentationVolumeParallel.cpp
        src/volcanite/compression/CSGVDatabase.cpp
        src/volcanite/compression/pack_nibble.cpp
        src/volcanite/compression/pack_rans.cpp
        src/volcanite/compression/pack_wavelet_matrix.cpp
        src/volcanite/compression/wavelet_tree/prefix_counting.cpp
        src/volcanite/compression/wavelet_tree/HuffmanWaveletMatrix.cpp
        src/volcanite/compression/wavelet_tree/WaveletMatrix.cpp
        src/util/args_and_csgv_provider.cpp
        src/util/segmentation_volume_synthesis.cpp
        src/renderer/CompressedSegmentationVolumeRenderer.cpp
        src/renderer/CompressedSegmentationVolumeBrickViewer.cpp
        src/renderer/PassCompSegVolRender.cpp
)

makeVolcaniteLibrary(libvolcanite ${VOLCANITE_HEADERS} ${VOLCANITE_SOURCES})
# add data/ folder as target_property to install() by each project
set_target_properties(libvolcanite PROPERTIES INTERFACE_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)

makeVolcaniteExecutable(volcanite)
installVolcaniteExecutable(volcanite)

# development executables:
makeVolcaniteExecutable(csgv_benchmark)
makeVolcaniteExecutable(csgv_brick_viewer)

