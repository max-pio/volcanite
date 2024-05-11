cmake_minimum_required(VERSION 3.16)
project(volcanite VERSION 0.2.0 DESCRIPTION "Volcanite Renderers" LANGUAGES CXX)

# set all sources and headers
set(VOLCANITE_HEADERS
        include/volcanite/VolcaniteArgs.hpp
        include/volcanite/StratifiedPixelSequence.hpp
        include/volcanite/compression/bitpack.hpp
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
        include/volcanite/renderer/HelloVolumeRenderer.hpp
        )

set(VOLCANITE_SOURCES
        src/volcanite/compression/bitpack.cpp
        src/volcanite/compression/CompressedSegmentationVolume.cpp
        src/volcanite/compression/CompressedSegmentationVolumeStats.cpp
        src/volcanite/compression/rans.cpp
        src/PassCompression.cpp
        src/renderer/CompressedSegmentationVolumeRenderer.cpp
        src/renderer/CompressedSegmentationVolumeBrickViewer.cpp
        src/renderer/PassCompSegVolRender.cpp
        src/renderer/HelloVolumeRenderer.cpp
        )

# ToDo: remove: some data sets are hardcoded into the renderer with a path relative to VOLCANITE_DEFAULT_DATA_PATH
set(VOLCANITE_DEFAULT_DATA_PATH CACHE PATH "Volcanite path prefix for hardcoded data set locations")

function(makeVolcaniteExecutable name)
    makeExecutable(${name} ${VOLCANITE_HEADERS} ${VOLCANITE_SOURCES})

    target_link_libraries(${name} PRIVATE libryg-rans tclap::tclap portable_file_dialogs)
    target_compile_definitions(${name} PRIVATE "-DVOLCANITE_DEFAULT_DATA_PATH=\"${VOLCANITE_DEFAULT_DATA_PATH}\"")
    target_compile_definitions(${name} PRIVATE "-DVOLCANITE_VERSION=\"${CMAKE_PROJECT_VERSION}\"")
    target_include_directories(${name} PRIVATE data/shader/cpp_glsl_include)

    installExecutable(${name})
endfunction()

makeVolcaniteExecutable(compression)
makeVolcaniteExecutable(compression_synth)
makeVolcaniteExecutable(csgv_brick_viewer)
makeVolcaniteExecutable(volcanite)
# ToDO: Move the hello_volume_renderer to the examples. And include actual volume rendering in the shader.
makeVolcaniteExecutable(hello_volume_renderer)
