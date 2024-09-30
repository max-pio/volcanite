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

using namespace vvv;

namespace volcanite {

AwaitableHandle PassCompSegVolRender::execute(AwaitableList awaitBeforeExecution, BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore* signalBinarySemaphore) {

    // fill command buffer
    auto &commandBuffer = m_commandBuffer->getActive();
    commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    getCtx()->debugMarker->beginRegion(commandBuffer, "total_rendering", glm::vec4(1.f));
    // potential cache reset / garbage collection
    if(m_reset_cache) {
        // will always be called on first frame => wait for all transfers to finish
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, {}, {vk::MemoryBarrier(vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eMemoryRead)}, nullptr, nullptr);
        executeCommands(commandBuffer, CACHECLEAR);
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {}, {vk::MemoryBarrier(vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eMemoryRead)}, nullptr, nullptr);
        Logger(DEBUG) << "hard reset brick cache";
        m_reset_cache = false;
    }

    // block request and visibility classification
    getCtx()->debugMarker->beginRegion(commandBuffer, "request", glm::vec4(0.f, 0.f, 0.9f, 1.f));
    executeCommands(commandBuffer, REQUEST);
    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {},
                                  {vk::MemoryBarrier(vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eMemoryWrite | vk::AccessFlagBits::eMemoryRead)},
                                  nullptr, nullptr);
    getCtx()->debugMarker->endRegion(commandBuffer);
//    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, {}, {vk::MemoryBarrier(vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite)},
//                                  nullptr, nullptr);
    // fetch new blocks at the end of the cache
    getCtx()->debugMarker->beginRegion(commandBuffer, "provision", glm::vec4(0.f, 0.3f, 0.6f, 1.f));
    executeCommands(commandBuffer, PROVISION);
    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {}, {vk::MemoryBarrier(vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eMemoryRead)}, nullptr, nullptr);
    getCtx()->debugMarker->endRegion(commandBuffer);
//    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, {}, {vk::MemoryBarrier(vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite)},
//                                  nullptr, nullptr);
    // assign brick decompression requests to free cache regions
    getCtx()->debugMarker->beginRegion(commandBuffer, "assign", glm::vec4(0.f, 1.f, 0.6f, 0.3f));
    executeCommands(commandBuffer, ASSIGN);
    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {}, {vk::MemoryBarrier(vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eMemoryRead)}, nullptr, nullptr);
    getCtx()->debugMarker->endRegion(commandBuffer);

    // decompress all bricks that request it to their assigned cache region (if it exists)
    getCtx()->debugMarker->beginRegion(commandBuffer, "decompress", glm::vec4(0.f, 1.f, 0.f, 1.f));
    executeCommands(commandBuffer, DECOMPRESS);
    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {}, {vk::MemoryBarrier(vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eMemoryRead)}, nullptr, nullptr);
    getCtx()->debugMarker->endRegion(commandBuffer);

    // ray marching
    getCtx()->debugMarker->beginRegion(commandBuffer, "rendering", glm::vec4(1.f, 0.f, 0.f, 1.f));
    executeCommands(commandBuffer, RENDERING);
    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, {}, {vk::MemoryBarrier(vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eMemoryRead)}, nullptr, nullptr);
    getCtx()->debugMarker->endRegion(commandBuffer);

    // inpainting (for progressive pixel subsampling rendering)
    getCtx()->debugMarker->beginRegion(commandBuffer, "resolve", glm::vec4(0.8f, 0.5f, 0.f, 1.f));
    executeCommands(commandBuffer, RESOLVE);
    getCtx()->debugMarker->endRegion(commandBuffer);

    getCtx()->debugMarker->endRegion(commandBuffer); // total_rendering
    commandBuffer.end();
    return getCtx()->sync->submit(commandBuffer, m_queueFamilyIndex, awaitBeforeExecution, vk::PipelineStageFlagBits::eAllCommands, awaitBinaryAwaitableList, signalBinarySemaphore);
}

void PassCompSegVolRender::executeCommands(vk::CommandBuffer commandBuffer, CSGVRenderStage pipeline_index) {
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
        for (const auto &s: m_shader_defines)
            ss << s << " ";
        Logger(DEBUG) << ss.str();
    }
    ShaderCompileErrorCallback compileErrorCallback = [](const ShaderCompileError& err) {
        Logger(ERROR) << err.errorText;
        return ShaderCompileErrorCallbackAction::USE_PREVIOUS_CODE;
    };
    return {std::make_shared<Shader>(SimpleGlslShaderRequest{.filename="volcanite/renderer/csgv_cacheclear.comp", .defines= m_shader_defines, .label="csgv_cacheclear.comp"}, compileErrorCallback),
            std::make_shared<Shader>(SimpleGlslShaderRequest{.filename="volcanite/renderer/csgv_request.comp", .defines= m_shader_defines, .label="csgv_request.comp"}, compileErrorCallback),
            std::make_shared<Shader>(SimpleGlslShaderRequest{.filename="volcanite/renderer/csgv_provision.comp", .defines= m_shader_defines, .label="csgv_provision.comp"}, compileErrorCallback),
            std::make_shared<Shader>(SimpleGlslShaderRequest{.filename="volcanite/renderer/csgv_assign.comp", .defines= m_shader_defines, .label="csgv_assign.comp"}, compileErrorCallback),
            m_parallel_decode ? std::make_shared<Shader>(SimpleGlslShaderRequest{.filename="volcanite/renderer/csgv_decompress_subgroup_parallel.comp", .defines= m_shader_defines, .label="csgv_decompress_subgroup_parallel.comp"}, compileErrorCallback) :
                                std::make_shared<Shader>(SimpleGlslShaderRequest{.filename="volcanite/renderer/csgv_decompress.comp", .defines= m_shader_defines, .label="csgv_decompress.comp"}, compileErrorCallback),
            std::make_shared<Shader>(SimpleGlslShaderRequest{.filename="volcanite/renderer/csgv_renderer.comp", .defines= m_shader_defines, .label="csgv_renderer.comp"}, compileErrorCallback),
            std::make_shared<Shader>(SimpleGlslShaderRequest{.filename="volcanite/renderer/csgv_upsample_resolve.comp", .defines= m_shader_defines, .label="csgv_upsample_resolve.comp"}, compileErrorCallback)
//            std::make_shared<Shader>(SimpleGlslShaderRequest{.filename="volcanite/renderer/csgv_denoise_resolve.comp", .defines= m_shader_defines, .label="csgv_denoise_resolve.comp"}, compileErrorCallback)
            };
}

std::vector<vk::PushConstantRange> PassCompSegVolRender::definePushConstantRanges() {
    return {};
}

} // namspace vvv
