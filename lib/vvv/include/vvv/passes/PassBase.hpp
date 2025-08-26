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

#pragma once

#include <utility>
#include <vvv/core/preamble.hpp>

#include <vvv/core/MultiBuffering.hpp>

#include <vvv/reflection/TextureReflection.hpp>
#include <vvv/reflection/UniformReflection.hpp>

#include <vvv/util/Logger.hpp>

#include <map>

namespace vvv {

namespace detail {
struct BindingState {
    uint32_t setIdx;
    // everything set except for `.descriptorSet`
    std::vector<vk::WriteDescriptorSet> writeOp = {};
    // extends the lifetime of data for some binding types
    std::vector<vk::DescriptorBufferInfo> uniformBufferInfo = {};
    std::vector<std::shared_ptr<vk::DescriptorImageInfo>> descriptorImageInfo = {};
};
} // namespace detail

class PassBase : public virtual WithMultiBuffering, public virtual WithGpuContext {
  public:
    virtual ~PassBase() { assert(m_pipelines.empty() && "You must call freeResources() before destroying Pass objects"); }

    virtual void allocateResources();
    /// Releases all vulkan resources including the shaders and pipelines returned by the subclassed creation methods.
    /// Subclasses that override this method must call the parent method to release parent resources.
    virtual void freeResources();

    //  a pass is either `TimelineSemaphoreWaitable execute(queue)` or `executeCommands(vk::CommandBuffer commandBuffer)`.
    // The first submits to the queue itself and is required for multipass or multiqueue algorithms,
    // the second variant just writes into a command buffer and the caller is responsible for submitting the work. This
    // can be more efficient since the number of submits can be reduced.
    //
    // A `vk::CommandBuffer executeCommands()` variant that returns a secondary commandbuffer without an argument could be more ergonomic and efficient, but
    // harder to synchronize correctly. Not sure...
    [[nodiscard]] virtual AwaitableHandle execute(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr) = 0;

    [[nodiscard]] std::vector<std::shared_ptr<Shader>> getShaders() const {
        if (!isPipelineCreated()) {
            Logger(Warn) << "calling getShaders of pass before allocateResources() was called / pipeline was created!";
        }

        return m_shaders;
    }
    DescriptorBinding findDescriptorBindingByName(const std::string &name);

    /// Creates a texture through reflection (`reflectTexture`) and automatically configures it for usage with this compute pass.
    /// @param name variable name of the texture
    /// @param opts options for reflection
    std::shared_ptr<Texture> getTexture(const std::string &name, TextureReflectionOptions opts);
    std::shared_ptr<UniformReflected> getUniformSet(const std::string &name);

    // these can be generalized to any class, move to own abstract thingy
    //
    // Probably want multibufferingCopy to support `MULTIBUFFERING_CURRENT_ACTIVE_COPY` (default) and
    // `MULTIBUFFERING_ALL_COPIES`. Could use negative numbers to indicates this.

    void setImageSampler(uint32_t setIdx, uint32_t bindingIdx, Texture &texture, vk::ImageLayout layout = vk::ImageLayout::eUndefined, bool atActiveIndex = true);
    void setImageSamplerArray(uint32_t setIdx, uint32_t bindingIdx, uint32_t arrayElement, Texture &texture, vk::ImageLayout layout = vk::ImageLayout::eUndefined, bool atActiveIndex = true);
    void setImageSampler(const std::string &name, Texture &texture, vk::ImageLayout layout = vk::ImageLayout::eUndefined, bool atActiveIndex = true);
    void setImageSamplerArray(const std::string &name, uint32_t arrayElement, Texture &texture, vk::ImageLayout layout = vk::ImageLayout::eUndefined, bool atActiveIndex = true);
    void setImageSampler(uint32_t setIdx, uint32_t bindingIdx, MultiBufferedResource<std::shared_ptr<Texture>> &textures, vk::ImageLayout layout = vk::ImageLayout::eUndefined);
    void setImageSampler(const std::string &name, MultiBufferedResource<std::shared_ptr<Texture>> &textures, vk::ImageLayout layout = vk::ImageLayout::eUndefined);

    void setStorageImage(uint32_t setIdx, uint32_t bindingIdx, Texture &texture, vk::ImageLayout layout = vk::ImageLayout::eUndefined, bool atActiveIndex = true);
    void setStorageImageArray(uint32_t setIdx, uint32_t bindingIdx, uint32_t arrayElement, Texture &texture, vk::ImageLayout layout = vk::ImageLayout::eUndefined, bool atActiveIndex = true);
    void setStorageImage(const std::string &name, Texture &texture, vk::ImageLayout layout = vk::ImageLayout::eUndefined, bool atActiveIndex = true);
    void setStorageImageArray(const std::string &name, uint32_t arrayElement, Texture &texture, vk::ImageLayout layout = vk::ImageLayout::eUndefined, bool atActiveIndex = true);
    void setStorageImage(uint32_t setIdx, uint32_t bindingIdx, MultiBufferedResource<std::shared_ptr<Texture>> &textures, vk::ImageLayout layout = vk::ImageLayout::eUndefined);
    void setStorageImage(const std::string &name, MultiBufferedResource<std::shared_ptr<Texture>> &textures, vk::ImageLayout layout = vk::ImageLayout::eUndefined);

    void setStorageBuffer(uint32_t setIdx, uint32_t bindingIdx, Buffer &buffer, bool atActiveIndex = false);
    void setStorageBuffer(const std::string &name, uint32_t bindingIdx, Buffer &buffer, bool atActiveIndex = false) { assert(false && "set storage buffer by name not yet implemented!"); };

    void setUniformBuffer(UniformReflected &uniform);
    void setUniformBuffer(uint32_t setIdx, uint32_t bindingIdx, UniformReflected &uniform);

    [[nodiscard]] std::shared_ptr<UniformReflected> reflectUniformSet(const std::string &name) const { return ::vvv::reflectUniformSet(getCtx(), getShaders(), name); }
    [[nodiscard]] std::shared_ptr<Texture> reflectTexture(vk::ArrayProxy<const std::string> names, TextureReflectionOptions opts) const {
        return ::vvv::reflectTexture(getCtx(), getShaders(), names, std::move(opts));
    }
    std::shared_ptr<Texture> reflectTexture(const char *name, TextureReflectionOptions opts) const { return reflectTexture(std::string(name), std::move(opts)); }
    std::shared_ptr<MultiBufferedTexture> reflectTextures(const char *name, TextureReflectionOptions opts) const;

    [[nodiscard]] std::vector<std::shared_ptr<Texture>> reflectTextureArray(vk::ArrayProxy<const std::string> names, TextureReflectionOptions opts) const {
        return ::vvv::reflectTextureArray(getCtx(), getShaders(), names, std::move(opts));
    }
    std::vector<std::shared_ptr<Texture>> reflectTextureArray(const char *name, TextureReflectionOptions opts) const { return reflectTextureArray(std::string(name), std::move(opts)); }

    //
    // A note on caching of descriptor sets: the healthy mental model is that you are rebuilding descriptor sets each
    // frame by allocating them from the descriptor pool, writing and binding them. To reduce the GPU and synchronization
    // load, you might want to introduce caching on top of that. There are several approaches.
    //
    // Update Immediately: In contrast to not working with a cache, you are not rebuilding each frame, but only if there
    // was an update to the data. the problem with this is that you cannot update a resource that is currently in use.
    // So this is not a strategy for any real-time application or descriptor sets that are updated with high frequency
    // since you have to drain the GPU pipeline.
    //
    // Dirty State Tracking: This requires that you have a copy of each resource for each frame. But actually checking
    // for an update is very cheap. the only headache is that you have to track this per in-flight frame and update
    // each copy individually when the frame associated with the copy is no longer in-flight.
    //
    // Descriptor Set Hashing: here you create a hash that is unique for the resources bound to the set. The problem
    // with this is that creating the hash can be expensive on the CPU since you are effectively rebuilding the
    // descriptor set while xoring the pointers to resources written to the descriptor set. A workaround for this
    // is to cache the hash itself, e.g. store it and only recompute it when a resource changes. The advantage is
    // that we do not have to make N copies of the resource. We can use the same descriptor set for all in-flight
    // frames.

    [[nodiscard]] std::string getLabel() const { return m_label; }
    [[nodiscard]] uint32_t getQueueFamilyIndex() const { return m_queueFamilyIndex; }

  private:
    static uint32_t bufferIdToMask(BufferCopyId copy) { return (uint32_t)1 << copy; }
    void setResourceCount(size_t count, bool initiallyDirty = true) { m_isDirty = std::vector(count, initiallyDirty ? m_allDirtyMask : 0 /* allCleanMask */); }

    void updateDescriptorSetsImage(uint32_t setIdx, uint32_t bindingIdx, Texture &texture, vk::DescriptorType descriptorType, vk::ImageLayout layout = vk::ImageLayout::eUndefined, bool atActiveIndex = true);
    void updateDescriptorSetsImageArray(uint32_t setIdx, uint32_t bindingIdx, uint32_t arrayElement, Texture &texture, vk::DescriptorType descriptorType, vk::ImageLayout layout = vk::ImageLayout::eUndefined, bool atActiveIndex = true);
    void updateDescriptorSetsImage(uint32_t setIdx, uint32_t bindingIdx, MultiBufferedResource<std::shared_ptr<Texture>> &textures, vk::DescriptorType descriptorType, vk::ImageLayout layout = vk::ImageLayout::eUndefined);

  protected:
    PassBase(GpuContextPtr ctx, std::string label, const std::shared_ptr<MultiBuffering> &multiBuffering = NoMultiBuffering, uint32_t queueFamilyIndex = 0)
        : WithMultiBuffering(multiBuffering), WithGpuContext(ctx), m_label(std::move(label)), m_queueFamilyIndex(queueFamilyIndex),
          m_allDirtyMask(((uint32_t)1 << multiBuffering->getIndexCount()) - 1) {}

    bool isPipelineCreated() const { return !m_pipelines.empty(); }
    bool hasDescriptors() const { return !m_descriptorSetLayouts.empty(); }

    void createCommandBuffers();

    /// Creates all shaders that are used in this pass. Shader reflections from this pass are performed on this shader list.
    virtual std::vector<std::shared_ptr<Shader>> createShaders() = 0;

    virtual std::vector<vk::PushConstantRange> definePushConstantRanges() { return {}; };

    void createPipelineLayout();

    /// Creates one (single pass) or more (multi pass) pipelines. At this point, the pipeline layout is already created from the shaders.
    virtual std::vector<vk::Pipeline> createPipelines() = 0;

    std::string m_label;
    uint32_t m_queueFamilyIndex;

    std::vector<vk::Pipeline> m_pipelines = {};
    vk::PipelineLayout m_pipelineLayout = nullptr;
    vk::DescriptorPool m_descriptorPool = nullptr;
    vk::CommandPool m_commandPool = nullptr;

    std::map<uint32_t, size_t> m_descriptorSetNumberToIdx = {};
    std::vector<vk::DescriptorSetLayout> m_descriptorSetLayouts = {};
    std::unique_ptr<MultiBufferedResource<std::vector<vk::DescriptorSet>>> m_descriptorSets = {};
    std::vector<std::map<uint32_t, detail::BindingState>> m_descriptorSetWrites = {};
    std::unique_ptr<MultiBufferedResource<vk::CommandBuffer>> m_commandBuffer;

    std::vector<std::shared_ptr<Shader>> m_shaders = {};

    uint32_t m_allDirtyMask;

    /// a simple caching mechanic using a matrix of {Resource Id}x{Buffer Copy} bits to track if a resource needs to
    /// be updated (is dirty) before use. Index with `m_isDirty[resourceId]`, then each bit corresponds to a buffer copy.
    std::vector<uint32_t> m_isDirty;
};

} // namespace vvv
