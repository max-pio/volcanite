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

#include "volcanite/renderer/PassCompSegVolRender.hpp"

#include "csgv_constants.incl"

using namespace vvv;

namespace volcanite {

void PassCompSegVolRender::startTimingQuery(const vk::CommandBuffer &commandBuffer, const TimingStageMS stage) {
    if (!m_timing_active)
        return;
    commandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, m_timestamp_query_pool, 2 * static_cast<int>(stage));
    m_timing_active_stage_bits |= (1 << stage);
}

void PassCompSegVolRender::stopTimingQuery(const vk::CommandBuffer &commandBuffer, const TimingStageMS stage) const {
    if (!m_timing_active)
        return;
    commandBuffer.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, m_timestamp_query_pool, 2 * static_cast<int>(stage) + 1);
}

AwaitableHandle PassCompSegVolRender::execute(AwaitableList awaitBeforeExecution, BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) {

    // fill command buffer
    const auto &commandBuffer = m_commandBuffer->getActive();
    commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    if (m_timing_active) {
        // Note: based on how the Compressed Segmentation Volume renderer operates, we assume here that the previous frame finished execution at this point.
        // i.e. the CompressedSegmentationVolumeRenderer waits for the awaitable of the previous frame before submitting the next frame.
        // This is due to the frame to frame dependency of decoding and caching. If the cache stages are not used, this requirement could be lifted.
        // If the previous frame did NOT finish execution, the results for the timings might not be ready yet.
        if (m_timing_frame > 0)
            readNextGPUTimings();
        m_timing_frame++;
        m_timing_active_stage_bits = 0u;
        commandBuffer.resetQueryPool(m_timestamp_query_pool, 0, GPU_TIMINGS_COUNT);
    }

    // all uploads must be finished before the rendering can access the buffers
    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, {},
                                  {vk::MemoryBarrier(vk::AccessFlagBits::eTransferWrite,
                                                     vk::AccessFlagBits::eShaderRead)},
                                  nullptr, nullptr);

    getCtx()->debugMarker->beginRegion(commandBuffer, "total_rendering", glm::vec4(1.f));

    startTimingQuery(commandBuffer, CACHE_MANAGE_MS);

    // potential cache reset / garbage collection
    if (m_render_update_flags & UPDATE_CLEAR_CACHE) {
        // will always be called on first frame => wait for all transfers to finish
        executeCommands(commandBuffer, CACHECLEAR);
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                      vk::PipelineStageFlagBits::eComputeShader, {},
                                      {vk::MemoryBarrier(vk::AccessFlagBits::eMemoryWrite,
                                                         vk::AccessFlagBits::eMemoryRead)},
                                      nullptr, nullptr);
    }

    // block request and visibility classification
    getCtx()->debugMarker->beginRegion(commandBuffer, "request", glm::vec4(0.f, 0.f, 0.9f, 1.f));
    // if cache stages are not enabled, the request stage has to be executed nevertheless on material changes
    // to recompute the empty space information
    if (m_enable_cache_stages || (m_render_update_flags & UPDATE_PMATERIAL)) {
        executeCommands(commandBuffer, REQUEST);
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                                      {},
                                      {vk::MemoryBarrier(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead |
                                                                                               vk::AccessFlagBits::eShaderWrite)},
                                      nullptr, nullptr);
    }
    getCtx()->debugMarker->endRegion(commandBuffer);

    if (m_enable_cache_stages && (m_render_update_flags & UPDATE_RENDER_FRAME)) {
        // fetch new blocks at the end of the cache
        getCtx()->debugMarker->beginRegion(commandBuffer, "provision", glm::vec4(0.f, 0.3f, 0.6f, 1.f));
        executeCommands(commandBuffer, PROVISION);
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                      vk::PipelineStageFlagBits::eComputeShader, {},
                                      {vk::MemoryBarrier(vk::AccessFlagBits::eShaderWrite,
                                                         vk::AccessFlagBits::eShaderRead)},
                                      nullptr, nullptr);
        getCtx()->debugMarker->endRegion(commandBuffer);
        // assign brick decompression requests to free cache regions
        getCtx()->debugMarker->beginRegion(commandBuffer, "assign", glm::vec4(0.f, 1.f, 0.6f, 0.3f));
        executeCommands(commandBuffer, ASSIGN);

        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                      vk::PipelineStageFlagBits::eComputeShader, {},
                                      {vk::MemoryBarrier(vk::AccessFlagBits::eShaderWrite,
                                                         vk::AccessFlagBits::eShaderRead)},
                                      nullptr, nullptr);
        getCtx()->debugMarker->endRegion(commandBuffer);

        stopTimingQuery(commandBuffer, CACHE_MANAGE_MS);

        // decompress all bricks that request it to their assigned cache region (if it exists)
        getCtx()->debugMarker->beginRegion(commandBuffer, "decompress", glm::vec4(0.f, 1.f, 0.f, 1.f));
        startTimingQuery(commandBuffer, DECOMPRESS_MS);
        executeCommands(commandBuffer, DECOMPRESS);
        stopTimingQuery(commandBuffer, DECOMPRESS_MS);

        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                      vk::PipelineStageFlagBits::eComputeShader, {},
                                      {vk::MemoryBarrier(vk::AccessFlagBits::eShaderWrite,
                                                         vk::AccessFlagBits::eShaderRead)},
                                      nullptr, nullptr);
        getCtx()->debugMarker->endRegion(commandBuffer);
    } else {
        // if the brick cache stages are not executed, the end time stamp for the caching stages must be set here
        stopTimingQuery(commandBuffer, CACHE_MANAGE_MS);
    }

    // ray marching
    if (m_render_update_flags & UPDATE_RENDER_FRAME) {
        getCtx()->debugMarker->beginRegion(commandBuffer, "rendering", glm::vec4(1.f, 0.f, 0.f, 1.f));
        startTimingQuery(commandBuffer, RENDERING_MS);
        executeCommands(commandBuffer, RENDERING);
        stopTimingQuery(commandBuffer, RENDERING_MS);
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                      vk::PipelineStageFlagBits::eComputeShader, {},
                                      {vk::MemoryBarrier(vk::AccessFlagBits::eShaderWrite,
                                                         vk::AccessFlagBits::eShaderRead |
                                                             vk::AccessFlagBits::eShaderWrite)},
                                      nullptr, nullptr);
        getCtx()->debugMarker->endRegion(commandBuffer);
    } else {
        // simply copy the previous ping-pong buffers to the next ping-pong buffers
        getCtx()->debugMarker->beginRegion(commandBuffer, "rendering(dummy)", glm::vec4(1.f, 0.f, 0.f, 1.f));
        executeCommands(commandBuffer, RENDERING_DUMMY);
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                      vk::PipelineStageFlagBits::eComputeShader, {},
                                      {vk::MemoryBarrier(vk::AccessFlagBits::eShaderWrite,
                                                         vk::AccessFlagBits::eShaderRead |
                                                             vk::AccessFlagBits::eShaderWrite)},
                                      nullptr, nullptr);
        getCtx()->debugMarker->endRegion(commandBuffer);
    }

    // sample accumulation, post processing, and inpainting
    if (m_render_update_flags & (UPDATE_RENDER_FRAME | UPDATE_PRESOLVE)) {
        getCtx()->debugMarker->beginRegion(commandBuffer, "resolve", glm::vec4(0.8f, 0.5f, 0.f, 1.f));
        startTimingQuery(commandBuffer, POSTPROCESS_MS);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_pipelines.at(RESOLVE));
        if (hasDescriptors()) {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_pipelineLayout, 0, m_descriptorSets->getActive(), nullptr);
        }
        for (uint32_t i = 0; i < m_atrous_iterations; i++) {
            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {}, {vk::MemoryBarrier(vk::AccessFlagBits::eMemoryRead, vk::AccessFlagBits::eMemoryWrite)}, nullptr, nullptr);
            PushConstants pushConstants{.denoising_iteration = i, .last_denoising_iteration = (m_atrous_iterations - 1u)};
            commandBuffer.pushConstants(m_pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(PushConstants), &pushConstants);
            commandBuffer.dispatch(m_work_group_sizes[RESOLVE].width, m_work_group_sizes[RESOLVE].height, m_work_group_sizes[RESOLVE].depth);
            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {}, {vk::MemoryBarrier(vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eMemoryRead)}, nullptr, nullptr);
        }
        stopTimingQuery(commandBuffer, POSTPROCESS_MS);
        getCtx()->debugMarker->endRegion(commandBuffer);
    }

    getCtx()->debugMarker->endRegion(commandBuffer); // total_rendering

    // later buffer transfers (e.g. material uploads) must wait for the previous buffer uploads to finish to prevent write-write hazards
    // and for the shader to finish all reads
    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer, {}, {vk::MemoryBarrier(vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eTransferWrite)}, nullptr, nullptr);
    commandBuffer.end();

    // reset update flags
    m_render_update_flags = 0u;

    return getCtx()->sync->submit(commandBuffer, m_queueFamilyIndex, awaitBeforeExecution, vk::PipelineStageFlagBits::eComputeShader, awaitBinaryAwaitableList, signalBinarySemaphore);
}

void PassCompSegVolRender::executeCommands(vk::CommandBuffer commandBuffer, CSGVRenderStage pipeline_index) {
    assert((m_work_group_sizes[pipeline_index].width * m_work_group_sizes[pipeline_index].height * m_work_group_sizes[pipeline_index].depth) && "dispatching empty work group size");
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_pipelines.at(pipeline_index)); // each compute shader has one pipeline
    if (hasDescriptors()) {
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_pipelineLayout, 0, m_descriptorSets->getActive(), nullptr);
    }
    commandBuffer.dispatch(m_work_group_sizes[pipeline_index].width, m_work_group_sizes[pipeline_index].height, m_work_group_sizes[pipeline_index].depth);
}

std::vector<std::shared_ptr<Shader>> PassCompSegVolRender::createShaders() {
    {
        std::stringstream ss;
        ss << "Shader Definitions: ";
        for (const auto &s : m_shader_defines)
            ss << s << " ";
        Logger(Debug) << ss.str();
    }
    ShaderCompileErrorCallback compileErrorCallback = [](const ShaderCompileError &err) {
        Logger(Error) << err.errorText;
        return ShaderCompileErrorCallbackAction::USE_PREVIOUS_CODE;
    };
    return {
        std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "volcanite/renderer/csgv_cacheclear.comp", .defines = m_shader_defines, .label = "csgv_cacheclear.comp"}, compileErrorCallback),
        std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "volcanite/renderer/csgv_request.comp", .defines = m_shader_defines, .label = "csgv_request.comp"}, compileErrorCallback),
        std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "volcanite/renderer/csgv_provision.comp", .defines = m_shader_defines, .label = "csgv_provision.comp"}, compileErrorCallback),
        std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "volcanite/renderer/csgv_assign.comp", .defines = m_shader_defines, .label = "csgv_assign.comp"}, compileErrorCallback),
        m_parallel_decode ? std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "volcanite/renderer/csgv_decompress_subgroup_parallel.comp", .defines = m_shader_defines, .label = "csgv_decompress_subgroup_parallel.comp"}, compileErrorCallback) : std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "volcanite/renderer/csgv_decompress.comp", .defines = m_shader_defines, .label = "csgv_decompress.comp"}, compileErrorCallback),
        std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "volcanite/renderer/csgv_renderer.comp", .defines = m_shader_defines, .label = "csgv_renderer.comp"}, compileErrorCallback),
        std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "volcanite/renderer/csgv_denoise_resolve.comp", .defines = m_shader_defines, .label = "csgv_denoise_resolve.comp"}, compileErrorCallback),
        std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "volcanite/renderer/csgv_renderer_dummy.comp", .defines = m_shader_defines, .label = "csgv_renderer_dummy.comp"}, compileErrorCallback),
    };
}

std::vector<vk::PushConstantRange> PassCompSegVolRender::definePushConstantRanges() {
    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    return {pushConstantRange};
}

// GPU Timing ---------------------------------------------------------------------------------------------------------------------------------------------------------

bool PassCompSegVolRender::initializeGPUTimingQueries() {
    if (m_timestamp_query_pool)
        Logger(Warn) << "PassCompSegVolRender GPU timing queries were already initialized.";

    m_frame_gpu_timings = {};
    m_timing_active = false;

    const auto device_limits = getCtx()->getPhysicalDevice().getProperties().limits;
    m_timestamp_period = device_limits.timestampPeriod;
    if (m_timestamp_period == 0.f || !device_limits.timestampComputeAndGraphics) {
        m_enable_gpu_timing = false;
        m_timestamp_query_pool = nullptr;
        return false;
    }

    vk::QueryPoolCreateInfo query_pool_info{};
    query_pool_info.queryType = vk::QueryType::eTimestamp;
    query_pool_info.queryCount = GPU_TIMINGS_COUNT;
    if (const auto query_pool_res = getCtx()->getDevice().createQueryPool(query_pool_info))
        m_timestamp_query_pool = query_pool_res;
    else {
        m_enable_gpu_timing = false;
        return false;
    }
    return true;
}

/// Queries the GPU stage timings of the last executed frame and appends them to the m_frame_gpu_timings vector.
/// Must be called after the last submitted frame finished all GPU work but before submitting the next frame.
void PassCompSegVolRender::readNextGPUTimings() {
    // add a next GPU timings initialized with invalid values
    m_frame_gpu_timings.emplace_back(-1.f, -1.f, -1.f, -1.f);

    uint64_t time_stamp_avail[2 * GPU_TIMINGS_COUNT]; // timestamp + availability each
    const auto query_res = device().getQueryPoolResults(m_timestamp_query_pool, 0, GPU_TIMINGS_COUNT,
                                                        2 * GPU_TIMINGS_COUNT * sizeof(uint64_t), time_stamp_avail,
                                                        2 * sizeof(uint64_t),
                                                        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);

    if (query_res != vk::Result::eSuccess && query_res != vk::Result::eNotReady) {
        Logger(Warn) << "Could not query timestamp of frame " << (m_frame_gpu_timings.size() - 1);
        return;
    }

    for (int i = 0; i < GPU_TIMINGS_COUNT / 2; i++) {
        // some stages are not always executed. the stage bits denote which stages were actually queried.
        if ((m_timing_active_stage_bits & (1u << i)) == 0u) {
            m_frame_gpu_timings.back()[i] = 0.f;
            continue;
        }

        // each stage has 4 uint64 values in time_stamp_avail:
        // [start_timestamp][start_avail][end_timestamp][end_avail]

        // leave the duration of a stage at -1 if one of the timestamps is not available
        if (time_stamp_avail[i * 4 + 3] == 0u || time_stamp_avail[i * 4 + 1] == 0u) {
            Logger(Warn) << "GPU timing was not available for frame " << (m_frame_gpu_timings.size() - 1) << ", stage " << i;
            continue;
        }

        constexpr double NS_PER_MS = 1000000.;
        m_frame_gpu_timings.back()[i] = static_cast<float>((static_cast<double>(time_stamp_avail[i * 4 + 2] - time_stamp_avail[i * 4 + 0])) * m_timestamp_period / NS_PER_MS);
    }
}

void PassCompSegVolRender::startFrameTimeTracking() {
    if (!m_enable_gpu_timing)
        throw std::runtime_error("enable_gpu_timing must be true to use GPU frame time tracking in PassCompSegVolRender.");
    m_timing_active = true;
    m_timing_frame = 0u;
    m_frame_gpu_timings.clear();
    m_frame_gpu_timings.reserve(4096);
}

/// Waits for awaitLastFrameFinished and measures one last set of GPU stage timings.
/// @returns the GPU stage (TimingStageMS) timings in [ms] for each frame between startFrameTimeTracking() and the last frame (execute) submitted for rendering.
const std::vector<glm::vec4> &PassCompSegVolRender::stopFrameTimeTracking(const std::optional<AwaitableList> &awaitLastFrameFinished) {
    if (!m_enable_gpu_timing)
        throw std::runtime_error("enable_gpu_timing must be true to use GPU frame time tracking in PassCompSegVolRender.");
    if (!m_timing_active) {
        throw std::runtime_error("PassCompSegVolRender frame time tracking was not started before stopping results.");
    }

    // TODO: all stopFrameTimeTracking() methods should store their own awaitable to the last timed frame and wait for it, not pass it as argument
    // if the last frame is rendering, wait for completion and track
    if (awaitLastFrameFinished.has_value())
        getCtx()->sync->hostWaitOnDevice(awaitLastFrameFinished.value(), 60 * 1000000000ull);

    readNextGPUTimings();

    m_timing_active = false;
    return m_frame_gpu_timings;
}

[[nodiscard]] const std::vector<glm::vec4> &PassCompSegVolRender::getLastFrameTimeTrackingResults() const {
    // if (m_timing_active) {
    //     throw std::runtime_error("PassCompSegVolRender frame time tracking is active. Call stopFrameTimeTracking() before getLastFrameTimeTrackingResults().");
    // }
    return m_frame_gpu_timings;
}

} // namespace volcanite
