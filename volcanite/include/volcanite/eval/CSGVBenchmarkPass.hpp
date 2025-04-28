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

#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <utility>

#include "vvv/core/Renderer.hpp"
#include "vvv/core/Shader.hpp"
#include "vvv/passes/PassCompute.hpp"
#include "vvv/reflection/UniformReflection.hpp"

#include "volcanite/compression/CompressedSegmentationVolume.hpp"

using namespace vvv;

namespace volcanite {

class CSGVBenchmarkPass : public PassCompute {

  public:
    CSGVBenchmarkPass(CompressedSegmentationVolume *csgv, GpuContextPtr ctx,
                      uint32_t cache_size_MB = 1024, bool palette_cache = false,
                      bool decode_from_shared_memory = false, const std::string &label = "CSGVBenchmark")
        : WithGpuContext(ctx),
          WithMultiBuffering(NoMultiBuffering),
          PassCompute(ctx, label, NoMultiBuffering, ctx->getQueueFamilyIndices().compute.value()), m_csgv(csgv),
          m_decode_from_shared_memory(decode_from_shared_memory), m_cache_bytes(cache_size_MB * 1024ull * 1024ull),
          m_use_palette_cache(palette_cache), m_shader_defines(csgv->getGLSLDefines()) {

        // obtain shader compilation and execution parameters
        m_shader_defines.emplace_back("SUBGROUP_SIZE=" + std::to_string(
                                                             getCtx()->getPhysicalDeviceSubgroupProperties().subgroupSize));
        m_shader_defines.emplace_back("CACHE_MODE=" + std::to_string(CACHE_BRICKS));
        if (m_use_palette_cache)
            m_shader_defines.emplace_back("PALETTE_CACHE");
        if (m_decode_from_shared_memory)
            m_shader_defines.emplace_back("DECODE_FROM_SHARED_MEMORY");

        // check how many bits are required to store cache indices
        if (m_use_palette_cache) {
            // must be (max_palette_count + 1), need an additional magic number (= 0) for not yet written output voxels
            m_cache_palette_idx_bits = static_cast<uint32_t>(glm::ceil(
                glm::log2(static_cast<double>(m_csgv->getMaxBrickPaletteCount()) + 1.0)));
            m_cache_indices_per_uint = 32u / m_cache_palette_idx_bits;
            m_cache_uints_per_brick = m_csgv->getBrickSize() * m_csgv->getBrickSize() * m_csgv->getBrickSize();
            m_cache_uints_per_brick = (m_cache_uints_per_brick + m_cache_indices_per_uint - 1u) / m_cache_indices_per_uint;
            m_cache_base_element_uints = (8u + m_cache_indices_per_uint - 1u) /
                                         m_cache_indices_per_uint; // = ceil(8 / m_palette_indices_per_uint)
        } else {
            // without paletting, the cache stores explicit 32 bit labels = one label per uint
            m_cache_palette_idx_bits = 32u;
            m_cache_indices_per_uint = 1u;
            m_cache_uints_per_brick = m_csgv->getBrickSize() * m_csgv->getBrickSize() * m_csgv->getBrickSize();
            m_cache_base_element_uints = 8u;
        }

        // compute how many bricks fit into the cache at once
        const uint32_t brick_idx_count = csgv->getBrickIndexCount();
        const size_t required_cache_bytes = static_cast<size_t>(m_cache_uints_per_brick) * brick_idx_count * sizeof(uint32_t);
        if (required_cache_bytes > m_cache_bytes) {
            m_bricks_per_execution = m_cache_bytes / sizeof(uint32_t) / m_cache_uints_per_brick;
            m_cache_bytes = m_bricks_per_execution * sizeof(uint32_t) * m_cache_uints_per_brick;
        } else {
            m_bricks_per_execution = brick_idx_count;
            m_cache_bytes = required_cache_bytes;
        }
        m_execution_iterations = (brick_idx_count + m_bricks_per_execution - 1u) / m_bricks_per_execution;

        if (m_csgv->isUsingRandomAccess()) {
            const uint32_t subgroup_size = getCtx()->getPhysicalDeviceSubgroupProperties().subgroupSize;
            m_decompression_workgroup_size = vk::Extent3D{m_bricks_per_execution * subgroup_size, 1u, 1u};
        } else {
            m_decompression_workgroup_size = vk::Extent3D{m_bricks_per_execution, 1u, 1u};
        }

        // allocate all shader and command buffer resources
        allocateResources();

        // create and bind buffers
        initDataSetGPUBuffers();

        // initialize timing queries
        m_time_stamps = std::vector<uint64_t>(2 * m_execution_iterations, 0);
        auto device_limits = getCtx()->getPhysicalDevice().getProperties().limits;
        m_timestamp_period = device_limits.timestampPeriod;
        if (m_timestamp_period == 0.f)
            throw std::runtime_error("The selected device does not support timestamp queries.");
        if (!device_limits.timestampComputeAndGraphics)
            throw std::runtime_error("The queue might not support time stamps.");
        vk::QueryPoolCreateInfo query_pool_info{};
        query_pool_info.queryType = vk::QueryType::eTimestamp;
        query_pool_info.queryCount = static_cast<uint32_t>(m_time_stamps.size());
        auto query_pool_res = getCtx()->getDevice().createQueryPool(query_pool_info);
        if (query_pool_res)
            m_query_pool_timestamps = query_pool_res;
    }

    void initDataSetGPUBuffers();
    void freeResources() override;

    AwaitableHandle execute(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr) override;

    /// Returns the execution time for decompressing the whole volume in milliseconds.
    /// Returns -1 if the result could not be queried.
    /// Returns 0 if the result is not yet available.
    /// @returns the GPU decompression time in milliseconds
    [[nodiscard]] double getExecutionTimeMS() {
        std::vector<uint64_t> time_stamp_avail(m_time_stamps.size() * 2);
        auto query_res = device().getQueryPoolResults(m_query_pool_timestamps, 0, m_time_stamps.size(),
                                                      time_stamp_avail.size() * sizeof(uint64_t), time_stamp_avail.data(),
                                                      2 * sizeof(uint64_t), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);
        if (query_res != vk::Result::eSuccess) {
            Logger(Warn) << "Could not query time stamp.";
            return -1.f;
        }

        for (int i = 0; i < m_time_stamps.size(); i++) {
            // return 0 if one of the timestamps is not available
            if (time_stamp_avail[i * 2 + 1] == 0u)
                return 0u;
            m_time_stamps[i] = time_stamp_avail[i * 2];
        }

        // convert execution times to milliseconds
        double total_time_ms = 0.;
        for (int i = 0; i < m_execution_iterations; i++) {
            total_time_ms += static_cast<double>(m_time_stamps[2 * i + 1] - m_time_stamps[2 * i]) * m_timestamp_period / 1000000.;
        }
        return total_time_ms;
    }

    static void configureExtensionsAndLayersAndFeatures(GpuContextRwPtr ctx) {
        ctx->enableDeviceExtension("VK_EXT_memory_budget");
        ctx->physicalDeviceFeaturesV12().setBufferDeviceAddress(true);
        ctx->physicalDeviceFeaturesV12().setHostQueryReset(true);
        ctx->physicalDeviceFeatures().setShaderInt64(true);
    }

  protected:
    struct PushConstants {
        uint32_t brick_idx_offset; ///< the workgroup starts decompression at this 1D index during execution
        uint32_t target_inv_lod;   ///< the inv. LOD (0 is coarsest at 1³ voxels) into which to decode bricks
    };

    std::vector<std::shared_ptr<Shader>> createShaders() override;
    std::vector<vk::PushConstantRange> definePushConstantRanges() override;

    CompressedSegmentationVolume *m_csgv;      ///< the compressed segmentation volume to benchmark
    std::vector<std::string> m_shader_defines; ///< defines that are passed on to shader compilation
    uint32_t m_bricks_per_execution;           ///< how many bricks can be decompressed in one execution
    uint32_t m_execution_iterations;           ///< how many executions are requried to decode all bricks
    vk::Extent3D m_decompression_workgroup_size = {0u, 0u, 0u};
    size_t m_cache_bytes = 1024ull * 1024 * 1024; ///< cache size in bytes
    bool m_decode_from_shared_memory = false;     ///< if true, the encoding is copied to shared memory before decoding. Requires random access encoding.

    // GPU resources and buffers
    std::shared_ptr<UniformReflected> m_usegmented_volume_info = nullptr;
    // cache to store decompressed bricks
    std::shared_ptr<Buffer> m_cache_buffer = nullptr;                   ///< cache for decoding bricks
    bool m_use_palette_cache = false;                                   ///< if the cache stores palette indices
    uint32_t m_cache_palette_idx_bits = 32u;                            ///< the GPU cache can store palette indices with fewer than 32 bits per entry
    uint32_t m_cache_indices_per_uint = 1u;                             ///< is floor(32/bits_per_palette_index), indices do not cross multiple words
    uint32_t m_cache_uints_per_brick = 0;                               ///< number of uints needed to store all voxels of a full brick
    uint32_t m_cache_base_element_uints = 8;                            ///< number of uints needed to store 2x2x2 output voxels
    std::vector<std::shared_ptr<Buffer>> m_split_encoding_buffers = {}; // base level split encoding buffers
    std::vector<glm::uvec2> m_split_encoding_buffer_addresses = {};
    std::shared_ptr<Buffer> m_split_encoding_buffer_addresses_buffer = nullptr;
    std::shared_ptr<Buffer> m_brick_starts_buffer = nullptr;
    // detail level split encoding buffers
    std::vector<uint32_t> m_constructed_detail_starts = {};
    std::shared_ptr<Buffer> m_detail_starts_buffer = nullptr;
    std::shared_ptr<Buffer> m_detail_buffer = nullptr;
    glm::uvec2 m_detail_buffer_address = {};

    // timing
    float m_timestamp_period;
    std::vector<uint64_t> m_time_stamps;
    vk::QueryPool m_query_pool_timestamps;
    int m_cache_heat_up_iterations = 0;
};

} // namespace volcanite
