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

AwaitableHandle PassCompSegVolRender::execute(AwaitableList awaitBeforeExecution, BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) {

    // fill command buffer
    auto &commandBuffer = m_commandBuffer->getActive();
    commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    // all uploads must be finished before the rendering can access the buffers
    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, {},
                                  {vk::MemoryBarrier(vk::AccessFlagBits::eTransferWrite,
                                                     vk::AccessFlagBits::eShaderRead)},
                                  nullptr, nullptr);

    getCtx()->debugMarker->beginRegion(commandBuffer, "total_rendering", glm::vec4(1.f));
    // potential cache reset / garbage collection
    if (m_render_update_flags & UPDATE_CLEAR_CACHE) {
        // will always be called on first frame => wait for all transfers to finish
        executeCommands(commandBuffer, CACHECLEAR);
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                      vk::PipelineStageFlagBits::eComputeShader, {},
                                      {vk::MemoryBarrier(vk::AccessFlagBits::eMemoryWrite,
                                                         vk::AccessFlagBits::eMemoryRead)},
                                      nullptr, nullptr);
        Logger(Debug) << "hard reset brick cache";
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

        // decompress all bricks that request it to their assigned cache region (if it exists)
        getCtx()->debugMarker->beginRegion(commandBuffer, "decompress", glm::vec4(0.f, 1.f, 0.f, 1.f));
        executeCommands(commandBuffer, DECOMPRESS);
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                      vk::PipelineStageFlagBits::eComputeShader, {},
                                      {vk::MemoryBarrier(vk::AccessFlagBits::eShaderWrite,
                                                         vk::AccessFlagBits::eShaderRead)},
                                      nullptr, nullptr);
        getCtx()->debugMarker->endRegion(commandBuffer);
    }

    // ray marching
    if (m_render_update_flags & UPDATE_RENDER_FRAME) {
        getCtx()->debugMarker->beginRegion(commandBuffer, "rendering", glm::vec4(1.f, 0.f, 0.f, 1.f));
        executeCommands(commandBuffer, RENDERING);
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

} // namespace volcanite
