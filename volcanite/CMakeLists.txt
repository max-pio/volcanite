cmake_minimum_required(VERSION 3.16)
project(volcanite VERSION 0.2.0 DESCRIPTION "Volcanite Renderers" LANGUAGES CXX)

# set all sources and headers
set(VOLCANITE_HEADERS
        include/volcanite/CSGVPathUtils.hpp
        include/volcanite/VolcaniteArgs.hpp
        include/volcanite/StratifiedPixelSequence.hpp
        include/volcanite/compression/bitpack.hpp
        include/volcanite/compression/BitVector.hpp
        include/volcanite/compression/VolumeCompressionBase.hpp
        data/shader/cpp_glsl_include/csgv_constants.h
        include/volcanite/compression/CompressedSegmentationVolume.hpp
        include/volcanite/compression/CompSegVolHandler.hpp
        include/volcanite/compression/CSGVChunkMerger.hpp
        include/volcanite/compression/CSGVDatabase.hpp
        include/volcanite/compression/rans.hpp
        include/volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp
        include/volcanite/renderer/CompressedSegmentationVolumeBrickViewer.hpp
        include/volcanite/renderer/PassCompSegVolRender.hpp
        )

set(VOLCANITE_SOURCES
        src/volcanite/compression/bitpack.cpp
        src/volcanite/compression/CompressedSegmentationVolume.cpp
        src/volcanite/compression/CompressedSegmentationVolumeStats.cpp
        src/volcanite/compression/CompressedSegmentationVolumeParallel.cpp
        src/volcanite/compression/rans.cpp
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
