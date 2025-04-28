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
#include <vvv/passes/PassSimpleSsao.hpp>

namespace vvv {

PassSimpleSsao::PassSimpleSsao(GpuContextPtr ctx, const std::shared_ptr<MultiBuffering> &multiBuffering,
                               vk::ImageUsageFlags outputImageUsage, const std::string &label, Algorithm algorithm)
    : WithMultiBuffering(multiBuffering), WithGpuContext(ctx),
      m_ssaoPass(ctx, multiBuffering, PassBlur::getInputImageUsageFlags(), label + ".ssao", algorithm),
      m_blurPass(ctx, multiBuffering, 5, PassBlur::DepthNormal, outputImageUsage, label + ".blur") {
}

void PassSimpleSsao::allocateResources() {
    m_ssaoPass.allocateResources();
    m_blurPass.allocateResources();
}

void PassSimpleSsao::initSwapchainResources() {
    m_ssaoPass.initSwapchainResources();
    m_blurPass.initSwapchainResources();
}

void PassSimpleSsao::releaseSwapchain() {
    m_ssaoPass.releaseSwapchain();
    m_blurPass.releaseSwapchain();
}

void PassSimpleSsao::freeResources() {
    m_ssaoPass.freeResources();
    m_blurPass.freeResources();
}

void PassSimpleSsao::setInputTextures(Texture *depthTexture, Texture *normalTexture) {
    m_ssaoPass.setInputTextures(depthTexture, normalTexture);
    m_blurPass.setInputTexturesBilateral(depthTexture, normalTexture);
}

RendererOutput PassSimpleSsao::renderSsao(AwaitableList awaitBeforeExecution, BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) {
    auto ssaoResult = m_ssaoPass.renderSsao(std::move(awaitBeforeExecution), awaitBinaryAwaitableList, signalBinarySemaphore);

    if (m_blurPass.getKernelRadius() == 0)
        return ssaoResult;

    m_blurPass.setInputTexture(ssaoResult.texture);
    auto blurResult = m_blurPass.renderBlur(ssaoResult.renderingComplete);

    return blurResult;
}

void PassSimpleSsao::addToGui(GuiInterface::GuiElementList *gui, std::optional<std::function<void(int, bool)>> shaderRecompileCallback) {
    m_ssaoPass.addToGui(gui, std::move(shaderRecompileCallback));
    gui->addInt([this](int i) { setBlurKernelRadius(i); }, [this]() { return getBlurKernelRadius(); }, "filter radius", 0, 32, 1);
}

PassSimpleApplySsao::PassSimpleApplySsao(GpuContextPtr ctx, const std::shared_ptr<MultiBuffering> &multiBuffering,
                                         vk::ImageUsageFlags outputImageUsage, const std::string &label, Algorithm algorithm)
    : WithGpuContext(ctx), WithMultiBuffering(multiBuffering),
      PassSimpleSsao(ctx, multiBuffering, vk::ImageUsageFlagBits::eSampled, label, algorithm),
      m_applyPass(SinglePassComputeSettings{.ctx = ctx, .label = label + ".apply", .multiBuffering = multiBuffering, .queueFamilyIndex = ctx->getQueueFamilyIndices().graphics.value()},
                  SimpleGlslShaderRequest{.filename = "passes/apply_ssao.comp", .label = label + ".apply"}),
      m_outputImageUsage(outputImageUsage) {
}

void PassSimpleApplySsao::allocateResources() {
    PassSimpleSsao::allocateResources();
    m_applyPass.allocateResources();
    m_uniform = m_applyPass.getUniformSet("options");
}

void PassSimpleApplySsao::initSwapchainResources() {
    PassSimpleSsao::initSwapchainResources();

    auto extent = getCtx()->getWsi()->getScreenExtent();
    m_applyPass.setGlobalInvocationSize(extent.width, extent.height);

    TextureReflectionOptions opts = {.width = extent.width, .height = extent.height, .format = vk::Format::eR8G8B8A8Unorm, .queues = {m_applyPass.getQueueFamilyIndex()}};
    opts.usage |= m_outputImageUsage;
    m_outputTextures = m_applyPass.reflectTextures("outputTexture", opts);

    for (auto &tex : *m_outputTextures)
        tex->initResources();
}

void PassSimpleApplySsao::releaseSwapchain() {
    m_outputTextures = nullptr;

    PassSimpleSsao::releaseSwapchain();
}

void PassSimpleApplySsao::freeResources() {
    m_applyPass.freeResources();
    PassSimpleSsao::freeResources();
}

void PassSimpleApplySsao::setInputTextures(Texture *depthTexture, Texture *normalTexture, Texture *colorTexture) {
    PassSimpleSsao::setInputTextures(depthTexture, normalTexture);

    assert(colorTexture->aspectMask & vk::ImageAspectFlagBits::eColor);

    m_inputColorTexture = colorTexture;
    m_applyPass.setImageSampler("inputTexture", *colorTexture, vk::ImageLayout::eShaderReadOnlyOptimal);
}

RendererOutput PassSimpleApplySsao::renderSsao(AwaitableList awaitBeforeExecution, BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) {
    if (g_ssaoIntensity == 0) {
        auto await = m_inputColorTexture->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eAllGraphics, {.await = awaitBeforeExecution});
        return {.texture = m_inputColorTexture, .renderingComplete = {await}, .queueFamilyIndex = m_applyPass.getQueueFamilyIndex()};
    }

    auto bluredSsaoResult = PassSimpleSsao::renderSsao(awaitBeforeExecution, awaitBinaryAwaitableList, signalBinarySemaphore);

    m_applyPass.setStorageImage("outputTexture", *m_outputTextures->getActive(), vk::ImageLayout::eGeneral);
    m_applyPass.setImageSampler("ssaoTexture", *bluredSsaoResult.texture, vk::ImageLayout::eShaderReadOnlyOptimal);
    m_uniform->setUniform("intensity", g_ssaoIntensity);
    m_uniform->setUniform("gamma", g_ssaoGamma);
    m_uniform->upload(getActiveIndex());

    const auto stage = vk::PipelineStageFlagBits::eComputeShader;
    const auto opts = detail::OpenGLStyleSubmitOptions{.await = {bluredSsaoResult.renderingComplete}};
    auto colorAwait = m_inputColorTexture->setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal, stage, opts);
    auto ssaoAwait = bluredSsaoResult.texture->setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal, stage, opts);
    auto outputAwait = m_outputTextures->getActive()->setImageLayout(vk::ImageLayout::eGeneral, stage, opts);

    auto applyAwait = m_applyPass.execute({colorAwait, ssaoAwait, outputAwait});
    return {.texture = m_outputTextures->getActive().get(), .renderingComplete = {applyAwait}, .queueFamilyIndex = m_applyPass.getQueueFamilyIndex()};
}

void PassSimpleApplySsao::addToGui(GuiInterface::GuiElementList *gui, std::optional<std::function<void(int, bool)>> shaderRecompileCallback) {
    gui->addFloat(&g_ssaoIntensity, "Intensity", 0, 1, 0.1f, 2);
    gui->addFloat(&g_ssaoGamma, "Gamma", 0.5, 2, 1, 2);
    PassSimpleSsao::addToGui(gui, shaderRecompileCallback);
}

} // namespace vvv