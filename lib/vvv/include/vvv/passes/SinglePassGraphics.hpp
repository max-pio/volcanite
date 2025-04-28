//  Copyright (C) 2024, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
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

#include <vvv/passes/PassBase.hpp>

#include <vvv/core/preamble.hpp>

#include <vvv/core/MultiBuffering.hpp>

#include <vvv/reflection/GraphicsPipelineReflection.hpp>
#include <vvv/reflection/TextureReflection.hpp>

#include <unordered_map>
#include <utility>

/*
 * ======== Open TODOs ========
 * - allow multiple color attachments
 * - include stencil attachment and implement format checks for depthAttachmentFormat
 * - reflect color (and depth?) attachment in corresponding reflection methods
 * - implement a common superclass 'Pass' for PassCompute and SinglePassGraphics
 */

namespace vvv {

namespace detail {

struct VertexBinding {
    uint32_t binding;               ///< binding point
    std::vector<std::string> names; ///< ordered list of names of vertex shader inputs on this binding
};

} // namespace detail

struct GraphicsPassConfig {
    const vk::PrimitiveTopology primitiveTopology = vk::PrimitiveTopology::eTriangleList;
    std::unordered_map<std::string, vk::Format> colorAttachmentFormats = {}; ///< for each output name: set to vk::Format::Undefined to let it be reflected from shaders or to required format
    std::optional<vk::Format> depthAttachmentFormat = {};                    ///< set to a depth buffer vk::Format::eD[...] to enable depth buffering
    const bool alphaBlending = false;                                        ///< alpha blending not supported yet!
    std::string vertexShaderName = "fullscreen_triangle.vert";
    std::string fragmentShaderName = "white.frag";
    std::string geometryShaderName; ///< geometry stage supported yet!
};

/// This is a typesafe wrapper around the non-typesafe but more flexible `PassComputeDynamic`
// template <PassComputeStructure Types> class PassCompute : public virtual MultiBuffering, public virtual WithGpuContext /*: PassComputeDynamic */ {
//     using UniformSets = decltype(Types::uniformSets)::type;
//     using StorageImages = decltype(Types::storageImages)::type;
//     using ImageSamplers = decltype(Types::imageSamplers)::type;
//     using StorageBuffers = decltype(Types::storageBuffers)::type;
class SinglePassGraphics : public PassBase {
  public:
    void freeResources() override {
        for (auto &tex : m_colorAttachmentTextures)
            tex = nullptr;
        m_depthAttachmentTexture = nullptr;
        PassBase::freeResources();
    }

    //  a pass is either `TimelineSemaphoreWaitable execute(queue)` or `executeCommands(vk::CommandBuffer commandBuffer)`.
    // The first submits to the queue itself and is required for multipass or multiqueue algorithms,
    // the second variant just writes into a command buffer and the caller is responsible for submitting the work. This
    // can be more efficient since the number of submits can be reduced.
    //
    // A `vk::CommandBuffer executeCommands()` variant that returns a secondary commandbuffer without an argument could be more ergonomic and efficient, but
    // harder to synchronize correctly. Not sure...

    [[nodiscard]] AwaitableHandle execute(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr) override {
        assert(isPipelineCreated() && "you must call 'allocateResources' if the pass was created with lazy state initialization.");
        assert(!m_colorAttachmentTextures.empty() && "you must set color attachments before executing a graphics pass.");
        assert((!m_graphicsPassConfig.depthAttachmentFormat.has_value() || m_depthAttachmentTexture) && "you must add depth textures as attachments if a depth attachment format is specified.");

        vk::ClearValue colorClear = {};
        colorClear.color.setFloat32(std::array<float, 4>({0.f, 0.f, 0.f, 0.f}));
        vk::ClearValue depthClear = {};
        depthClear.depthStencil.depth = 1.f;

        // updateUniformBufferMemory(getActiveIndex());

        auto &commandBuffer = m_commandBuffer->getActive();
        commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipelines[0]);
        if (hasDescriptors())
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipelineLayout, 0, m_descriptorSets->getActive(), nullptr);

        std::vector<vk::RenderingAttachmentInfo> renderingAttachmentInfos;
        for (const auto &col : m_colorAttachmentTextures) {
            renderingAttachmentInfos.emplace_back(col->view, vk::ImageLayout::eColorAttachmentOptimal, vk::ResolveModeFlagBits::eNone,
                                                  nullptr, vk::ImageLayout::eUndefined, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, colorClear);
        }

        std::optional<vk::RenderingAttachmentInfo> depth_attachment_info;
        if (m_depthAttachmentTexture) {
            assert(m_depthAttachmentTexture->aspectMask | vk::ImageAspectFlagBits::eDepth);
            depth_attachment_info = vk::RenderingAttachmentInfo(m_depthAttachmentTexture->view, vk::ImageLayout::eDepthAttachmentOptimal, vk::ResolveModeFlagBits::eNone, nullptr,
                                                                vk::ImageLayout::eUndefined, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, depthClear);
        }

        auto renderArea = vk::Rect2D{vk::Offset2D{}, getCtx()->getWsi()->getScreenExtent()};
        auto renderInfo = vk::RenderingInfo(vk::RenderingFlags(), renderArea, 1, 0, renderingAttachmentInfos.size(), renderingAttachmentInfos.data(), depth_attachment_info.has_value() ? &depth_attachment_info.value() : nullptr, nullptr);
        commandBuffer.beginRendering(renderInfo);

        // set viewport and scissor (remains the same for all renderings)
        auto extent = getCtx()->getWsi()->getScreenExtent();
        vk::Viewport viewport;
        viewport.x = viewport.y = 0;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0;
        viewport.maxDepth = 1;
        commandBuffer.setViewport(0, 1, &viewport);
        vk::Rect2D scissor;
        scissor.offset.x = scissor.offset.y = 0;
        scissor.extent.width = static_cast<float>(viewport.width);
        scissor.extent.height = static_cast<float>(viewport.height);
        commandBuffer.setScissor(0, 1, &scissor);

        draw(commandBuffer);
        commandBuffer.endRendering();

        commandBuffer.end();

        auto submission = getCtx()->sync->submit(commandBuffer, m_queueFamilyIndex, awaitBeforeExecution, vk::PipelineStageFlagBits::eAllCommands, awaitBinaryAwaitableList, signalBinarySemaphore);
        return submission;
    }

    // Attachments
    void setColorAttachment(const std::string &name, std::shared_ptr<Texture> color) {
        assert(m_graphicsPassConfig.colorAttachmentFormats.contains(name) && "Color attachment was not specified before or does not exist!");
        assert(color->format == m_graphicsPassConfig.colorAttachmentFormats.at(name) && "Color attachment texture format must match the format passed to the constructor configuration!");

        size_t location = ::vvv::reflectColorAttachmentLocation(getCtx(), name, m_shaders.at(1));
        m_colorAttachmentTextures.resize(std::max(m_colorAttachmentTextures.size(), location + 1), nullptr);
        m_colorAttachmentTextures[location] = std::move(color);
    }

    void setDepthAttachment(std::shared_ptr<Texture> depth) {
        assert(depth->format == m_graphicsPassConfig.depthAttachmentFormat && "Depth attachment texture format must match the format passed to the constructor!");
        m_depthAttachmentTexture = std::move(depth);
    }

    [[nodiscard]] std::shared_ptr<Texture> reflectColorAttachment(vk::ArrayProxy<const std::string> names, TextureReflectionOptions opts) const {
        if (opts.format.has_value()) {
            for (const auto &n : names) {
                if (m_graphicsPassConfig.colorAttachmentFormats.contains(n) && m_graphicsPassConfig.colorAttachmentFormats.at(n) != opts.format.value())
                    Logger(Warn) << "Color attachment format " << to_string(opts.format.value()) << " does not equal format " << to_string(m_graphicsPassConfig.colorAttachmentFormats.at(n)) << " for "
                                 << n << " from creation time!";
            }
        }
        return ::vvv::reflectColorAttachment(getCtx(), getShaders(), names, std::move(opts));
    }
    std::shared_ptr<Texture> reflectColorAttachment(const char *name, TextureReflectionOptions opts) const { return reflectColorAttachment(std::string(name), std::move(opts)); }

    std::shared_ptr<Texture> createDepthStencilAttachment(TextureReflectionOptions opts) {
        assert(m_graphicsPassConfig.depthAttachmentFormat.has_value() && "You must set depthAttachmentFormat to a depth texture format to enable depth buffering for this pass!");

        if (opts.format.has_value() && opts.format.value() != vk::Format::eUndefined && m_graphicsPassConfig.depthAttachmentFormat.value() != opts.format.value()) {
            Logger(Warn) << "Queried depth texture format " << to_string(opts.format.value()) << " differs from render pass attachment format " << to_string(m_graphicsPassConfig.depthAttachmentFormat.value())
                         << "! Returning texture with " << to_string(m_graphicsPassConfig.depthAttachmentFormat.value());
        }

        return std::make_shared<Texture::depthAttachment>(getCtx(), opts.width, opts.height, m_graphicsPassConfig.depthAttachmentFormat.value(),
                                                          opts.usage | vk::ImageUsageFlagBits::eDepthStencilAttachment, opts.queues);
    }

  protected:
    SinglePassGraphics(GpuContextPtr ctx, std::string label, GraphicsPassConfig config, const std::shared_ptr<MultiBuffering> &multiBuffering = NoMultiBuffering, uint32_t queueFamilyIndex = 0)
        : PassBase(ctx, std::move(label), multiBuffering, queueFamilyIndex), m_graphicsPassConfig(std::move(config)) {}

    /// Must be implemented by subclass and is called between commandBuffer.beginRendering(renderInfo) and commandBuffer.endRendering().
    virtual void draw(vk::CommandBuffer &commandBuffer) = 0;

    std::vector<std::shared_ptr<Shader>> createShaders() override {
        auto shaders = std::vector<std::shared_ptr<Shader>>();
        shaders.push_back(std::make_shared<Shader>(SimpleGlslShaderRequest({.filename = m_graphicsPassConfig.vertexShaderName, .label = m_label + ".shaders.0"})));
        shaders.push_back(std::make_shared<Shader>(SimpleGlslShaderRequest({.filename = m_graphicsPassConfig.fragmentShaderName, .label = m_label + ".shaders.1"})));
        assert(m_graphicsPassConfig.geometryShaderName.empty() && "Geometry stage not yet supported in graphics pass. Maybe you can implement it?");
        if (!m_graphicsPassConfig.geometryShaderName.empty())
            shaders.push_back(std::make_shared<Shader>(Shader({.filename = m_graphicsPassConfig.geometryShaderName, .label = m_label + ".shaders.2"})));
        return shaders;
    }
    std::shared_ptr<Shader> getVertexShader() { return m_shaders.at(0); }
    std::shared_ptr<Shader> getFragmentShader() { return m_shaders.at(1); }
    std::shared_ptr<Shader> getGeometryShader() { return m_shaders.size() > 1 ? m_shaders.at(2) : nullptr; }

    /// Has to fill out the VertexInputBindingDescription and VertexAttributeDescription vectors by reference.
    virtual void createVertexInputDescriptions(std::vector<vk::VertexInputBindingDescription> &vertexBindingDescriptions, std::vector<vk::VertexInputAttributeDescription> &vertexAttributeDescriptions) = 0;

    std::vector<vk::Pipeline> createPipelines() override {
        assert(!isPipelineCreated());
        assert(m_shaders.size() > 1);

        const auto device = getCtx()->getDevice();
        const auto debug = getCtx()->debugMarker;

        // create pipeline stage infos ------------------------------------------------------------------------------------------------------------------------------------------------------------------
        std::vector<vk::VertexInputBindingDescription> vertexBindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> vertexAttributeDescriptions;
        createVertexInputDescriptions(vertexBindingDescriptions, vertexAttributeDescriptions);

        // shader stages
        std::array<vk::PipelineShaderStageCreateInfo, 2> pipelineShaderStageCreateInfos = {
            *m_shaders[0]->pipelineShaderStageCreateInfo(getCtx()),
            *m_shaders[1]->pipelineShaderStageCreateInfo(getCtx()),
        };

        // Create pipeline from fixed function stages. Viewport will be set dynamically.
        vk::PipelineVertexInputStateCreateInfo vertexInputState = {};
        vertexInputState.vertexBindingDescriptionCount = vertexBindingDescriptions.size();
        vertexInputState.vertexAttributeDescriptionCount = vertexAttributeDescriptions.size();
        vertexInputState.pVertexBindingDescriptions = vertexBindingDescriptions.empty() ? nullptr : vertexBindingDescriptions.data();
        vertexInputState.pVertexAttributeDescriptions = vertexAttributeDescriptions.empty() ? nullptr : vertexAttributeDescriptions.data();

        vk::PipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo(vk::PipelineInputAssemblyStateCreateFlags(), m_graphicsPassConfig.primitiveTopology, false);
        vk::PipelineViewportStateCreateInfo pipelineViewportStateCreateInfo(vk::PipelineViewportStateCreateFlags(), 1, nullptr, 1, nullptr);
        vk::PipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo(vk::PipelineRasterizationStateCreateFlags(), false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack,
                                                                                      vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f);
        vk::PipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo({}, vk::SampleCountFlagBits::e1);
        vk::StencilOpState stencilOpState(vk::StencilOp::eKeep, vk::StencilOp::eKeep, vk::StencilOp::eKeep, vk::CompareOp::eAlways);
        vk::PipelineDepthStencilStateCreateInfo pipelineDepthStencilStateCreateInfo(vk::PipelineDepthStencilStateCreateFlags(), m_graphicsPassConfig.depthAttachmentFormat.has_value(),
                                                                                    m_graphicsPassConfig.depthAttachmentFormat.has_value(), vk::CompareOp::eLessOrEqual, false, false,
                                                                                    stencilOpState, stencilOpState);
        vk::ColorComponentFlags colorComponentFlags(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
        assert(!m_graphicsPassConfig.alphaBlending); // not implemented yet
        std::vector<vk::PipelineColorBlendAttachmentState> pipelineColorBlendAttachmentStates{};
        std::array<vk::DynamicState, 3> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor, vk::DynamicState::eLineWidth};
        vk::PipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo(vk::PipelineDynamicStateCreateFlags(), dynamicStates);

        // dynamic rendering (we don't use a render pass) passed to GraphicsPipeLineCreateInfo in pNext ------------------------------------------------------------------------------------------------
        std::vector<vk::Format> colorAttachmentFormats = {};
        std::vector<std::pair<std::string, vk::Format>> shaderColorAttachmentFormats = ::vvv::reflectColorAttachmentInfo(getCtx(), m_shaders.at(1));
        for (int i = 0; i < shaderColorAttachmentFormats.size(); i++) {
            const auto &name = shaderColorAttachmentFormats[i].first;
            // if no format was specified for the output attachment at all, we use the refleted format
            if (!m_graphicsPassConfig.colorAttachmentFormats.contains(name)) {
                Logger(Warn) << "No format was specified for color attachment " << name << "! Using reflected format " << to_string(shaderColorAttachmentFormats[i].second);
                m_graphicsPassConfig.colorAttachmentFormats[name] = shaderColorAttachmentFormats[i].second;
            }
            // if the format was specified as undefined, we also use the reflected format
            else if (m_graphicsPassConfig.colorAttachmentFormats[name] == vk::Format::eUndefined) {
                m_graphicsPassConfig.colorAttachmentFormats[name] = shaderColorAttachmentFormats[i].second;
            }
            // we add the formats to this vector to ensure that they are in the correct order (as read from the reflection)
            colorAttachmentFormats.push_back(m_graphicsPassConfig.colorAttachmentFormats[name]);

            // each color attachment needs its own ColorBlendState (standard: pColorBlendState->attachmentCount must be equal to VkPipelineRenderingCreateInfo::colorAttachmentCount)
            pipelineColorBlendAttachmentStates.emplace_back(false, vk::BlendFactor::eZero, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::BlendFactor::eZero, vk::BlendFactor::eZero, vk::BlendOp::eAdd, colorComponentFlags);
        }

        vk::PipelineColorBlendStateCreateInfo pipelineColorBlendStateCreateInfo(vk::PipelineColorBlendStateCreateFlags(), false, vk::LogicOp::eNoOp, pipelineColorBlendAttachmentStates,
                                                                                {{1.0f, 1.0f, 1.0f, 1.0f}});
        vk::PipelineRenderingCreateInfo pipeline_create;
        if (m_graphicsPassConfig.depthAttachmentFormat.has_value()) {
            pipeline_create = vk::PipelineRenderingCreateInfo({}, colorAttachmentFormats.size(), colorAttachmentFormats.data(), m_graphicsPassConfig.depthAttachmentFormat.value(),
                                                              vk::Format::eUndefined);
        } else {
            pipeline_create = vk::PipelineRenderingCreateInfo({}, colorAttachmentFormats.size(), colorAttachmentFormats.data(), vk::Format::eUndefined,
                                                              vk::Format::eUndefined);
        }

        // create pipeline -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
        vk::GraphicsPipelineCreateInfo graphicsPipelineCreateInfo(vk::PipelineCreateFlags(), pipelineShaderStageCreateInfos, &vertexInputState, &pipelineInputAssemblyStateCreateInfo, nullptr,
                                                                  &pipelineViewportStateCreateInfo, &pipelineRasterizationStateCreateInfo, &pipelineMultisampleStateCreateInfo,
                                                                  &pipelineDepthStencilStateCreateInfo, &pipelineColorBlendStateCreateInfo, &pipelineDynamicStateCreateInfo, m_pipelineLayout,
                                                                  nullptr, 0, nullptr, 0);
        graphicsPipelineCreateInfo.setPNext(&pipeline_create);

        const auto [pipelineResult, pipeline] = device.createGraphicsPipeline(getCtx()->getPipelineCache(), graphicsPipelineCreateInfo);

        switch (pipelineResult) {
        case vk::Result::eSuccess:
            break;
        default:
            throw std::runtime_error("failed to create graphics pipeline");
        }

        return {pipeline};
    }

    GraphicsPassConfig m_graphicsPassConfig;
    std::vector<std::shared_ptr<Texture>> m_colorAttachmentTextures = {};
    std::shared_ptr<Texture> m_depthAttachmentTexture = {};
};

class SinglePassFullscreenGraphics : public SinglePassGraphics {
  public:
    SinglePassFullscreenGraphics(GpuContextPtr ctx, const std::shared_ptr<MultiBuffering> &multiBuffering, std::string fragmentShaderName, std::unordered_map<std::string, vk::Format> colorAttachmentFormats = {}, std::string label = "SinglePassFullscreenGraphics")
        : WithGpuContext(ctx), WithMultiBuffering(multiBuffering), SinglePassGraphics(ctx, std::move(label), {.colorAttachmentFormats = std::move(colorAttachmentFormats), .fragmentShaderName = std::move(fragmentShaderName)},
                                                                                      multiBuffering, ctx->getQueueFamilyIndices().graphics.value()) {}

    void createVertexInputDescriptions(std::vector<vk::VertexInputBindingDescription> &vertexBindingDescriptions, std::vector<vk::VertexInputAttributeDescription> &vertexAttributeDescriptions) override {
        // we have no vertex input, so we leave it empty
    }

  protected:
    void draw(vk::CommandBuffer &commandBuffer) override {
        // the default vertex shader "fullscreen_triangle.vert" draws a fullscreen triangle from three implicit vertices
        commandBuffer.draw(3, 1, 0, 0);
    }
};

} // namespace vvv