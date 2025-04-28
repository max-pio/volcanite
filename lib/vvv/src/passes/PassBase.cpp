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

#include <vvv/passes/PassBase.hpp>

using namespace vvv;

void vvv::PassBase::allocateResources() {
    const auto debug = getCtx()->debugMarker;

    if (isPipelineCreated()) {
        return;
    }

    m_shaders = createShaders(); // abstract method, subclassed
    createPipelineLayout();
    m_pipelines = createPipelines(); // abstract method, subclassed
    for (size_t idx = 0; idx < m_pipelines.size(); idx++) {
        debug->setName(m_pipelines[idx], m_label + ".m_pipelines." + std::to_string(idx));
    }

    createCommandBuffers();
    // Note: if we ever device to allocate uniform buffers within a pass, this would be the place to do it:
    // instantiatePipeline();
}

void vvv::PassBase::freeResources() {
    // release all vulkan resources (including shaders and pipelines that were created by base class methods)
    for (auto &shader : m_shaders) {
        shader->destroyModule(device());
    }
    m_shaders.clear();

    m_descriptorSetNumberToIdx.clear();
    m_descriptorSetWrites.clear();
    m_isDirty.clear();

    VK_DEVICE_DESTROY_ALL(device(), m_pipelines)
    VK_DEVICE_DESTROY(device(), m_pipelineLayout)
    VK_DEVICE_DESTROY_ALL(device(), m_descriptorSetLayouts)
    for (auto &cmd : *m_commandBuffer) {
        VK_DEVICE_FREE(device(), m_commandPool, cmd)
    }
    VK_DEVICE_DESTROY(device(), m_commandPool)
    for (auto &sets : *m_descriptorSets) {
        VK_DEVICE_FREE_ALL(device(), m_descriptorPool, sets)
    }
    m_descriptorSets = {};
    VK_DEVICE_DESTROY(device(), m_descriptorPool)

    m_descriptorSetLayouts = {};
    m_pipelines = {};
}

DescriptorBinding PassBase::findDescriptorBindingByName(const std::string &name) {
    for (auto const &shader : m_shaders) {
        auto binding = shader->reflectBindingByName(name);
        if (binding) {
            return binding.value();
        }
    }

    Logger(Error) << "unknown binding '" + name + "' in pass '" + m_label + "'";
    throw std::runtime_error("unknown binding '" + name + "' in pass '" + m_label + "'");
}

void PassBase::setImageSampler(uint32_t setIdx, uint32_t bindingIdx, Texture &texture, vk::ImageLayout layout, bool atActiveIndex) {
    updateDescriptorSetsImage(setIdx, bindingIdx, texture, vk::DescriptorType::eCombinedImageSampler, layout, atActiveIndex);
}

void PassBase::setImageSamplerArray(uint32_t setIdx, uint32_t bindingIdx, uint32_t arrayElement, Texture &texture, vk::ImageLayout layout, bool atActiveIndex) {
    updateDescriptorSetsImageArray(setIdx, bindingIdx, arrayElement, texture, vk::DescriptorType::eCombinedImageSampler, layout, atActiveIndex);
}

void PassBase::setImageSampler(const std::string &name, Texture &texture, vk::ImageLayout layout, bool atActiveIndex) {
    auto descriptor = findDescriptorBindingByName(name);
    setImageSampler(descriptor.set_number, descriptor.binding.binding, texture, layout, atActiveIndex);
}

void PassBase::setImageSamplerArray(const std::string &name, uint32_t arrayElement, Texture &texture, vk::ImageLayout layout, bool atActiveIndex) {
    auto descriptor = findDescriptorBindingByName(name);
    setImageSamplerArray(descriptor.set_number, descriptor.binding.binding, arrayElement, texture, layout, atActiveIndex);
}

void PassBase::setImageSampler(uint32_t setIdx, uint32_t bindingIdx, MultiBufferedResource<std::shared_ptr<Texture>> &textures, vk::ImageLayout layout) {
    updateDescriptorSetsImage(setIdx, bindingIdx, textures, vk::DescriptorType::eCombinedImageSampler, layout);
}

void PassBase::setImageSampler(const std::string &name, MultiBufferedResource<std::shared_ptr<Texture>> &textures, vk::ImageLayout layout) {
    auto descriptor = findDescriptorBindingByName(name);
    setImageSampler(descriptor.set_number, descriptor.binding.binding, textures, layout);
}

void PassBase::setStorageImage(uint32_t setIdx, uint32_t bindingIdx, Texture &texture, vk::ImageLayout layout, bool atActiveIndex) {
    updateDescriptorSetsImage(setIdx, bindingIdx, texture, vk::DescriptorType::eStorageImage, layout, atActiveIndex);
}

void PassBase::setStorageImageArray(uint32_t setIdx, uint32_t bindingIdx, uint32_t arrayElement, Texture &texture, vk::ImageLayout layout, bool atActiveIndex) {
    updateDescriptorSetsImageArray(setIdx, bindingIdx, arrayElement, texture, vk::DescriptorType::eStorageImage, layout, atActiveIndex);
}

void PassBase::setStorageImage(const std::string &name, Texture &texture, vk::ImageLayout layout, bool atActiveIndex) {
    auto descriptor = findDescriptorBindingByName(name);
    assert(descriptor.binding.descriptorCount == 1 && "you should use the setStorageImageArray(.., arrayElement, ..) method to set the image array element");
    setStorageImage(descriptor.set_number, descriptor.binding.binding, texture, layout, atActiveIndex);
}

void PassBase::setStorageImageArray(const std::string &name, uint32_t arrayElement, Texture &texture, vk::ImageLayout layout, bool atActiveIndex) {
    auto descriptor = findDescriptorBindingByName(name);
    setStorageImageArray(descriptor.set_number, descriptor.binding.binding, arrayElement, texture, layout, atActiveIndex);
}

void PassBase::setStorageImage(uint32_t setIdx, uint32_t bindingIdx, MultiBufferedResource<std::shared_ptr<Texture>> &textures, vk::ImageLayout layout) {
    updateDescriptorSetsImage(setIdx, bindingIdx, textures, vk::DescriptorType::eStorageImage, layout);
}

void PassBase::setStorageImage(const std::string &name, MultiBufferedResource<std::shared_ptr<Texture>> &textures, vk::ImageLayout layout) {
    auto descriptor = findDescriptorBindingByName(name);
    setStorageImage(descriptor.set_number, descriptor.binding.binding, textures, layout);
}

void PassBase::updateDescriptorSetsImage(uint32_t setIdx, uint32_t bindingIdx, Texture &texture, vk::DescriptorType descriptorType, vk::ImageLayout layout, bool atActiveIndex) {
    updateDescriptorSetsImageArray(setIdx, bindingIdx, 0u, texture, descriptorType, layout, atActiveIndex);
}

void PassBase::updateDescriptorSetsImageArray(uint32_t setIdx, uint32_t bindingIdx, uint32_t arrayElement, Texture &texture, vk::DescriptorType descriptorType, vk::ImageLayout layout, bool atActiveIndex) {
    const auto descriptorCount = 1;

    assert(texture.areResourcesInitialized());

    // if atActiveIndex == true:
    //  Because this resource is updated every frame,
    //  there may be several descriptor sets of the other frames in flight that are still processed by a command buffer,
    //  so we are not allowed to change them!
    //  Change the descriptor set corresponding to the current frame in flight only!
    // else:
    //  This resource is not updated every frame,
    //  we can define the descriptor sets for all the frames in flight at once only one time.

    detail::BindingState state = {
        .setIdx = setIdx,
        .writeOp = {}};

    if (atActiveIndex) {
        state.writeOp.emplace_back(
            m_descriptorSets->getActive()[setIdx], bindingIdx, arrayElement, descriptorCount, descriptorType, &texture.descriptor);
    } else {
        for (uint32_t i = 0; i < getIndexCount(); i++) {
            state.writeOp.emplace_back(
                (*m_descriptorSets)[i][setIdx], bindingIdx, arrayElement, descriptorCount, descriptorType, &texture.descriptor);
        }
    }

    if (layout != vk::ImageLayout::eUndefined) {
        // instead of using texture.descriptor, create new vk::DescriptorImageInfo with imageLayout == layout

        state.descriptorImageInfo.push_back(std::make_shared<vk::DescriptorImageInfo>(texture.descriptor));
        state.descriptorImageInfo[0]->imageLayout = layout;
        if (atActiveIndex) {
            // state is local variable. in case of atActiveIndex, writeOp.size()==1, hence indexing at 0
            state.writeOp[0].pImageInfo = state.descriptorImageInfo[0].get();
        } else {
            for (uint32_t i = 0; i < getIndexCount(); i++) {
                state.writeOp[i].pImageInfo = state.descriptorImageInfo[0].get();
            }
        }
    }

    m_descriptorSetWrites[m_descriptorSetNumberToIdx[setIdx]][bindingIdx] = state;

    device().updateDescriptorSets(state.writeOp, {});
}

void PassBase::updateDescriptorSetsImage(uint32_t setIdx, uint32_t bindingIdx, MultiBufferedResource<std::shared_ptr<Texture>> &textures, vk::DescriptorType descriptorType, vk::ImageLayout layout) {
    const auto arrayElement = 0;
    const auto descriptorCount = 1;

    std::vector<vk::WriteDescriptorSet> writeOp = {};
    for (uint32_t i = 0; i < getIndexCount(); i++) {
        writeOp.emplace_back(
            (*m_descriptorSets)[i][setIdx], bindingIdx, arrayElement, descriptorCount, descriptorType, &textures[i]->descriptor);
    }

    detail::BindingState state = {
        .setIdx = setIdx,
        .writeOp = writeOp};

    if (layout != vk::ImageLayout::eUndefined) {
        for (uint32_t i = 0; i < getIndexCount(); i++) {
            state.descriptorImageInfo.push_back(std::make_shared<vk::DescriptorImageInfo>(textures[i]->descriptor));
            state.descriptorImageInfo[i]->imageLayout = layout;
            state.writeOp[i].pImageInfo = state.descriptorImageInfo[i].get();
        }
    }

    m_descriptorSetWrites[m_descriptorSetNumberToIdx[setIdx]][bindingIdx] = state;

    device().updateDescriptorSets(writeOp, {});
}

void PassBase::setStorageBuffer(uint32_t setIdx, uint32_t bindingIdx, Buffer &buffer, bool atActiveIndex) {
    std::vector<vk::WriteDescriptorSet> writeOp = {};
    if (atActiveIndex) {
        writeOp.emplace_back(vk::WriteDescriptorSet(m_descriptorSets->getActive()[setIdx], bindingIdx, 0, vk::DescriptorType::eStorageBuffer, {}, buffer.descriptor));
    } else {
        for (uint32_t i = 0; i < getIndexCount(); i++) {
            writeOp.emplace_back(vk::WriteDescriptorSet(
                (*m_descriptorSets)[i][setIdx], bindingIdx, 0, vk::DescriptorType::eStorageBuffer, {}, buffer.descriptor));
        }
    }

    detail::BindingState state = {
        .setIdx = setIdx,
        .writeOp = writeOp};

    m_descriptorSetWrites[m_descriptorSetNumberToIdx[setIdx]][bindingIdx] = state;

    device().updateDescriptorSets(writeOp, {});
}

std::shared_ptr<MultiBufferedTexture> PassBase::reflectTextures(const char *name, TextureReflectionOptions opts) const {
    return std::make_shared<MultiBufferedTexture>(getMultiBuffering(), reflectTexture(std::string(name), std::move(opts)));
}

std::shared_ptr<Texture> PassBase::getTexture(const std::string &name, TextureReflectionOptions opts) {
    std::shared_ptr<Texture> texture = reflectTexture(name, std::move(opts));
    texture->initResources();
    texture->setName(m_label + "." + name);

    if (texture->usage & vk::ImageUsageFlagBits::eStorage)
        setStorageImage(name, *texture);
    else
        setImageSampler(name, *texture);

    return texture;
}

std::shared_ptr<UniformReflected> PassBase::getUniformSet(const std::string &name) {
    auto set = ::vvv::reflectUniformSet(getCtx(), getShaders(), name);
    set->createGpuBuffers(getCtx(), getIndexCount());

    for (int i = 0; i < getIndexCount(); ++i) {
        set->upload(i);
    }

    setUniformBuffer(*set);
    return set;
}

void PassBase::setUniformBuffer(UniformReflected &uniform) {
    auto loc = uniform.getLocation();
    setUniformBuffer(loc.set_number, loc.binding_number, uniform);
}

void PassBase::setUniformBuffer(uint32_t setIdx, uint32_t bindingIdx, UniformReflected &uniform) {
    assert(uniform.getCopies() == getIndexCount());

    std::vector<vk::DescriptorBufferInfo> uniformBufferInfo = {};
    for (uint32_t i = 0; i < getIndexCount(); i++) {
        const auto &data = *uniform.getGpuBuffer(i);
        uniformBufferInfo.emplace_back(data.getBuffer(), 0, data.getByteSize());
    }

    std::vector<vk::WriteDescriptorSet> writeOp = {};
    for (uint32_t i = 0; i < getIndexCount(); i++) {
        writeOp.emplace_back(vk::WriteDescriptorSet(
            (*m_descriptorSets)[i][setIdx], bindingIdx, 0, vk::DescriptorType::eUniformBuffer, {}, uniformBufferInfo[i]));
    }

    detail::BindingState state = {
        .setIdx = setIdx,
        .writeOp = writeOp,
        .uniformBufferInfo = uniformBufferInfo};

    m_descriptorSetWrites[m_descriptorSetNumberToIdx[setIdx]][bindingIdx] = state;

    // rebind the pointer to the persistent storage location (terrible code)
    for (uint32_t i = 0; i < getIndexCount(); i++) {
        m_descriptorSetWrites[m_descriptorSetNumberToIdx[setIdx]][bindingIdx].writeOp[i].pBufferInfo =
            &m_descriptorSetWrites[m_descriptorSetNumberToIdx[setIdx]][bindingIdx].uniformBufferInfo[i];
    }

    device().updateDescriptorSets(writeOp, {});
}

void PassBase::createCommandBuffers() {
    const auto device = getCtx()->getDevice();
    const auto debug = getCtx()->debugMarker;

    vk::CommandPoolCreateInfo cmdPoolInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_queueFamilyIndex);
    m_commandPool = device.createCommandPool(cmdPoolInfo);
    debug->setName(m_commandPool, m_label + ".m_commandPool");
    vk::CommandBufferAllocateInfo cmdBufferAllocInfo(m_commandPool, vk::CommandBufferLevel::ePrimary, getIndexCount());
    std::vector<vk::CommandBuffer> commandBuffers = device.allocateCommandBuffers(cmdBufferAllocInfo);
    m_commandBuffer = std::make_unique<MultiBufferedResource<vk::CommandBuffer>>(getMultiBuffering(), commandBuffers);

    for (int i = 0; i < getActiveIndex(); ++i) {
        debug->setName((*m_commandBuffer)[i], m_label + ".m_commandBuffer." + std::to_string(i));
    }
}

void PassBase::createPipelineLayout() {

    const auto device = getCtx()->getDevice();
    const auto debug = getCtx()->debugMarker;

    std::map<vk::DescriptorType, uint32_t> descriptorCounts = {};

    for (auto &shader : m_shaders) {
        auto layouts = shader->reflectDescriptorLayouts();
        for (auto &layout : layouts) {
            size_t index = m_descriptorSetLayouts.size();
            // could check overlapping bindings for compatibility here, or allow a per shader descriptor set.
            // we currently set all descriptors at the beginning of the multistage pass.
            if (m_descriptorSetNumberToIdx.contains(layout.set_number)) {
                continue;
            }

            auto descriptorSetLayout = device.createDescriptorSetLayout(layout.create_info);
            debug->setName(descriptorSetLayout, m_label + ".m_descSetLayouts[idx=" + std::to_string(index) + ",set=" + std::to_string(layout.set_number) + "]");
            m_descriptorSetLayouts.push_back(descriptorSetLayout);
            m_descriptorSetNumberToIdx.insert({layout.set_number, index});

            for (const auto &binding : layout.bindings) {
                if (!descriptorCounts.contains(binding.descriptorType)) {
                    descriptorCounts[binding.descriptorType] = 0;
                }
                descriptorCounts[binding.descriptorType] += binding.descriptorCount;
            }
        }
    }

    setResourceCount(m_descriptorSetNumberToIdx.size());
    m_descriptorSetWrites.resize(m_descriptorSetNumberToIdx.size());

    std::vector<vk::DescriptorPoolSize> poolSizes;

    for (const auto &[descType, descCount] : descriptorCounts) {
        poolSizes.emplace_back(descType, descCount * getIndexCount());
    }

    if (hasDescriptors()) {
        m_descriptorPool =
            device.createDescriptorPool(vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, m_descriptorSetLayouts.size() * getIndexCount(), poolSizes));
        debug->setName(m_descriptorPool, m_label + ".m_descriptorPool");

        m_descriptorSets = std::make_unique<MultiBufferedResource<std::vector<vk::DescriptorSet>>>(getMultiBuffering());
        for (uint32_t idx = 0; idx < getIndexCount(); idx++) {
            vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo(m_descriptorPool, m_descriptorSetLayouts);
            const auto descriptorSets = device.allocateDescriptorSets(descriptorSetAllocateInfo);
            for (size_t setIdx = 0; setIdx < descriptorSets.size(); setIdx++) {
                debug->setName(descriptorSets[setIdx], m_label + ".m_descriptorSets?multibuffering=" + std::to_string(idx) + "&setIdx=" + std::to_string(setIdx));
            }
            (*m_descriptorSets)[idx] = std::move(descriptorSets);
        }
    }

    vk::PipelineLayoutCreateInfo pipeInfo({}, m_descriptorSetLayouts);
    std::vector<vk::PushConstantRange> pushConstantRanges = definePushConstantRanges();
    if (!pushConstantRanges.empty()) {
        pipeInfo.pushConstantRangeCount = pushConstantRanges.size();
        pipeInfo.pPushConstantRanges = pushConstantRanges.data();
    }
    m_pipelineLayout = device.createPipelineLayout(pipeInfo);
    debug->setName(m_pipelineLayout, m_label + ".m_pipelineLayout");
}
