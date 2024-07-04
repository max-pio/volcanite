#pragma once

#include <utility>
#include <vvv/core/preamble.hpp>

#include <vvv/core/MultiBuffering.hpp>

#include <vvv/reflection/TextureReflection.hpp>
#include <vvv/reflection/UniformReflection.hpp>

#include <vvv/util/Logger.hpp>

#include <map>
#include <utility>

namespace vvv {

// template <class U>
// class UniformSet : public virtual MultiBuffering, public virtual WithGpuContext {
//
// public:
//    UniformSet(GpuContextPtr ctx, uint32_t copies = 1) : MultiBuffering(copies), WithGpuContext(ctx), m_isUniformSetDirty(copies, true) {}
//
//    /**
//     * Update the value of an option that influences the value of the uniform buffer.
//     *
//     * In case you have to do additional work for a specific member, like for example,
//     * recreate shaders because the uniform also influences a preprocessor macro, just
//     * override this function for the given member:
//     *
//     * ```
//     * void setUniform(uint32_t* SVGFPass::Iterations, uint32_t value,
//     * ```
//     */
//    template <typename T> void setUniform(T U::*member, T val, bool retainedGpuUpload = true) {
//        if (m_uniforms.*member != val) {
//            m_uniforms.*member = val;
//            std::fill(m_isUniformSetDirty.begin(), m_isUniformSetDirty.end(), true);
//            // if (isPipelineCreated() && !retainedGpuUpload) {
//            //     updateUniformDescriptorSet();
//            // }
//        }
//    }
//
//    void updateUniformDescriptorSet(idx = getActiveIndex()) {
//        const Uniform_TransferFunction2D ubo = {
//            .backgroundOpacity = m_options.backgroundOpacity,
//            .foregroundOpacity = m_options.foregroundOpacity,
//            .feathering = m_options.feathering,
//        };
//
//        const auto device = ctx()->getDevice();
//
//        void *data = device
//         .mapMemory(m_uniformBufferMemory, 0, sizeof(ubo), {});
//        memcpy(data, &ubo, sizeof(ubo));
//        device.unmapMemory(m_uniformBufferMemory);
//    }
//
//    size_t getUniformSetByteSize() { return sizeof(U); }
//
// protected:
//    void resetUniformsDirtyFlag() { m_isUniformSetDirty = false; }
//    bool areUniformsDirty() const { return m_isUniformSetDirty; }
//
//    virtual void updateUniformDescriptorSet();
//
// private:
//    GpuContextPtr m_ctx;
//    U m_uniforms;
//    std::vector<bool> m_isUniformSetDirty;
//};
//
// TODO: maybe make this untyped, and add macros to generate accessors?
// template <class UniformSets = EmptyEnum, class StorageImages = EmptyEnum, class StorageBuffers = EmptyEnum, class ImageSamplers = EmptyEnum, > struct PassComputeStructure {
//    type_t<UniformSets> uniformSets;
//    type_t<StorageImages> storageImages;
//    type_t<ImageSamplers> imageSamplers;
//    type_t<StorageBuffers> storageBuffers;
//};

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
    //! Releases all vulkan resources including the shaders and pipelines returned by the subclassed creation methods.
    //! Subclasses that override this method must call the parent method to release parent resources.
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
            Logger(WARN) << "calling getShaders of pass before allocateResources() was called / pipeline was created!";
        }

        return m_shaders;
    }
    DescriptorBinding findDescriptorBindingByName(const std::string &name);
    /**
     * Creates a texture through reflection (`reflectTexture`) and automatically configures it for usage with this compute pass.
     * @param name variable name of the texture
     * @param opts options for reflection
     */
    std::shared_ptr<Texture> getTexture(const std::string &name, TextureReflectionOptions opts);
    std::shared_ptr<UniformReflected> getUniformSet(const std::string &name);

    // these can be generalized to any class, move to own abstract thingy
    // TODO: we probably want multibufferingCopy to support `MULTIBUFFERING_CURRENT_ACTIVE_COPY` (default) and `MULTIBUFFERING_ALL_COPIES`,
    // we could use negative numbers to indicates this.

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
    //
    // TODO(Reiner): the last approach seems most promising from a resource recycling and memory overhead perspective.

    //    ResourceId newResource(bool initiallyDirty = true) {
    //        const auto idx = m_isDirty.size();
    //        m_isDirty.push_back(initiallyDirty ? m_allDirtyMask : 0 /* allCleanMask */);
    //        return idx;
    //    }

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

    //! Creates all shaders that are used in this pass. Shader reflections from this pass are performed on this shader list.
    virtual std::vector<std::shared_ptr<Shader>> createShaders() = 0;

    void createPipelineLayout(uint32_t push_constant_byte_size = 0);

    //! Creates one (single pass) or more (multi pass) pipelines. At this point, the pipeline layout is already created from the shaders.
    virtual std::vector<vk::Pipeline> createPipelines() = 0;

    std::string m_label;
    uint32_t m_queueFamilyIndex;

    std::vector<vk::Pipeline> m_pipelines = {};
    vk::PipelineLayout m_pipelineLayout = nullptr;
    vk::DescriptorPool m_descriptorPool = nullptr;
    vk::CommandPool m_commandPool = nullptr; // TODO(Reiner): a commandPool for the whole context would be enough and more efficient, right?

    std::map<uint32_t, size_t> m_descriptorSetNumberToIdx = {};
    std::vector<vk::DescriptorSetLayout> m_descriptorSetLayouts = {};
    std::unique_ptr<MultiBufferedResource<std::vector<vk::DescriptorSet>>> m_descriptorSets = {};
    std::vector<std::map<uint32_t, detail::BindingState>> m_descriptorSetWrites = {}; // indexed as [m_descriptorSetNumberToIdx[set_number]][binding] // TODO(Reiner): this update mechanic is so dumb
    std::unique_ptr<MultiBufferedResource<vk::CommandBuffer>> m_commandBuffer;

    std::vector<std::shared_ptr<Shader>> m_shaders = {};

    uint32_t m_allDirtyMask;

    //! a simple caching mechanic using a matrix of {Resource Id}x{Buffer Copy} bits to track if a resource needs to
    //! be updated (is dirty) before use. Index with `m_isDirty[resourceId]`, then each bit corresponds to a buffer copy.
    std::vector<uint32_t> m_isDirty;
};

} // namespace vvv
