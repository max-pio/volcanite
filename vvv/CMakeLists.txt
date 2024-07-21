cmake_minimum_required(VERSION 3.16.0)
project(libvvv VERSION 0.1 DESCRIPTION "Vulkan Volume Visualization library" LANGUAGES CXX)

# Project Files --------------------------------------------------------------------------------------------------------
set(VVV_HEADERS
        include/vvv/core/preamble.hpp
        include/vvv/core/preamble_forward_decls.hpp
        include/vvv/vvv.hpp

        include/vvv/core/Buffer.hpp
        include/vvv/core/Camera.hpp
        include/vvv/core/DefaultGpuContext.hpp
        include/vvv/core/GpuContext.hpp
        include/vvv/core/GuiInterface.hpp
        include/vvv/core/HeadlessRendering.hpp
        include/vvv/core/MultiBuffering.hpp
        include/vvv/core/Renderer.hpp
        include/vvv/core/Shader.hpp
        include/vvv/core/Synchronization.hpp
        include/vvv/core/Texture.hpp
        include/vvv/core/TimelineSemaphore.hpp
        include/vvv/core/WindowingSystemIntegration.hpp
        include/vvv/core/WithGpuContext.hpp

        include/vvv/passes/PassBase.hpp
        include/vvv/passes/PassCompute.hpp
        include/vvv/passes/SinglePassGraphics.hpp
        include/vvv/passes/PassSsao.hpp
        include/vvv/passes/PassBlur.hpp
        include/vvv/passes/PassSimpleSsao.hpp

        include/vvv/pointren/PointCloud.hpp

        include/vvv/reflection/GraphicsPipelineReflection.hpp
        include/vvv/reflection/TextureReflection.hpp
        include/vvv/reflection/UniformReflection.hpp

        include/vvv/volren/tf/TransferFunction.hpp
        include/vvv/volren/tf/VectorTransferFunction.hpp
        include/vvv/volren/tf/TransferFunction1D.hpp
        include/vvv/volren/tf/SegmentedVolumeMaterial.hpp
        include/vvv/volren/tf/builtin.hpp
        include/vvv/volren/tf/color.hpp
        include/vvv/volren/Volume.hpp
        include/vvv/volren/VolumeHistogram.hpp

        include/vvv/vk/debug_marker.hpp
        include/vvv/vk/destroy.hpp
        include/vvv/vk/format_utils.hpp
        include/vvv/vk/memory.hpp
        include/vvv/vk/queue.hpp
        include/vvv/vk/swapchain.hpp
        include/vvv/vk/version.hpp

        include/vvv/util/csv_utils.hpp
        include/vvv/util/Logger.hpp
        include/vvv/util/PointUtils.hpp
        include/vvv/util/detect_debugger.hpp
        include/vvv/util/managed_buffer.hpp
        include/vvv/util/random_gpu.hpp
        include/vvv/util/space_filling_curves.hpp
        include/vvv/util/util.hpp
        include/vvv/util/vertex_primitives.hpp
        include/vvv/util/Paths.hpp

        ../extern/SPIRV-Reflect/spirv_reflect.h
        ../extern/SPIRV-Reflect/include/spirv/unified1/spirv.h

        ../extern/stb/stb_image.hpp
        ../extern/stb/stb_image_write.hpp

# ToDo: why are builds breaking on some machines when including tinyexr?
#        ../extern/tinyexr/tinyexr.hpp
        )

if(HEADLESS)
    list(APPEND VVV_HEADERS include/vvv/headless_entrypoint.hpp)
endif()

set(VVV_SOURCES
        src/vvv.cpp
        src/core/config.cpp

        src/core/Buffer.cpp
        src/core/Camera.cpp
        src/core/DefaultGpuContext.cpp
        src/core/GuiInterface.cpp
        src/core/GuiElementList.cpp
        src/core/GpuContext.cpp
        src/core/HeadlessRendering.cpp
        src/core/MultiBuffering.cpp
        src/core/Shader.cpp
        src/core/Synchronization.cpp
        src/core/Texture.cpp
        src/core/WithGpuContext.cpp

        src/passes/PassBase.cpp
        src/passes/PassCompute.cpp
        src/passes/SinglePassGraphics.cpp
        src/passes/PassSsao.cpp
        src/passes/PassBlur.cpp
        src/passes/PassSimpleSsao.cpp

        src/pointren/PointCloud.cpp

        src/reflection/GraphicsPipelineReflection.cpp
        src/reflection/UniformReflection.cpp
        src/reflection/TextureReflection.cpp

        src/volren/tf/VectorTransferFunction.cpp
        src/volren/tf/TransferFunction2D.cpp
        src/volren/Volume.cpp
        src/volren/VolumeHistogram.cpp

        src/vk/debug_marker.cpp
        src/vk/format_utils.cpp
        src/vk/memory.cpp
        src/vk/queue.cpp

        src/util/csv_utils.cpp
        src/util/Logger.cpp
        src/util/PointUtils.cpp
        src/util/detect_debugger.cpp
        src/util/random_gpu.cpp
        src/util/util.cpp
        src/util/Paths.cpp

        src/reader/hdf5.cpp
        src/reader/vti.cpp
        src/reader/tiff_ome.cpp
        src/reader/nrrd.cpp
        src/reader/volcanite_raw.cpp

        ../extern/SPIRV-Reflect/spirv_reflect.cpp

        ../extern/stb/stb_image.cpp
# ToDo: why are builds breaking on some machines when including tinyexr?
#        ../extern/tinyexr/tinyexr.cpp
        )

if(HEADLESS)
    list(APPEND VVV_SOURCES src/headless_entrypoint.cpp)
endif()

# Target and Packages --------------------------------------------------------------------------------------------------

# create library target
add_library(libvvv STATIC ${VVV_HEADERS} ${VVV_SOURCES})
# add an alias so that library can be used inside the build tree, e.g. when testing
add_library(LibVVV::libvvv ALIAS libvvv)

# required packages
find_package(Vulkan REQUIRED)

# optional packages
find_package(OpenMP)

option(USE_TIFF_SUPPORT  "Includes the TIFF library for importing and exporting tiff files" OFF)
if(USE_TIFF_SUPPORT)
    find_package(TIFF)
    if(NOT TIFF_FOUND)
        message(WARNING "USE_TIFF_SUPPORT was set but TIFF library could not be found!")
    endif ()
endif()

option(USE_XML_SUPPORT  "Includes the PUGIXML library for processing XML data" ON)
if(USE_XML_SUPPORT)
    find_package(PUGIXML)
    if(NOT PUGIXML_FOUND)
        message(WARNING "USE_XML_SUPPORT was set but PUGIXML library could not be found!")
    endif ()
endif()

# setting glslangValidator via the Vulkan package requires CMAKE 3.21
set(SHADER_COMPILER_EXECUTABLE ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE})
string(TIMESTAMP BUILD_TIME_ISO8601 "%Y-%m-%dT%H:%M:%SZ" UTC)
set(VVV_BUILD_TIME_ISO8601 ${BUILD_TIME_ISO8601})
configure_file(include/vvv/config.hpp.in include/vvv/config.hpp @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

target_compile_definitions(libvvv PRIVATE -DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_compile_definitions(libvvv PRIVATE -DVULKAN_HPP_STORAGE_SHARED_EXPORT=1)
target_compile_definitions(libvvv PRIVATE -DVULKAN_HPP_STORAGE_SHARED=1)

target_compile_definitions(libvvv PUBLIC -DGLM_ENABLE_EXPERIMENTAL -DGLM_FORCE_DEPTH_ZERO_TO_ONE -DLIB_VVV_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\")

# link required libraries
# target_link_libraries(libvvv PUBLIC Vulkan::Vulkan PRIVATE TIFF::TIFF ZLIB::ZLIB PRIVATE pugixml::pugixml PUBLIC glm::glm)
target_link_libraries(libvvv PUBLIC Vulkan::Vulkan glm::glm)

# link optional libraries
if (OpenMP_CXX_FOUND)
    target_link_libraries(libvvv PUBLIC OpenMP::OpenMP_CXX)
endif ()
if (HDF5_FOUND)
    target_link_libraries(libvvv PRIVATE HighFive)
    target_compile_definitions(libvvv PUBLIC -DLIB_HIGHFIVE)
endif ()
# vtk linking and module initialization
if (VTK_FOUND)
    target_link_libraries(libvvv PRIVATE ${VTK_LIBRARIES})
    vtk_module_autoinit(
            TARGETS libvvv
            MODULES ${VTK_LIBRARIES}
    )
    target_compile_definitions(libvvv PUBLIC -DLIB_VTK)
endif ()
if (SQLite3_FOUND)
    target_link_libraries(libvvv PUBLIC SQLiteCpp SQLite::SQLite3) # pthread dl
    target_compile_definitions(libvvv PUBLIC -DLIB_SQLITE3)
endif ()
if (TIFF_FOUND)
    target_link_libraries(libvvv PRIVATE TIFF::TIFF)
    target_compile_definitions(libvvv PUBLIC -DLIB_TIFF)
endif ()
if (PUGIXML_FOUND)
    target_link_libraries(libvvv PRIVATE pugixml::pugixml)
    target_compile_definitions(libvvv PUBLIC -DLIB_PUGIXML)
endif ()

# remove comment for automatic /data copy:
#addCopyDataDependency(libvvv ${CMAKE_CURRENT_SOURCE_DIR}/data)

# use minimal public include interface
target_include_directories(libvvv
        PUBLIC
        # include paths depend on installed or build library
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../extern>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        )

# add data/ folder as target_property to install() by each project
set_target_properties(libvvv PROPERTIES INTERFACE_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
