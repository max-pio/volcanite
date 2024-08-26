//  Copyright (C) 2024, Max Piochowiak, Karlsruhe Institute of Technology
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#pragma once

#include <memory>
#include <optional>
#include <glm/glm.hpp>
#include <utility>

#include "vvv/core/Renderer.hpp"
#include "vvv/core/Shader.hpp"
#include "vvv/util/hash_memory.hpp"
#include "vvv/reflection/UniformReflection.hpp"
#include "vvv/passes/PassCompute.hpp"

#include "volcanite/compression/CompressedSegmentationVolume.hpp"

using namespace vvv;

namespace volcanite {

class CSGVBenchmarkPass : public PassCompute {

    public:
        enum CSGVBenchmarkStage {
            DECOMPRESSION = 0,
        };

        CSGVBenchmarkPass(CompressedSegmentationVolume* csgv, GpuContextPtr ctx,
                             bool parallel_decode = false,
                             uint32_t cache_size_MB = 1024, bool palette_cache = false,
                             const std::string& label = "CSGVBenchmark")
                : WithGpuContext(ctx),
                  WithMultiBuffering(NoMultiBuffering),
                  PassCompute(ctx, label, NoMultiBuffering, ctx->getQueueFamilyIndices().compute.value()),
                  m_csgv(csgv), m_parallel_decode(parallel_decode), m_cache_bytes(cache_size_MB * 1024 * 1024),
                  m_use_palette_cache(palette_cache), m_shader_defines(csgv->getGLSLDefines()) {

            // obtain shader compilation and execution parameters
            m_shader_defines.emplace_back("SUBGROUP_SIZE=" + std::to_string(
                                                getCtx()->getPhysicalDeviceSubgroupProperties().subgroupSize));
            if(m_use_palette_cache)
                m_shader_defines.emplace_back("PALETTE_CACHE");

            // check how many bits are required to store cache indices
            if(m_use_palette_cache) {
                // must be (max_palette_count + 1), need an additional magic number (= 0) for not yet written output voxels
                m_cache_palette_idx_bits = static_cast<uint32_t>(glm::ceil(
                        glm::log2(static_cast<double>(m_csgv->getMaxBrickPaletteCount()) + 1.0)));
                m_cache_indices_per_uint = 32u / m_cache_palette_idx_bits;
                m_cache_uints_per_brick = m_csgv->getBrickSize() * m_csgv->getBrickSize() * m_csgv->getBrickSize();
                m_cache_uints_per_brick = (m_cache_uints_per_brick + m_cache_indices_per_uint - 1u)
                                            / m_cache_indices_per_uint;
            } else {
                // without paletting, the cache stores explicit 32 bit labels = one label per uint
                m_cache_palette_idx_bits = 32u;
                m_cache_indices_per_uint = 1u;
                m_cache_uints_per_brick = m_csgv->getBrickSize() * m_csgv->getBrickSize() * m_csgv->getBrickSize();
            }

            // compute how many bricks fit into the cache at once
            const uint32_t brick_idx_count = csgv->getBrickIndexCount();
            const size_t required_cache_bytes = static_cast<size_t>(m_cache_uints_per_brick) * brick_idx_count * sizeof(uint32_t);
            if (required_cache_bytes > m_cache_bytes) {
                m_bricks_per_execution = m_cache_bytes / sizeof(uint32_t) / m_cache_uints_per_brick;
                m_cache_bytes = m_bricks_per_execution * sizeof(uint32_t) * m_cache_uints_per_brick;
            }
            else {
                m_bricks_per_execution = brick_idx_count;
                m_cache_bytes = required_cache_bytes;
            }

            if (m_parallel_decode) {
                const uint32_t subgroup_size = getCtx()->getPhysicalDeviceSubgroupProperties().subgroupSize;
                m_decompression_workgroup_size = vk::Extent3D{m_bricks_per_execution * subgroup_size, 1u, 1u};
            } else {
                m_decompression_workgroup_size = vk::Extent3D{m_bricks_per_execution, 1u, 1u};
            }

            // allocate all shader and command buffer resources
            allocateResources();

            // create and bind buffers
            initDataSetGPUBuffers();
        }

        void initDataSetGPUBuffers();
        void freeResources() override;


    AwaitableHandle execute(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr) override;

    protected:

        struct PushConstants{
            uint32_t brick_idx_offset;                ///< the workgroup starts decompression at this 1D index during execution
        };

        std::vector<std::shared_ptr<Shader>> createShaders() override;
        std::vector<vk::PushConstantRange> definePushConstantRanges() override;

        CompressedSegmentationVolume* m_csgv;        ///< the compressed segmentation volume to benchmark
        std::vector<std::string> m_shader_defines;   ///< defines that are passed on to shader compilation
        bool m_parallel_decode = false;              ///< if decompression is parallelized within one brick
        uint32_t m_bricks_per_execution;             ///< how many bricks can be decompressed in one execution
        vk::Extent3D m_decompression_workgroup_size = {0u, 0u, 0u};
        size_t m_cache_bytes = 1024 * 1024 * 1024;   ///< cache size in bytes

        // GPU resources and buffers
        std::shared_ptr<UniformReflected> m_usegmented_volume_info = nullptr;
        // cache to store decompressed bricks
        std::shared_ptr<Buffer> m_cache_buffer = nullptr; ///< cache for decoding bricks
        bool m_use_palette_cache = false;                 ///< if the cache stores palette indices
        uint32_t m_cache_palette_idx_bits = 32u;          ///< the GPU cache can store palette indices with fewer than 32 bits per entry
        uint32_t m_cache_indices_per_uint = 1u;           ///< is floor(32/bits_per_palette_index), indices do not cross multiple words
        uint32_t m_cache_uints_per_brick = 0;             ///< number of uints needed to store all voxels of a full brick
        std::vector<std::shared_ptr<Buffer>> m_split_encoding_buffers = {};        // base level split encoding buffers
        std::vector<glm::uvec2> m_split_encoding_buffer_addresses = {};
        std::shared_ptr<Buffer> m_split_encoding_buffer_addresses_buffer = nullptr;
        std::shared_ptr<Buffer> m_brick_starts_buffer = nullptr;
        // detail level split encoding buffers
        std::vector<uint32_t> m_constructed_detail_starts = {};
        std::shared_ptr<Buffer> m_detail_starts_buffer = nullptr;
        std::shared_ptr<Buffer> m_detail_buffer = nullptr;
        glm::uvec2 m_detail_buffer_address = {};
};

} // namespace volcanite