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

#include <vulkan/vulkan.hpp>
#include "Volume.hpp"
#include <vvv/util/util.hpp>

namespace vvv {

struct VolumeHistogramOptions {
    uint32_t countScalarValueBuckets;
    uint32_t countGradientBuckets;
    glm::vec2 gradientLimits;
    glm::vec2 valueLimits;
};

const glm::vec2 AutomaticLimits = glm::vec2(0.0);

const VolumeHistogramOptions DefaultVolumeHistogramOptions{
    .countScalarValueBuckets = 512,
    .countGradientBuckets = 512,
    .gradientLimits = AutomaticLimits,
    .valueLimits = AutomaticLimits,
};

// struct ComputePassOptions {
//     /** increases allocations by multibuffering for swapchain support. You can create multiple independent compute passes as an alternative. */
//     size_t maxInstanceCount = 1;
// };
//
// struct ComputePass {
//     inputTextures // can be added independently, appended to vector internally
//     outputTextures // can be added independently, appended to vector internally
//     uniforms // all in a single set
//     createInstance();
// };

struct Uniform_VolumeHistogram {
    glm::vec2 valueLimits;
    glm::vec2 gradientLimits;
};

class VolumeHistogram {
public:
    VolumeHistogram(GpuContextPtr ctx, std::shared_ptr<Volume<>> volume, VolumeHistogramOptions histogramOptions = DefaultVolumeHistogramOptions)
        : m_ctx(ctx), m_volume(volume), m_options(histogramOptions) {
        const auto tableSize = getHistogramSize();
        m_histogram =
            std::make_unique<Texture>(ctx, vk::Format::eR32Uint, tableSize.width, tableSize.height,
                                      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst, // TransferDst is required for vkCmdClearColorImage
                                      TextureExclusiveQueueUsage);
    }

    std::shared_ptr<Texture> texture() const { return m_histogram; }

    /**
     * Prepare resources for the computation. This may include uploading data, executing layout transitions, etc.
     *
     * @param performComputeCommands
     */
    void prepare(const std::function<void(const std::function<void(vk::CommandBuffer)>)> performComputeCommands) {
        if (!m_histogram->areResourcesInitialized()) {
            m_histogram->initResources();
            m_histogram->setName("VolumeHistogram.m_histogram");
        }

        if (!isPipelineCreated()) {
            createPipeline();
            instantiatePipeline();
        }

        performComputeCommands([this](vk::CommandBuffer commandBuffer) { m_histogram->setImageLayout(commandBuffer, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eAllCommands); });
        m_histogram->setUploaded(true);
        updateUniformDescriptorSet();
        writePipelineDescriptorSet();
    }

    void executeCommands(vk::CommandBuffer commandBuffer) {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_pipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_pipelineLayout, 0, m_descriptorSet, nullptr);

        const std::array<uint32_t, 4> zeros = {0, 0, 0, 0};
        const vk::ClearColorValue clearToZero(zeros);
        const std::vector<vk::ImageSubresourceRange> clearSubresourceRange{{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        commandBuffer.clearColorImage(m_histogram->image, m_histogram->descriptor.imageLayout, clearToZero, clearSubresourceRange);

        m_histogram->setImageLayout(commandBuffer, m_histogram->descriptor.imageLayout, vk::PipelineStageFlagBits::eAllCommands);

        const vk::Extent3D workgroupSize(8, 8, 8); // TODO(Reiner): reflect workgroup size from shader
        const vk::Extent3D volumeSize(m_volume->getTexture(m_ctx)->width, m_volume->getTexture(m_ctx)->height, m_volume->getTexture(m_ctx)->depth);
        const auto dispatchSize = getDispatchSize(volumeSize, workgroupSize);
        std::cout << std::to_string(m_volume->getTexture(m_ctx)->width) << "x" << std::to_string(m_volume->getTexture(m_ctx)->height) << "x" << std::to_string(m_volume->getTexture(m_ctx)->depth)
                  << std::endl;
        std::cout << std::to_string(dispatchSize.width) << "x" << std::to_string(dispatchSize.height) << "x" << std::to_string(dispatchSize.depth) << std::endl;
        commandBuffer.dispatch(dispatchSize.width, dispatchSize.height, dispatchSize.depth);

        m_histogram->setImageLayout(commandBuffer, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eComputeShader);
    }

    ~VolumeHistogram() {
        const auto device = m_ctx->getDevice();

        VK_DEVICE_FREE(device, m_descriptorPool, m_descriptorSet)
        VK_DEVICE_DESTROY(device, m_descriptorPool)

        VK_DEVICE_DESTROY(device, m_uniformBuffer)
        VK_DEVICE_FREE_MEMORY(device, m_uniformBufferMemory)

        VK_DEVICE_DESTROY(device, m_pipeline)
        VK_DEVICE_DESTROY(device, m_descriptorSetLayout)

        VK_DEVICE_DESTROY(device, m_pipeline);
        VK_DEVICE_DESTROY(device, m_pipelineLayout);
        VK_DEVICE_DESTROY(device, m_descriptorSetLayout);

        m_shader->destroyModule(device);
        m_shader.reset();
        m_histogram.reset();
    }

    glm::vec2 getGradientLimits() {
        // TODO(Reiner): would be easy to derive something useful if a volume would carry min and max value
        return m_options.gradientLimits == AutomaticLimits ? glm::vec2(0, std::numeric_limits<uint16_t>::max()) : glm::vec2(m_options.gradientLimits);
    }

    glm::vec2 getScalarValueLimits() {
        // TODO(Reiner): would be easy to derive something useful if a volume would carry min and max value
        return m_options.valueLimits == AutomaticLimits ? glm::vec2(0, std::numeric_limits<uint16_t>::max()) : glm::vec2(m_options.valueLimits);
    }

private:
    vk::Extent2D getHistogramSize() { return vk::Extent2D(m_options.countScalarValueBuckets, m_options.countGradientBuckets); }

    void updateUniformDescriptorSet() {
        Uniform_VolumeHistogram ubo{.valueLimits = getScalarValueLimits(), .gradientLimits = getGradientLimits()};

        const auto device = m_ctx->getDevice();

        void *data = device.mapMemory(m_uniformBufferMemory, 0, sizeof(ubo), {});
        memcpy(data, &ubo, sizeof(ubo));
        device.unmapMemory(m_uniformBufferMemory);
    }

    bool isPipelineCreated() {
        // TODO(Reiner): it would be enough to create this pipeline once. It can be shared by all transfer functions
        return m_pipeline != static_cast<vk::Pipeline>(nullptr);
    }

    void createPipeline() {
        assert(!isPipelineCreated());

        const auto device = m_ctx->getDevice();
        const auto debug = m_ctx->debugMarker;

        const auto descriptorSetLayoutBindings = {
            // Binding 0: Input for original TF (read-only)
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute),
            // Binding 1: Output for preintegrated TF (write)
            vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute),
            // Binding 2: Uniform
            vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute),
        };

        vk::DescriptorSetLayoutCreateInfo descSetLayoutCreateInfo({}, descriptorSetLayoutBindings);
        m_descriptorSetLayout = device.createDescriptorSetLayout(descSetLayoutCreateInfo);
        debug->setName(m_descriptorSetLayout, "VolumeHistogram.m_descriptorSetLayout");

        const std::vector descSetLayouts = {m_descriptorSetLayout};
        vk::PipelineLayoutCreateInfo pipeInfo({}, descSetLayouts);
        m_pipelineLayout = device.createPipelineLayout(pipeInfo);
        debug->setName(m_pipelineLayout, "VolumeHistogram.m_pipelineLayout");

        m_shader = std::make_unique<Shader>(SimpleShaderRequest{.filename = ShaderName, .label = "VolumeHistogram.m_shader"});

        vk::ComputePipelineCreateInfo computePipelineCreateInfo({}, *m_shader->pipelineShaderStageCreateInfo(m_ctx), m_pipelineLayout);
        const auto [pipelineResult, pipeline] = device.createComputePipeline(nullptr, computePipelineCreateInfo);

        switch (pipelineResult) {
        case vk::Result::eSuccess:
            break;
        default:
            throw std::runtime_error("failed to create compute pipeline");
        }

        m_pipeline = pipeline;
        debug->setName(pipeline, "VolumeHistogram.m_pipeline");
    }

    void instantiatePipeline() {
        const auto device = m_ctx->getDevice();
        const auto debug = m_ctx->debugMarker;

        // create the descriptor memory
        VkDeviceSize bufferSize = sizeof(Uniform_VolumeHistogram);

        const auto [buffer, mem] = createBuffer(*m_ctx, bufferSize, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        m_uniformBuffer = buffer;
        m_uniformBufferMemory = mem;
        debug->setName(m_uniformBuffer, "VolumeHistogram.m_uniformBuffer");
        debug->setName(m_uniformBufferMemory, "VolumeHistogram.m_uniformBufferMemory");

        // create the descriptor slots
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1),
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 1),
            vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1),
        };

        m_descriptorPool = device.createDescriptorPool(vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, poolSizes));
        debug->setName(m_descriptorPool, "VolumeHistogram.m_descriptorPool");

        const std::vector<vk::DescriptorSetLayout> descriptorSetLayouts(1, m_descriptorSetLayout);
        vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo(m_descriptorPool, descriptorSetLayouts);
        m_descriptorSet = device.allocateDescriptorSets(descriptorSetAllocateInfo).front();
        debug->setName(m_descriptorSet, "VolumeHistogram.m_descriptorSet");
    }

    void writePipelineDescriptorSet() {
        // associate the descriptor memory with the descriptor slots
        const std::vector<vk::DescriptorBufferInfo> uniformBufferInfo{{m_uniformBuffer, 0, sizeof(Uniform_Preintegration)}};

        std::vector<vk::WriteDescriptorSet> computeWriteDescriptorSets = {
            vk::WriteDescriptorSet(m_descriptorSet, 0, 0, vk::DescriptorType::eCombinedImageSampler, m_volume->getTexture(m_ctx)->descriptor),
            vk::WriteDescriptorSet(m_descriptorSet, 1, 0, vk::DescriptorType::eStorageImage, m_histogram->descriptor),
            vk::WriteDescriptorSet(m_descriptorSet, 2, 0, vk::DescriptorType::eUniformBuffer, {}, uniformBufferInfo)};

        m_ctx->getDevice().updateDescriptorSets(computeWriteDescriptorSets, {});
    }

    const std::string ShaderName = "volume_histogram_2d.comp";

    std::shared_ptr<Volume<uint16_t>> m_volume;
    std::shared_ptr<Texture> m_histogram;
    GpuContextPtr m_ctx;

    VolumeHistogramOptions m_options = DefaultVolumeHistogramOptions;

    // structure of compute pass
    std::unique_ptr<Shader> m_shader = nullptr;
    vk::Pipeline m_pipeline = nullptr;
    vk::PipelineLayout m_pipelineLayout = nullptr;
    vk::DescriptorSetLayout m_descriptorSetLayout = nullptr;
    vk::DescriptorSet m_descriptorSet = nullptr;

    // instantiation of compute pass (resources that cannot be touched during execution)
    vk::Buffer m_uniformBuffer;
    vk::DeviceMemory m_uniformBufferMemory;
    vk::DescriptorPool m_descriptorPool;
};

}