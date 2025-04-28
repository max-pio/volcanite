//  Copyright (C) 2024, Patrick Jaberg, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
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

#include <utility>
#include <vvv/passes/PassSsao.hpp>

#include <imgui/imgui.h>

namespace vvv {

const std::map<PassSsao::Algorithm, const char *> PassSsao::algorithmToDefine = {
    {Crytek, "SSAO_CRYTEK"},
    {Starcraft, "SSAO_STARCRAFT"},
    {Hbao, "SSAO_HBAO"}};
const std::map<PassSsao::Algorithm, const char *> PassSsao::algorithmToGuiNames = {
    {Crytek, "Crytek SSAO"},
    {Starcraft, "Starcraft SSAO (default)"},
    {Hbao, "HBAO"}};

PassSsao::PassSsao(GpuContextPtr ctx, const std::shared_ptr<MultiBuffering> &multiBuffering, vk::ImageUsageFlags outputImageUsage, const std::string &label, Algorithm algorithm)
    : WithMultiBuffering(multiBuffering), WithGpuContext(ctx), SinglePassCompute(SinglePassComputeSettings{.ctx = ctx, .label = label, .multiBuffering = multiBuffering, .queueFamilyIndex = ctx->getQueueFamilyIndices().graphics.value()},
                                                                                 SimpleGlslShaderRequest{
                                                                                     .filename = "passes/ssao.comp",
                                                                                     .defines = {algorithmToDefine.at(algorithm)},
                                                                                     .label = label + ".shader",
                                                                                 }),
      g_ssaoAlgorithm(algorithm),
      m_outputImageUsage(outputImageUsage), m_perFrameConstantsUniform(nullptr) {}

void PassSsao::addToGui(GuiInterface::GuiElementList *gui, std::optional<std::function<void(int, bool)>> shaderRecompileCallback) {
    const bool allowAlgorithmToChange = shaderRecompileCallback.has_value();
    if (allowAlgorithmToChange) {
        std::vector<std::string> options;
        for (auto &[algorithm, text] : algorithmToGuiNames)
            options.emplace_back(text);

        gui->addCombo(reinterpret_cast<int *>(&g_ssaoAlgorithm), options, shaderRecompileCallback.value());
    }
    gui->addInt(&g_ssaoNumSamples, "Num Samples");
    gui->addFloat(&g_ssaoRadius, "Radius", 0.001f, 1.0f, 0.001f, 3);

    if (allowAlgorithmToChange) {
        // implement settings as dynamic ImGui code to that the gui changes along with g_ssaoAlgorithm
        gui->addCustomCode([this]() {
            if (g_ssaoAlgorithm == Starcraft || g_ssaoAlgorithm == Hbao)
                ImGui::SliderFloat("Bias", &g_ssaoBias, 0, 0.1f);
            if (g_ssaoAlgorithm == Starcraft)
                ImGui::SliderFloat("Falloff", &g_ssaoFalloff, 0, 15);
            if (g_ssaoAlgorithm == Hbao)
                ImGui::SliderInt("Num Steps", &g_ssaoNumSteps, 1, 32);
        },
                           "");
    } else {
        // we can use static gui->add*() here
        if (g_ssaoAlgorithm == Starcraft || g_ssaoAlgorithm == Hbao)
            gui->addFloat(&g_ssaoBias, "Bias", 0, 0.1f, 0.001f, 3);
        if (g_ssaoAlgorithm == Starcraft)
            gui->addFloat(&g_ssaoFalloff, "Falloff", 0, 15, 0.01f, 2);
        if (g_ssaoAlgorithm == Hbao)
            gui->addInt(&g_ssaoNumSteps, "Num Steps");
    }
}

void PassSsao::freeResources() {
    SinglePassCompute::freeResources();
}

void PassSsao::setInputTextures(Texture *depthTexture, Texture *normalTexture) {
    assert(depthTexture->aspectMask & vk::ImageAspectFlagBits::eDepth);
    assert(normalTexture->aspectMask & vk::ImageAspectFlagBits::eColor);

    m_inputDepthTexture = depthTexture;
    m_inputNormalTexture = normalTexture;

    m_inputDepthLayout = FormatHasDepth(static_cast<VkFormat>(m_inputDepthTexture->format)) ? vk::ImageLayout::eDepthStencilReadOnlyOptimal : vk::ImageLayout::eShaderReadOnlyOptimal;

    setImageSampler("depthTexture", *m_inputDepthTexture, m_inputDepthLayout);
    setImageSampler("normalTexture", *m_inputNormalTexture, vk::ImageLayout::eShaderReadOnlyOptimal);
}

void PassSsao::initSwapchainResources() {
    auto extent = getCtx()->getWsi()->getScreenExtent();

    TextureReflectionOptions opts = {.width = extent.width, .height = extent.height, .format = vk::Format::eR8G8B8A8Unorm, .queues = {m_queueFamilyIndex}};
    opts.usage |= m_outputImageUsage;
    m_outputTextures = reflectTextures("outputTexture", opts);

    for (auto &tex : *m_outputTextures)
        tex->initResources();
}

void PassSsao::releaseSwapchain() {
    m_outputTextures = nullptr;
}

RendererOutput PassSsao::renderSsao(AwaitableList awaitBeforeExecution, BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) {
    setStorageImage("outputTexture", *m_outputTextures->getActive(), vk::ImageLayout::eGeneral);

    updateUniforms(getActiveIndex());

    auto &commandBuffer = m_commandBuffer->getActive();
    commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    m_inputDepthTexture->setImageLayout(commandBuffer, m_inputDepthLayout, vk::PipelineStageFlagBits::eComputeShader);
    m_inputNormalTexture->setImageLayout(commandBuffer, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eComputeShader);
    m_outputTextures->getActive()->setImageLayout(commandBuffer, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eComputeShader);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_pipelines[0]);
    if (hasDescriptors())
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_pipelineLayout, 0, m_descriptorSets->getActive(), nullptr);

    const auto extent = getCtx()->getWsi()->getScreenExtent();
    commandBuffer.dispatch(extent.width / 16, extent.height / 16, 1);

    commandBuffer.end();

    auto renderingComplete =
        getCtx()->sync->submit(commandBuffer, m_queueFamilyIndex, std::move(awaitBeforeExecution), vk::PipelineStageFlagBits::eAllCommands, awaitBinaryAwaitableList, signalBinarySemaphore);

    return {.texture = m_outputTextures->getActive().get(), .renderingComplete = {renderingComplete}, .queueFamilyIndex = m_queueFamilyIndex};
}

void PassSsao::updateUniforms(uint32_t index) {
    auto camera = getCtx()->getWsi()->getCamera();
    auto extent = getCtx()->getWsi()->getScreenExtent();
    glm::mat4 world_to_projection = camera->get_world_to_projection_space(extent);
    glm::mat4 projection_to_view_space = glm::inverse(camera->get_view_to_projection_space(extent));

    m_perFrameConstantsUniform->setUniform("projection_to_world_space", glm::inverse(world_to_projection));
    m_perFrameConstantsUniform->setUniform("world_to_projection_space", world_to_projection);
    m_perFrameConstantsUniform->setUniform("projection_to_view_space", projection_to_view_space);
    m_perFrameConstantsUniform->setUniform("near", static_cast<glm::float32>(camera->near));
    m_perFrameConstantsUniform->setUniform("far", static_cast<glm::float32>(camera->far));
    m_perFrameConstantsUniform->setUniform("radius", g_ssaoRadius);
    m_perFrameConstantsUniform->setUniform("num_samples", g_ssaoNumSamples);
    m_perFrameConstantsUniform->setUniform("bias", g_ssaoBias);
    m_perFrameConstantsUniform->setUniform("falloff", g_ssaoFalloff);
    m_perFrameConstantsUniform->setUniform("num_steps", g_ssaoNumSteps);

    m_perFrameConstantsUniform->upload(index);
}

void PassSsao::allocateResources() {
    // again set shader args here to allow freeResources()+allocateResources() cycle to change the algorithm
    SinglePassCompute::setShaderArgs(SimpleGlslShaderRequest{
        .filename = "passes/ssao.comp",
        .defines = {algorithmToDefine.at(g_ssaoAlgorithm)},
        .label = m_label + ".shader",
    });

    SinglePassCompute::allocateResources();

    m_perFrameConstantsUniform = getUniformSet("per_frame_constants");
}

} // namespace vvv
