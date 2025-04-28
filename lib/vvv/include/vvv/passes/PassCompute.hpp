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

#include <vvv/reflection/TextureReflection.hpp>
#include <vvv/reflection/UniformReflection.hpp>

#include <functional>

namespace vvv {

/// This is a typesafe wrapper around the non-typesafe but more flexible `PassComputeDynamic`
// template <PassComputeStructure Types> class PassCompute : public virtual MultiBuffering, public virtual WithGpuContext /*: PassComputeDynamic */ {
//     using UniformSets = decltype(Types::uniformSets)::type;
//     using StorageImages = decltype(Types::storageImages)::type;
//     using ImageSamplers = decltype(Types::imageSamplers)::type;
//     using StorageBuffers = decltype(Types::storageBuffers)::type;
class PassCompute : public PassBase {
  public:
    //  a pass is either `TimelineSemaphoreWaitable execute(queue)` or `executeCommands(vk::CommandBuffer commandBuffer)`.
    // The first submits to the queue itself and is required for multipass or multiqueue algorithms,
    // the second variant just writes into a command buffer and the caller is responsible for submitting the work. This
    // can be more efficient since the number of submits can be reduced.
    //
    // A `vk::CommandBuffer executeCommands()` variant that returns a secondary commandbuffer without an argument could be more ergonomic and efficient, but
    // harder to synchronize correctly. Not sure...

    [[nodiscard]] AwaitableHandle execute(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr) override = 0;

  protected:
    PassCompute(GpuContextPtr ctx, std::string label, const std::shared_ptr<MultiBuffering> &multiBuffering = NoMultiBuffering, uint32_t queueFamilyIndex = 0)
        : WithGpuContext(ctx), WithMultiBuffering(multiBuffering), PassBase(ctx, label, multiBuffering, queueFamilyIndex) {}

    std::vector<vk::Pipeline> createPipelines() override {
        const auto device = getCtx()->getDevice();
        const auto debug = getCtx()->debugMarker;

        assert(!isPipelineCreated());

        std::vector<vk::ComputePipelineCreateInfo> computePipelineCreateInfos = {};

        for (const auto &shader : m_shaders) {
            vk::ComputePipelineCreateInfo computePipelineCreateInfo({}, *shader->pipelineShaderStageCreateInfo(getCtx()), m_pipelineLayout);
            computePipelineCreateInfos.push_back(computePipelineCreateInfo);
        }

        const auto [pipelineResult, pipelines] = device.createComputePipelines(getCtx()->getPipelineCache(), computePipelineCreateInfos);

        switch (pipelineResult) {
        case vk::Result::eSuccess:
            break;
        default:
            throw std::runtime_error("failed to create compute pipeline");
        }

        return pipelines;
    }
};

struct SinglePassComputeSettings {
    GpuContextPtr ctx;
    std::string label;
    std::shared_ptr<MultiBuffering> multiBuffering = NoMultiBuffering;
    uint32_t queueFamilyIndex = 0;
    vk::Extent3D workgroupCount = {1, 1, 1};
};

/// A special variant of a compute pass that can execute in a single submission to the GPU. This is the case if the algorithm does
/// not rely on multiple passes or multiple queues.
class SinglePassCompute : public PassCompute {
  public:
    template <typename... ShaderArgs>
    explicit SinglePassCompute(SinglePassComputeSettings settings, ShaderArgs &&...shaderArgs)
        : WithGpuContext(settings.ctx), WithMultiBuffering(settings.multiBuffering), PassCompute(settings.ctx, settings.label, settings.multiBuffering, settings.queueFamilyIndex),
          m_workgroupCount(settings.workgroupCount) {

        setShaderArgs(shaderArgs...);
    }

    template <typename... ShaderArgs>
    void setShaderArgs(ShaderArgs &&...shaderArgs) {
        // store vvv::Shader() constructor args for use in createShaders()
        m_shaderContructor = [this, shaderArgs...]() -> std::shared_ptr<Shader> {
            auto shader = std::make_shared<Shader>(shaderArgs...);
            shader->label = m_label;
            return shader;
        };
    }

    void freeResources() override {
        m_shader = nullptr;

        PassCompute::freeResources();
    }

    [[nodiscard]] AwaitableHandle execute(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr) override {
        assert(isPipelineCreated() && "you MUST call 'allocateResources' if the pass was created with lazy state initialization.");
        // updateUniformBufferMemory(getActiveIndex());

        auto &commandBuffer = m_commandBuffer->getActive();
        executeCommands(commandBuffer);

        auto submission = getCtx()->sync->submit(commandBuffer, m_queueFamilyIndex, awaitBeforeExecution, vk::PipelineStageFlagBits::eAllCommands, awaitBinaryAwaitableList, signalBinarySemaphore);

        return submission;
    }

    void executeCommands(vk::CommandBuffer commandBuffer) {
        commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_pipelines[0]); // each compute shader has one pipeline, we only have one shader, so only one pipeline
        if (hasDescriptors()) {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_pipelineLayout, 0, m_descriptorSets->getActive(), nullptr);
        }
        commandBuffer.dispatch(m_workgroupCount.width, m_workgroupCount.height, m_workgroupCount.depth);
        commandBuffer.end();
    }

    void setWorkgroupCount(uint32_t width, uint32_t height = 1, uint32_t depth = 1) { m_workgroupCount = vk::Extent3D{width, height, depth}; }

    void setGlobalInvocationSize(vk::Extent3D domain) { m_workgroupCount = getDispatchSize(domain.width, domain.height, domain.depth, m_shader->reflectWorkgroupSize()); }
    void setGlobalInvocationSize(vk::Extent2D domain) { m_workgroupCount = getDispatchSize(domain.width, domain.height, 1u, m_shader->reflectWorkgroupSize()); }
    void setGlobalInvocationSize(uint32_t width, uint32_t height = 1, uint32_t depth = 1) { m_workgroupCount = getDispatchSize(width, height, depth, m_shader->reflectWorkgroupSize()); }

    // setSize(vk::Extent2D domain, vk::Extent2D workgroupSize);
    // setSize(vk::Extent1D domain, vk::Extent1D workgroupSize);
    // setWorkgroupSize(vk::Extent3D);
    // setWorkgroupSize(vk::Extent2D);
    //  etc

  protected:
    std::vector<std::shared_ptr<Shader>> createShaders() override {
        m_shader = m_shaderContructor();
        return {m_shader};
    }

    std::shared_ptr<Shader> m_shader;
    vk::Extent3D m_workgroupCount;

  private:
    // this lambda is used as a type-erased container to store the vvv::Shader constructor arguments.
    // It is used in createShaders().
    std::function<std::shared_ptr<Shader>()> m_shaderContructor;
};
}; // namespace vvv
