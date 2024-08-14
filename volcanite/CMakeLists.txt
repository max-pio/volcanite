cmake_minimum_required(VERSION 3.16)
project(volcanite VERSION 0.2.0 DESCRIPTION "Volcanite Renderers" LANGUAGES CXX)

# set all sources and headers
set(VOLCANITE_HEADERS
        include/volcanite/CSGVPathUtils.hpp
        include/volcanite/VolcaniteArgs.hpp
        include/volcanite/StratifiedPixelSequence.hpp
        include/volcanite/compression/VolumeCompressionBase.hpp
        data/shader/cpp_glsl_include/csgv_constants.h
        include/volcanite/compression/CompressedSegmentationVolume.hpp
        include/volcanite/compression/CompSegVolHandler.hpp
        include/volcanite/compression/CSGVChunkMerger.hpp
        include/volcanite/compression/CSGVDatabase.hpp
        include/volcanite/compression/pack_nibble.hpp
        include/volcanite/compression/pack_rans.hpp
        include/volcanite/compression/pack_wavelet_matrix.hpp
        include/volcanite/compression/wavelet_tree/bit_reversal_permutation.hpp
        include/volcanite/compression/wavelet_tree/BitVector.hpp
        include/volcanite/compression/wavelet_tree/WaveletMatrix.hpp
        include/volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp
        include/volcanite/renderer/CompressedSegmentationVolumeBrickViewer.hpp
        include/volcanite/renderer/PassCompSegVolRender.hpp
        )

set(VOLCANITE_SOURCES
        src/volcanite/compression/CompressedSegmentationVolume.cpp
        src/volcanite/compression/CompressedSegmentationVolumeStats.cpp
        src/volcanite/compression/CompressedSegmentationVolumeParallel.cpp
        src/volcanite/compression/pack_nibble.cpp
        src/volcanite/compression/pack_rans.cpp
        src/renderer/CompressedSegmentationVolumeRenderer.cpp
        src/renderer/CompressedSegmentationVolumeBrickViewer.cpp
        src/renderer/PassCompSegVolRender.cpp
        )

makeExecutable(bitvector_test ${VOLCANITE_HEADERS} ${VOLCANITE_SOURCES})
installExecutable(bitvector_test)

makeExecutable(csgv_brick_viewer ${VOLCANITE_HEADERS} ${VOLCANITE_SOURCES})
installExecutable(csgv_brick_viewer)

makeExecutable(volcanite ${VOLCANITE_HEADERS} ${VOLCANITE_SOURCES})
installExecutable(volcanite)
