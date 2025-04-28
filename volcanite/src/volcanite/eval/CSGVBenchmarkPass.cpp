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

#include "volcanite/eval/CSGVBenchmarkPass.hpp"

using namespace vvv;

namespace volcanite {

AwaitableHandle CSGVBenchmarkPass::execute(AwaitableList awaitBeforeExecution, BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) {

    Logger(Info) << "GPU decompression with a cache size of " << m_cache_bytes / 1000 / 1000 << "MB in "
                 << m_execution_iterations << " iterations (with " << m_cache_heat_up_iterations
                 << " cache heat up iterations each)";

    // fill command buffer
    auto &commandBuffer = m_commandBuffer->getActive();
    getCtx()->debugMarker->setName(commandBuffer, "CSGVBenchmarkPass.commandBuffer");
    commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    commandBuffer.resetQueryPool(m_query_pool_timestamps, 0, static_cast<uint32_t>(m_time_stamps.size()));

    // all uploads must be finished before the rendering can access the buffers
    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, {},
                                  {vk::MemoryBarrier(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead)},
                                  nullptr, nullptr);

    // Each execution decompresses brick_per_exeuction many bricks into the cache. If the cache is not large enough
    // to fit all bricks at once, multiple executions are required.
    for (int i = 0; i < m_execution_iterations; i++) {
        PushConstants pc{.brick_idx_offset = i * m_bricks_per_execution, .target_inv_lod = (m_csgv->getLodCountPerBrick() - 1u)};
        commandBuffer.pushConstants(m_pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(PushConstants), &pc);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute,
                                   m_pipelines.at(0)); // each compute shader has one pipeline
        if (hasDescriptors()) {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_pipelineLayout, 0,
                                             m_descriptorSets->getActive(), nullptr);
        }

        for (int heatup_i = 0; heatup_i <= m_cache_heat_up_iterations; heatup_i++) {
            getCtx()->debugMarker->beginRegion(commandBuffer, "decompress", glm::vec4(0.f, 1.f, 0.f, 1.f));

            // barrier so that all executions finish writing to the cache
            commandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eAllCommands,
                vk::PipelineStageFlagBits::eComputeShader, {},
                {vk::MemoryBarrier(vk::AccessFlagBits::eMemoryWrite | vk::AccessFlagBits::eTransferWrite,
                                   vk::AccessFlagBits::eMemoryRead)},
                nullptr, nullptr);

            // dispatch decompression of bricks and measure runtime
            if (heatup_i == m_cache_heat_up_iterations)
                commandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe,
                                             m_query_pool_timestamps, 2 * i);
            commandBuffer.dispatch(m_decompression_workgroup_size.width, m_decompression_workgroup_size.height,
                                   m_decompression_workgroup_size.depth);
            if (heatup_i == m_cache_heat_up_iterations)
                commandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe,
                                             m_query_pool_timestamps, 2 * i + 1);

            // barrier so that all executions finish writing to the cache
            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                          vk::PipelineStageFlagBits::eComputeShader, {},
                                          {vk::MemoryBarrier(vk::AccessFlagBits::eMemoryWrite,
                                                             vk::AccessFlagBits::eMemoryRead)},
                                          nullptr, nullptr);
            getCtx()->debugMarker->endRegion(commandBuffer); // decompress
        }
    }

    commandBuffer.end();
    return getCtx()->sync->submit(commandBuffer, m_queueFamilyIndex, awaitBeforeExecution,
                                  vk::PipelineStageFlagBits::eAllCommands,
                                  awaitBinaryAwaitableList, signalBinarySemaphore);
}

void CSGVBenchmarkPass::initDataSetGPUBuffers() {
    if (!m_csgv || m_csgv->getAllEncodings()->empty())
        throw std::runtime_error("CompressedSegmentationVolume not initialized!");
    if (m_csgv->isUsingSeparateDetail() && !m_csgv->isUsingDetailFreq())
        throw std::runtime_error("Renderer only supports detail separation when rANS is in double table mode.");

    const GpuContext *ctx = getCtx();

    // CREATE GPU BUFFERS ---------------------------------

    // create (base) split encoding buffers
    m_brick_starts_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CSGVBenchmarkPass.m_brick_start_buffer", .byteSize = (m_csgv->getBrickIndexCount() + 1u) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    size_t split_encoding_count = m_csgv->getAllEncodings()->size();
    m_split_encoding_buffers.resize(split_encoding_count);
    m_split_encoding_buffer_addresses.resize(split_encoding_count);
    m_split_encoding_buffer_addresses_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CSGVBenchmarkPass.m_split_encoding_buffer_addresses_buffer", .byteSize = split_encoding_count * 2 * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    for (int i = 0; i < split_encoding_count; i++) {
        size_t encoding_byte_size = m_csgv->getAllEncodings()->at(i).size() * sizeof(uint32_t);
        m_split_encoding_buffers[i] = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CSGVBenchmarkPass.m_encoding_buffer_" + std::to_string(i), .byteSize = encoding_byte_size, .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
        Buffer::deviceAddressUvec2(m_split_encoding_buffers[i]->getDeviceAddress(), &m_split_encoding_buffer_addresses[i].x);
    }

    // create detail encoding buffers
    // uint32_t m_detail_capacity = 0u; // measured in number of uints
    if (m_csgv->isUsingSeparateDetail()) {
        throw std::runtime_error("CSGV benchmark does not support detail separation yet. Implement buffer in initDataSetGPUBuffers().");
        //            bool detail_buffer_fits_whole_detail = false;
        //            size_t complete_detail_size = 0;
        //            for(const auto& d : *m_csgv->getAllDetails())
        //                complete_detail_size += d.size();
        //            if(m_max_detail_byte_size / sizeof(uint32_t) < complete_detail_size) {
        //                throw std::runtime_error("CSGVBenchmarkPass only supports detail buffers with full detail upload but"
        //                                         " the detail GPU buffer is too small.");
        //            }
        //            // fit the complete detail buffer onto the GPU
        //            m_detail_capacity = complete_detail_size;
        //
        //            m_detail_requests.resize(m_max_detail_requests_per_frame + 2u);
        //            m_detail_requests_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CSGVBenchmarkPass.m_detail_requests_buffer", .byteSize = (m_max_detail_requests_per_frame + 2u) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc, .memoryUsage = vk::MemoryPropertyFlagBits::eHostVisible});
        //            m_detail_starts_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CSGVBenchmarkPass.m_detail_starts_buffer", .byteSize = (bricks_in_volume + 1u)*sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
        //            m_detail_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CSGVBenchmarkPass.m_detail_buffer", .byteSize = m_detail_capacity * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
        //            Buffer::deviceAddressUvec2(m_detail_buffer->getDeviceAddress(), &m_detail_buffer_address.x);
        //
        //            m_constructed_detail_starts.resize(bricks_in_volume + 1u, 0u);
        //            m_constructed_detail.resize(m_detail_capacity, 0u);
        //
        //            // upload the full detail buffer
        //            size_t offset = 0ul;
        //            size_t brick_idx = 0ul;
        //            for(int i = 0; i < m_csgv->getAllDetails()->size(); i++) {
        //                const std::vector<uint32_t>& detail_encoding = m_csgv->getAllDetails()->at(i);
        //                // upload next single detail encoding buffer into offset memory region to form a back-to-back buffer
        //                m_detail_staging = m_detail_buffer->uploadWithStagingBuffer(detail_encoding.data(),
        //                                                                            detail_encoding.size() * sizeof(uint32_t),
        //                                                                            offset * sizeof(uint32_t));
        //                // construct detail starts into continuous detail encoding array
        //                while(brick_idx < bricks_in_volume && brick_idx / m_csgv->getBrickIdxToEncVectorMapping() == i) {
        //                    m_constructed_detail_starts[brick_idx + 1] = m_constructed_detail_starts[brick_idx] + m_csgv->getBrickDetailEncodingLength(brick_idx);
        //                    brick_idx++;
        //                }
        //                offset += detail_encoding.size();
        //                getCtx()->sync->hostWaitOnDevice({m_detail_staging.first});
        //            }
        //
        //            // upload initial detail starts buffer (all zeros if no detail is uploaded initially)
        //            m_detail_starts_staging = m_detail_starts_buffer->uploadWithStagingBuffer(m_constructed_detail_starts.data(), m_constructed_detail_starts.size() * sizeof(uint32_t), {.queueFamily = m_queueFamilyIndex});
        //            getCtx()->sync->hostWaitOnDevice({m_detail_starts_staging.first});
        //            m_detail_staging = {nullptr, nullptr};
        //            m_detail_starts_staging = {nullptr, nullptr};
    }

    // cache buffer
    if (m_cache_bytes > 4294967295ul) {
        throw std::runtime_error("Cache size is currently limited to 4 GB maximum.");
    }
    m_cache_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CSGVBenchmarkPass.m_cache_buffer", .byteSize = m_cache_bytes, .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});

    // UPLOAD TO GPU BUFFERS ----------------------------------
    AwaitableList awaitBeforeExecution = {};
    std::vector<std::pair<AwaitableHandle, std::shared_ptr<vvv::Buffer>>> _encoding_upload;
    for (int i = 0; i < split_encoding_count; i++) {
        _encoding_upload.emplace_back(
            m_split_encoding_buffers[i]->uploadWithStagingBuffer(m_csgv->getAllEncodings()->at(i),
                                                                 {.queueFamily = m_queueFamilyIndex}));
        awaitBeforeExecution.push_back(_encoding_upload[i].first);
    }
    auto [encoding_addresses_upload_finished, _encoding_addresses_staging_buffer] = m_split_encoding_buffer_addresses_buffer->uploadWithStagingBuffer(m_split_encoding_buffer_addresses, {.queueFamily = m_queueFamilyIndex});
    awaitBeforeExecution.push_back(encoding_addresses_upload_finished);
    auto [brickstarts_upload_finished, _brickstarts_staging_buffer] = m_brick_starts_buffer->uploadWithStagingBuffer(*(m_csgv->getBrickStarts()), {.queueFamily = m_queueFamilyIndex});
    awaitBeforeExecution.push_back(brickstarts_upload_finished);

    // wait until all uploads finished
    getCtx()->sync->hostWaitOnDevice(awaitBeforeExecution);

    // update all bindings
    setStorageBuffer(0, 1, *m_brick_starts_buffer);
    setStorageBuffer(0, 2, *m_split_encoding_buffer_addresses_buffer);
    if (m_csgv->isUsingSeparateDetail()) {
        setStorageBuffer(0, 3, *m_detail_starts_buffer);
        setStorageBuffer(0, 4, *m_detail_buffer);
    }
    setStorageBuffer(0, 5, *m_cache_buffer);

    // upload the segmentation volume uniform
    m_usegmented_volume_info = getUniformSet("segmented_volume_info");
    m_usegmented_volume_info->setUniform<glm::uvec3>("g_vol_dim", m_csgv->getVolumeDim());
    m_usegmented_volume_info->setUniform<glm::uvec3>("g_brick_count", m_csgv->getBrickCount());
    m_usegmented_volume_info->setUniform<uint32_t>("g_brick_idx_count", m_csgv->getBrickIndexCount());
    m_usegmented_volume_info->setUniform<uint32_t>("g_max_inv_lod", m_csgv->getLodCountPerBrick() - 1u); // remove
    m_usegmented_volume_info->setUniform<uint32_t>("g_cache_uints_per_brick", m_cache_uints_per_brick);
    m_usegmented_volume_info->setUniform<uint32_t>("g_cache_indices_per_uint", m_cache_indices_per_uint);
    m_usegmented_volume_info->setUniform<uint32_t>("g_cache_palette_idx_bits", m_cache_palette_idx_bits);
    m_usegmented_volume_info->setUniform<uint32_t>("g_cache_base_element_uints", m_cache_base_element_uints);
    m_usegmented_volume_info->setUniform<uint32_t>("g_brick_idx_to_enc_vector", m_csgv->getBrickIdxToEncVectorMapping());
    m_usegmented_volume_info->setUniform<glm::uvec2>("g_detail_buffer_address", m_detail_buffer_address);
    m_usegmented_volume_info->setUniform<uint32_t>("g_detail_buffer_dirty", 0u);
    m_usegmented_volume_info->upload(getActiveIndex());
}

std::vector<std::shared_ptr<Shader>> CSGVBenchmarkPass::createShaders() {
    {
        std::stringstream ss;
        ss << "Shader Definitions: ";
        for (const auto &s : m_shader_defines)
            ss << s << " ";
        Logger(Debug) << ss.str();
    }
    ShaderCompileErrorCallback compileErrorCallback = [](const ShaderCompileError &err) {
        Logger(Error) << err.errorText;
        return ShaderCompileErrorCallbackAction::THROW;
    };
    if (m_csgv->isUsingRandomAccess()) {
        return {std::make_shared<Shader>(
            SimpleGlslShaderRequest{.filename = "volcanite/benchmark/bench_decompress_subgroup_parallel.comp",
                                    .defines = m_shader_defines,
                                    .label = "bench_decompress_subgroup_parallel.comp"},
            compileErrorCallback)};
    } else {
        return {std::make_shared<Shader>(
            SimpleGlslShaderRequest{.filename = "volcanite/benchmark/bench_decompress.comp",
                                    .defines = m_shader_defines,
                                    .label = "bench_decompress.comp"},
            compileErrorCallback)};
    }
}

std::vector<vk::PushConstantRange> CSGVBenchmarkPass::definePushConstantRanges() {
    return {vk::PushConstantRange{vk::ShaderStageFlagBits::eCompute, 0, sizeof(PushConstants)}};
}

void CSGVBenchmarkPass::freeResources() {
    VK_DEVICE_DESTROY(device(), m_query_pool_timestamps);
    m_usegmented_volume_info = nullptr;
    m_cache_buffer = nullptr;
    for (auto &b : m_split_encoding_buffers)
        b = nullptr;
    m_split_encoding_buffer_addresses_buffer = nullptr;
    m_brick_starts_buffer = nullptr;
    m_detail_starts_buffer = nullptr;
    m_detail_buffer = nullptr;

    PassCompute::freeResources();
}

} // namespace volcanite
