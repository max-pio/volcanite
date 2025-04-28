#  Copyright (C) 2024, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
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


cmake_minimum_required(VERSION 3.16.0)
project(libvvv VERSION 0.4.0 DESCRIPTION "Vulkan Volume Visualization library" LANGUAGES CXX)

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

        include/vvv/vk/debug_marker.hpp
        include/vvv/vk/destroy.hpp
        include/vvv/vk/format_utils.hpp
        include/vvv/vk/memory.hpp
        include/vvv/vk/queue.hpp
        include/vvv/vk/swapchain.hpp
        include/vvv/vk/version.hpp

        include/vvv/util/csv_utils.hpp
        include/vvv/util/Logger.hpp
        include/vvv/util/detect_debugger.hpp
        include/vvv/util/hash_memory.hpp
        include/vvv/util/random_gpu.hpp
        include/vvv/util/space_filling_curves.hpp
        include/vvv/util/util.hpp
        include/vvv/util/volume_data_types.hpp
        include/vvv/util/vertex_primitives.hpp
        include/vvv/util/Paths.hpp
        include/vvv/util/color_space_converter.hpp

        ../../extern/SPIRV-Reflect/spirv_reflect.h
        ../../extern/SPIRV-Reflect/include/spirv/unified1/spirv.h

        ../../extern/stb/stb_image.hpp
        ../../extern/stb/stb_image_write.hpp
)

if (HEADLESS)
    list(APPEND VVV_HEADERS include/vvv/headless_entrypoint.hpp)
endif ()

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

        src/reflection/GraphicsPipelineReflection.cpp
        src/reflection/UniformReflection.cpp
        src/reflection/TextureReflection.cpp

        src/volren/tf/VectorTransferFunction.cpp
        src/volren/Volume.cpp

        src/vk/debug_marker.cpp
        src/vk/format_utils.cpp
        src/vk/memory.cpp
        src/vk/queue.cpp

        src/util/csv_utils.cpp
        src/util/Logger.cpp
        src/util/detect_debugger.cpp
        src/util/random_gpu.cpp
        src/util/util.cpp
        src/util/Paths.cpp

        src/reader/hdf5.cpp
        src/reader/vti.cpp
        src/reader/tiff_ome.cpp
        src/reader/nrrd.cpp
        src/reader/volcanite_raw.cpp

        ../../extern/SPIRV-Reflect/spirv_reflect.cpp

        ../../extern/stb/stb_image.cpp
)

if (HEADLESS)
    list(APPEND VVV_SOURCES src/headless_entrypoint.cpp)
endif ()

# Target and Packages --------------------------------------------------------------------------------------------------

# create library target
add_library(libvvv STATIC ${VVV_HEADERS} ${VVV_SOURCES})
# add an alias so that library can be used inside the build tree, e.g. when testing
add_library(LibVVV::libvvv ALIAS libvvv)

# optional packages
find_package(OpenMP)

# setting glslangValidator via the Vulkan package requires CMAKE 3.21
# TODO: REMOVE set(SHADER_COMPILER_EXECUTABLE ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE})
set(SHADER_COMPILER_EXECUTABLE ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE})
string(TIMESTAMP BUILD_TIME_ISO8601 "%Y-%m-%dT%H:%M:%SZ" UTC)
set(VVV_BUILD_TIME_ISO8601 ${BUILD_TIME_ISO8601})
configure_file(include/vvv/config.hpp.in include/vvv/config.hpp @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

target_compile_definitions(libvvv PRIVATE -DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_compile_definitions(libvvv PRIVATE -DVULKAN_HPP_STORAGE_SHARED_EXPORT=1)
target_compile_definitions(libvvv PRIVATE -DVULKAN_HPP_STORAGE_SHARED=1)

# add GLM definitions for ImGuizmo
target_compile_definitions(libvvv PUBLIC -DGLM_ENABLE_EXPERIMENTAL -DGLM_FORCE_DEPTH_ZERO_TO_ONE -DLIB_VVV_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\")

# link required libraries
target_link_libraries(libvvv PUBLIC Vulkan::Vulkan glm::glm shaderc fmt::fmt)

# link optional libraries
if (OpenMP_CXX_FOUND)
    target_link_libraries(libvvv PUBLIC OpenMP::OpenMP_CXX)
endif ()
if (HDF5_FOUND)
    target_link_libraries(libvvv PRIVATE HighFive)
    target_compile_definitions(libvvv PRIVATE -DLIB_HIGHFIVE)
endif ()
# vtk linking and module initialization
if (VTK_FOUND)
    target_link_libraries(libvvv PRIVATE ${VTK_LIBRARIES})
    vtk_module_autoinit(
            TARGETS libvvv
            MODULES ${VTK_LIBRARIES}
    )
    target_compile_definitions(libvvv PRIVATE -DLIB_VTK)
endif ()
if (TIFF_FOUND)
    target_link_libraries(libvvv PRIVATE TIFF::TIFF)
    target_compile_definitions(libvvv PRIVATE -DLIB_TIFF)
endif ()
if (PUGIXML_FOUND)
    target_link_libraries(libvvv PRIVATE pugixml::pugixml)
    target_compile_definitions(libvvv PRIVATE -DLIB_PUGIXML)
endif ()

# use minimal public include interface
target_include_directories(libvvv
        PUBLIC
        # include paths depend on installed or build library
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../extern>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# add data/ folder as target_property to install() by each project
set_target_properties(libvvv PROPERTIES INTERFACE_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
