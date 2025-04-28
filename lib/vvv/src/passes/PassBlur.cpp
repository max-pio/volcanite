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

#include <cmath>
#include <vvv/passes/PassBlur.hpp>

namespace vvv {

PassBlur::PassBlur(GpuContextPtr ctx, const std::shared_ptr<MultiBuffering> &multiBuffering, int radius, BilateralMode bilateralMode,
                   vk::ImageUsageFlags outputImageUsage, const std::string &label)
    : WithGpuContext(ctx), WithMultiBuffering(multiBuffering), m_outputImageUsage(outputImageUsage), m_bilateralMode(bilateralMode),
      PassCompute(ctx, label, multiBuffering, ctx->getQueueFamilyIndices().graphics.value()),
      m_kernelRadius(radius) {
}

void PassBlur::allocateResources() {
    PassCompute::allocateResources();

    m_uniform = getUniformSet("options");
}

std::vector<std::shared_ptr<Shader>> PassBlur::createShaders() {
    std::string bilateral = "BILATERAL=" + std::to_string(static_cast<uint32_t>(m_bilateralMode));
    m_shader1_h = std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "passes/blur.comp", .defines = {bilateral, "PASS_1"}, .label = m_label + ".shader1_h"});
    m_shader2_v = std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "passes/blur.comp", .defines = {bilateral, "PASS_2"}, .label = m_label + ".shader2_v"});

    return {m_shader1_h, m_shader2_v};
}

void PassBlur::freeResources() {
    m_kernelTexture = nullptr;
    m_kernelDirty = true;

    m_shader1_h = nullptr;
    m_shader2_v = nullptr;

    PassCompute::freeResources();
}

void PassBlur::initSwapchainResources() {
    auto extent = getCtx()->getWsi()->getScreenExtent();

    TextureReflectionOptions opts = {.width = extent.width, .height = extent.height, .format = vk::Format::eR8G8B8A8Unorm, .queues = {m_queueFamilyIndex}};
    opts.usage |= vk::ImageUsageFlagBits::eSampled;
    opts.usage |= m_outputImageUsage;

    m_internalTextures = reflectTextures("outputTexture_H", opts);
    m_outputTextures = reflectTextures("outputTexture_V", opts);

    for (auto &tex : *m_internalTextures)
        tex->initResources();
    for (auto &tex : *m_outputTextures)
        tex->initResources();
}

void PassBlur::releaseSwapchain() {
    m_internalTextures = nullptr;
    m_outputTextures = nullptr;
}

void PassBlur::setInputTexture(vvv::Texture *inputTexture) {
    m_inputTexture = inputTexture;
    setImageSampler("inputTexture_H", *m_inputTexture, vk::ImageLayout::eShaderReadOnlyOptimal);
}

void PassBlur::setInputTexturesBilateral(Texture *depth, Texture *normal) {
    assert(m_bilateralMode != BilateralMode::Disabled || (!depth && !normal));
    assert(m_bilateralMode != BilateralMode::DepthOnly || (depth && !normal));
    assert(m_bilateralMode != BilateralMode::NormalOnly || (!depth && normal));
    assert(m_bilateralMode != BilateralMode::DepthNormal || (depth && normal));
    assert(!depth || depth->aspectMask & vk::ImageAspectFlagBits::eDepth);
    assert(!normal || normal->aspectMask & vk::ImageAspectFlagBits::eColor);

    m_bilateralDepthTexture = depth;
    m_bilateralNormalTexture = normal;

    if (depth)
        setImageSampler("bilateralDepthTexture", *depth, vk::ImageLayout::eDepthStencilReadOnlyOptimal);
    if (normal)
        setImageSampler("bilateralNormalTexture", *normal, vk::ImageLayout::eShaderReadOnlyOptimal);
}

std::shared_ptr<Buffer> PassBlur::uploadKernelTexture(VkCommandBuffer commandBuffer) {
    std::vector<uint16_t> kernelData(2 * m_kernelRadius + 1);
    float sum = 0;

    for (int i = 0; i < kernelData.size(); i++) {
        const float truncate = 3.0f;
        const float t = 2 * truncate * static_cast<float>(i) / static_cast<float>(kernelData.size() - 1) - truncate;
        float val = std::exp(-0.5f * t * t);
        kernelData[i] = static_cast<uint16_t>(val * std::numeric_limits<uint16_t>::max());
        sum += static_cast<float>(kernelData[i]);
    }

    for (uint16_t &val : kernelData)
        val = static_cast<uint16_t>((static_cast<float>(val) / sum) * std::numeric_limits<uint16_t>::max());

    std::string msg = "gaussian kernel (radius=" + std::to_string(m_kernelRadius) + "): ";
    double s = 0;
    for (uint16_t &val : kernelData) {
        msg += std::to_string(val) + " ";
        s += val;
    }
    Logger(Debug) << msg << "[sum:" << s << "/" << std::numeric_limits<uint16_t>::max() << " " << s / std::numeric_limits<uint16_t>::max() * 100.0 << "%]";

    auto usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
    m_kernelTexture = reflectTexture("kernelTexture", {.width = static_cast<uint32_t>(kernelData.size()), .format = vk::Format::eR16Unorm, .usage = usage, .queues = {m_queueFamilyIndex}});

    auto staging = std::make_shared<Buffer>(getCtx(), vvv::BufferSettings{.label = "staging(" + m_label + ")", .byteSize = m_kernelTexture->memorySize()});
    m_kernelTexture->upload(commandBuffer, *staging, kernelData.data(), vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eComputeShader);

    m_uniform->setUniform("kernelRadius", m_kernelRadius);

    for (int i = 0; i < getIndexCount(); i++) {
        m_uniform->upload(i);
    }

    return staging;
}

RendererOutput PassBlur::renderBlur(vvv::AwaitableList awaitBeforeExecution, vvv::BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) {
    if (m_kernelTexture)
        setImageSampler("kernelTexture", *m_kernelTexture, vk::ImageLayout::eShaderReadOnlyOptimal);
    setStorageImage("outputTexture_H", *m_internalTextures->getActive(), vk::ImageLayout::eGeneral);
    setImageSampler("inputTexture_V", *m_internalTextures->getActive(), vk::ImageLayout::eShaderReadOnlyOptimal);
    setStorageImage("outputTexture_V", *m_outputTextures->getActive(), vk::ImageLayout::eGeneral);

    auto &commandBuffer = m_commandBuffer->getActive();
    commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    if (m_kernelDirty) {
        getMultiBuffering()->keepAlive(m_kernelTexture);

        auto staging = uploadKernelTexture(commandBuffer);
        getMultiBuffering()->keepAlive(staging);

        setImageSampler("kernelTexture", *m_kernelTexture, vk::ImageLayout::eShaderReadOnlyOptimal);
        m_kernelDirty = false;
    }

    auto extent = getCtx()->getWsi()->getScreenExtent();

    m_inputTexture->setImageLayout(commandBuffer, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eComputeShader);
    m_internalTextures->getActive()->setImageLayout(commandBuffer, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eComputeShader);

    if (m_bilateralDepthTexture)
        m_bilateralDepthTexture->setImageLayout(commandBuffer, vk::ImageLayout::eDepthStencilReadOnlyOptimal, vk::PipelineStageFlagBits::eComputeShader);
    if (m_bilateralNormalTexture)
        m_bilateralNormalTexture->setImageLayout(commandBuffer, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eComputeShader);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_pipelines[0]);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_pipelineLayout, 0, m_descriptorSets->getActive(), nullptr);
    commandBuffer.dispatch(extent.width / 16, extent.height / 16, 1);

    m_internalTextures->getActive()->setImageLayout(commandBuffer, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eComputeShader);
    m_outputTextures->getActive()->setImageLayout(commandBuffer, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eComputeShader);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_pipelines[1]);
    commandBuffer.dispatch(extent.width / 16, extent.height / 16, 1);

    commandBuffer.end();

    auto renderingComplete =
        getCtx()->sync->submit(commandBuffer, m_queueFamilyIndex, awaitBeforeExecution, vk::PipelineStageFlagBits::eAllCommands, awaitBinaryAwaitableList, signalBinarySemaphore);

    return {.texture = m_outputTextures->getActive().get(), .renderingComplete = {renderingComplete}, .queueFamilyIndex = m_queueFamilyIndex};
}

} // namespace vvv
