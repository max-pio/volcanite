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

#include "Buffer.hpp"
#include "GpuContext.hpp"
#include "Shader.hpp"

#include "vvv/vk/format_utils.hpp"
#include "vvv/vk/memory.hpp"
#include <vulkan/vulkan.hpp>

#include "stb/stb_image_write.hpp"
// #include "tinyexr/tinyexr.hpp"
#include "vvv/util/util.hpp"

#include <set>
#include <stdexcept>
#include <string>

namespace vvv {

const std::set<uint32_t> TextureExclusiveQueueUsage = {};

enum class TextureDimensions {
    e1D = 0,
    e2D = 1,
    e3D = 2,
};

typedef std::underlying_type<TextureDimensions>::type TextureDimensionIndex;

const vk::ImageType lookupImageType[] = {vk::ImageType::e1D, vk::ImageType::e2D, vk::ImageType::e3D};

const vk::ImageViewType lookupImageViewType[] = {vk::ImageViewType::e1D, vk::ImageViewType::e2D, vk::ImageViewType::e3D};

class Texture {
  public:
    vk::Sampler sampler = nullptr;
    vk::Image image = nullptr;
    vk::DeviceMemory deviceMemory = nullptr;
    vk::ImageView view = nullptr;
    vk::DescriptorImageInfo descriptor = {};
    vk::Format format;
    vk::ImageUsageFlags usage;
    vk::ImageAspectFlagBits aspectMask = vk::ImageAspectFlagBits::eNoneKHR;

    std::vector<uint32_t> queues;

    TextureDimensions dims;
    uint32_t width, height, depth;
    uint32_t mipLevels = 1;

    vvv::GpuContextPtr m_ctx;

    /// Create the CPU side representation of a texture object. You can subsequently initialize GPU side state either
    /// using `Texture::upload` or using `Texture::initResources`.
    ///
    /// @param queues if the texture is used in multiple queues, the queue indices of the queues the texture will be used in concurrently. If the texture is only
    /// used in a single queue, this parameter can be left empty. See exclusive and concurrent sharing modes in the Vulkan specification for details.
    Texture(vvv::GpuContextPtr ctx, vk::Format format, uint32_t width, uint32_t height, uint32_t depth, vk::ImageUsageFlags usage, const std::set<uint32_t> &queues = TextureExclusiveQueueUsage)
        : m_ctx(ctx), dims(TextureDimensions::e3D), format(format), width(width), height(height), depth(depth), usage(Texture::defaultUsage(usage)),
          aspectMask(FormatHasDepth(static_cast<VkFormat>(format)) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor), queues(queues.begin(), queues.end()) {}

    Texture(vvv::GpuContextPtr ctx, vk::Format format, uint32_t width, uint32_t height, vk::ImageUsageFlags usage, std::set<uint32_t> queues = TextureExclusiveQueueUsage)
        : m_ctx(ctx), dims(TextureDimensions::e2D), format(format), width(width), height(height), depth(1), usage(Texture::defaultUsage(usage)),
          aspectMask(FormatHasDepth(static_cast<VkFormat>(format)) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor), queues(queues.begin(), queues.end()) {}

    Texture(vvv::GpuContextPtr ctx, vk::Format format, uint32_t width, vk::ImageUsageFlags usage, std::set<uint32_t> queues = TextureExclusiveQueueUsage)
        : m_ctx(ctx), dims(TextureDimensions::e1D), format(format), width(width), height(1), depth(1), usage(Texture::defaultUsage(usage)),
          aspectMask(FormatHasDepth(static_cast<VkFormat>(format)) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor), queues(queues.begin(), queues.end()) {}

    Texture(vvv::GpuContextPtr ctx, vk::Format format, TextureDimensions dims, uint32_t width, uint32_t height, uint32_t depth, vk::ImageUsageFlags usage,
            std::set<uint32_t> queues = TextureExclusiveQueueUsage)
        : m_ctx(ctx), dims(dims), format(format), width(width), height(height), depth(depth), usage(Texture::defaultUsage(usage)),
          aspectMask(FormatHasDepth(static_cast<VkFormat>(format)) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor), queues(queues.begin(), queues.end()) {
        if (dims == TextureDimensions::e2D || dims == TextureDimensions::e1D) {
            assert(depth == 1);
        }
        if (dims == TextureDimensions::e1D) {
            assert(height == 1);
        }
    }

    // Note: pattern to get named constructors that are callable with new and without new.
    struct outputLdr;
    struct colorAttachmentLDR;
    struct depthAttachment;
    struct input3d;
    struct input2d;
    struct input1d;

    ~Texture() {
        if (static_cast<vk::ImageView>(nullptr) != view)
            m_ctx->getDevice().destroyImageView(view);
        if (image != static_cast<vk::Image>(nullptr))
            m_ctx->getDevice().destroyImage(image);
        if (sampler != static_cast<vk::Sampler>(nullptr))
            m_ctx->getDevice().destroySampler(sampler);
        if (deviceMemory != static_cast<vk::DeviceMemory>(nullptr))
            m_ctx->getDevice().freeMemory(deviceMemory);
    }

    /// Add a debug label to all GPU resources associated with the texture.
    /// The GPU-side state MUST be initialized through `Texture::upload` or `Texture::initResources` prior to calling this method.
    /// @param name a label used in debuggers and log messages
    void setName(const std::string &name);
    std::string getName() { return m_label; }

    size_t memorySize(vk::ImageAspectFlags aspectMask) const;
    size_t memorySize() const { return memorySize(aspectMask); } // vk::ImageAspectFlagBits::eColor);

    void setImageLayout(vk::CommandBuffer const &commandBuffer, vk::ImageLayout destinationImageLayout, vk::PipelineStageFlags destinationStage = vk::PipelineStageFlags()) {
        ::vvv::setImageLayout(commandBuffer, image, format, descriptor.imageLayout, destinationImageLayout, destinationStage);
        descriptor.imageLayout = destinationImageLayout;
    }

    [[nodiscard]] vvv::AwaitableHandle setImageLayout(vk::ImageLayout destinationImageLayout, vk::PipelineStageFlags destinationStage = vk::PipelineStageFlags(), vvv::detail::OpenGLStyleSubmitOptions opts = {}) {
        ensureResources();
        return m_ctx->executeCommands([&](vk::CommandBuffer commandBuffer) { setImageLayout(commandBuffer, destinationImageLayout, destinationStage); }, opts);
    }

    /// Transfer ownership of an exclusive resource to another queue.
    /// @see <a href="https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#synchronization-queue-transfers">Vulkan Specification on Queue Transfers</a>
    /// @param fromQueueFamilyIndex family index of the queue releasing ownership
    /// @param toQueueFamilyIndex family index of the queue acquiring ownership
    [[nodiscard]] vk::ImageMemoryBarrier queueOwnershipTransfer(uint32_t fromQueueFamilyIndex, vk::AccessFlagBits srcAccess, uint32_t toQueueFamilyIndex, vk::AccessFlagBits dstAccess,
                                                                vk::ImageLayout transitionToLayout) const;
    [[nodiscard]] vk::ImageMemoryBarrier queueOwnershipTransfer(uint32_t fromQueueFamilyIndex, vk::AccessFlagBits srcAccess, uint32_t toQueueFamilyIndex, vk::AccessFlagBits dstAccess) {
        return queueOwnershipTransfer(fromQueueFamilyIndex, srcAccess, toQueueFamilyIndex, dstAccess, descriptor.imageLayout);
    }

    /// Upload data to the GPU using a staging buffer
    ///
    /// @param commandBuffer a command buffer, which MUST be in RECORDING state. this routine will only insert upload commands without any synchronization primitives.
    /// The command buffer MUST be created with the device associated with the context of the texture.
    /// @param rawData texels that should be uploaded
    /// @param destiniationImageLayout transfer the image into the specified layout. Set to `vk::ImageLayout::eTransferDstOptimal` to not perform the final image layout transfer.
    /// @param destinationStage forces a destination stage, this might be required for multi-queue setups where `destiniationImageLayout` is ambiguous. For example, `eShaderReadOnlyOptimal`
    /// could point to `eCompute` or `eFragment`.
    void upload(vk::CommandBuffer commandBuffer, vvv::Buffer const &stagingBuffer, const void *const rawData, vk::ImageLayout destinationImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::PipelineStageFlags destinationStage = vk::PipelineStageFlags());

    /// This method is here for convenience. It's more performant to enqueue this directly into an existing command buffer using the other overload.
    [[nodiscard]] std::pair<vvv::AwaitableHandle, std::shared_ptr<vvv::Buffer>> upload(const void *const rawData, vk::ImageLayout destinationImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                                                                       vk::PipelineStageFlags destinationStage = vk::PipelineStageFlags(), vvv::detail::OpenGLStyleSubmitOptions opts = {});

    template <typename T>
    [[nodiscard]] std::pair<vvv::AwaitableHandle, std::shared_ptr<vvv::Buffer>> upload(const std::vector<T> &rawData, vk::ImageLayout destinationImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                                                                       vk::PipelineStageFlags destinationStage = vk::PipelineStageFlags(), vvv::detail::OpenGLStyleSubmitOptions opts = {}) {
        assert(vectorByteSize(rawData) == memorySize());
        return upload(rawData.data(), destinationImageLayout, destinationStage, opts);
    }

    /// @discouraged this is a shorthand that drains the GPU pipeline and waits on the host. It allocates intermediate memory in
    /// the size of the texture.
    std::vector<uint8_t> download(uint32_t queueFamily = 0u) {
        // add full image barrier for download to host
        m_ctx->executeCommands([&](vk::CommandBuffer commandBuffer) {
            vk::ImageSubresourceRange imageSubresourceRange(aspectMask, 0, 1, 0, 1);
            vk::ImageMemoryBarrier imageMemoryBarrier(vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eHostWrite, descriptor.imageLayout, descriptor.imageLayout, queueFamily, queueFamily, image, imageSubresourceRange);
            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eHost, {}, nullptr, nullptr, imageMemoryBarrier);
        });
        return capture({.queueFamily = queueFamily, .hostWait = true}).second->download();
    }

    /// @discouraged this is a shorthand that drains the GPU pipeline and waits on the host.
    void writeExr(const std::string path, uint32_t queueFamily) {
        throw std::runtime_error("texture EXR export is not available because tinyexr implementation is missing.");
    }

    /// @discouraged this is a shorthand that drains the GPU pipeline and waits on the host.
    void writeHdr(const std::string path, uint32_t queueFamily = 0u) {
        const auto componentCount = FormatComponentCount(static_cast<VkFormat>(format));

        // Note: if four channels are given, alpha channel is dropped.
        assert((componentCount == 4 || componentCount == 3 || componentCount == 1) && "expecting r, rgb or rgba texture");
        const auto planeCount = FormatPlaneCount(static_cast<VkFormat>(format));
        assert(planeCount == 1);
        assert(FormatIsFloat(static_cast<VkFormat>(format)));
        assert(FormatElementIsTexel(static_cast<VkFormat>(format)));

        const auto texelSize = FormatTexelSize(static_cast<VkFormat>(format));

        const auto componentSize = texelSize / componentCount;

        assert(componentSize == 4 && "expecting 32bit float");

        const auto data = download(queueFamily);

        if (!stbi_write_hdr(path.c_str(), width, height, componentCount, reinterpret_cast<const float *>(data.data()))) {
            throw std::runtime_error("writing HDR file failed.");
        }
    }

    /// @discouraged this is a shorthand that drains the GPU pipeline and waits on the host.
    void writePng(const std::string path, uint32_t queueFamily = 0u) {
        const auto componentCount = FormatComponentCount(static_cast<VkFormat>(format));
        assert((componentCount == 4 || componentCount == 3 || componentCount == 1) && "expecting r, rg, rgb or rgba texture");
        const auto planeCount = FormatPlaneCount(static_cast<VkFormat>(format));
        assert(planeCount == 1);
        assert(FormatIsUInt(static_cast<VkFormat>(format)) || FormatIsUNorm(static_cast<VkFormat>(format)));
        assert(FormatElementIsTexel(static_cast<VkFormat>(format)));

        const auto texelSize = FormatTexelSize(static_cast<VkFormat>(format));

        const auto componentSize = texelSize / componentCount;

        assert(componentSize == 1 && "expecting 8bit unsigned integers");

        const auto data = download(queueFamily);

        const auto stride = componentCount * width;

        if (!stbi_write_png(path.c_str(), width, height, componentCount, reinterpret_cast<const void *>(data.data()), stride)) {
            throw std::runtime_error("writing PNG failed.");
        }
    }

    void writeJpeg(const std::string path, int quality = 70, uint32_t queueFamily = 0u) {
        const auto componentCount = FormatComponentCount(static_cast<VkFormat>(format));

        // Note: if four channels are given, alpha channel is dropped.
        assert((componentCount == 4 || componentCount == 3 || componentCount == 1) && "expecting r, rgb or rgba texture");
        const auto planeCount = FormatPlaneCount(static_cast<VkFormat>(format));
        assert(planeCount == 1);
        assert(FormatIsUInt(static_cast<VkFormat>(format)) || FormatIsUNorm(static_cast<VkFormat>(format)));
        assert(FormatElementIsTexel(static_cast<VkFormat>(format)));

        const auto texelSize = FormatTexelSize(static_cast<VkFormat>(format));

        const auto componentSize = texelSize / componentCount;

        assert(componentSize == 1 && "expecting 8bit unsigned integers");

        const auto data = download(queueFamily);

        if (!stbi_write_jpg(path.c_str(), width, height, componentCount, reinterpret_cast<const void *>(data.data()), quality)) {
            throw std::runtime_error("writing JPEG failed.");
        }
    }

    /// Select an export image file type based on the file ending (png, jp(e)g, hdr, exr).
    /// May throw a runtime error if filesystem or image export functionality fails or if the file type is not supported.
    void writeFile(const std::string path, uint32_t queueFamily = 0u) {
        std::filesystem::path file = std::filesystem::absolute(path).lexically_normal();
        std::filesystem::path dir = file;
        std::filesystem::create_directories(dir.remove_filename());

        const auto componentCount = FormatComponentCount(static_cast<VkFormat>(format));
        const auto planeCount = FormatPlaneCount(static_cast<VkFormat>(format));
        const auto texelSize = FormatTexelSize(static_cast<VkFormat>(format));
        const auto componentSize = texelSize / componentCount;
        if (path.ends_with(".png") || path.ends_with(".jpg") || path.ends_with(".jpeg")) {
            if (!(componentCount == 4 || componentCount == 3 || componentCount == 1) || planeCount != 1 || !(FormatIsUInt(static_cast<VkFormat>(format)) || FormatIsUNorm(static_cast<VkFormat>(format))) || !FormatElementIsTexel(static_cast<VkFormat>(format)) || componentSize != 1)
                throw std::runtime_error("texture format does not support png/jpg export");
        } else if (path.ends_with(".exr") || path.ends_with(".hdr")) {
            if (!(componentCount == 4 || componentCount == 3 || componentCount == 1) || planeCount != 1 || !FormatIsFloat(static_cast<VkFormat>(format)) || !FormatElementIsTexel(static_cast<VkFormat>(format)) || componentSize != 4)
                throw std::runtime_error("texture format does not support exr/hdr export");
        } else {
            throw std::runtime_error("unsupported image file type " + path.substr(path.rfind("."), path.length()) + ", use png, jpg, exr or hdr");
        }

        if (path.ends_with(".png"))
            this->writePng(std::filesystem::absolute(file).lexically_normal().string(), queueFamily);
        else if (path.ends_with(".jpg") || path.ends_with(".jpeg"))
            this->writeJpeg(std::filesystem::absolute(file).lexically_normal().string(), 90, queueFamily);
        else if (path.ends_with(".exr"))
            this->writeExr(std::filesystem::absolute(file).lexically_normal().string(), queueFamily);
        else if (path.ends_with(".hdr"))
            this->writeHdr(std::filesystem::absolute(file).lexically_normal().string(), queueFamily);
    }

    /// Combination of `capture` and `Buffer::download`.
    // [[nodiscard]] std::pair<vvv::Awaitable, std::shared_ptr<vvv::Buffer>> download();

    /// Create a copy of the textures current state on the GPU
    void capture(vk::CommandBuffer commandBuffer, vvv::Buffer const &stagingBuffer, vk::PipelineStageFlags destinationStage);
    void capture(vk::CommandBuffer commandBuffer, vvv::Buffer const &stagingBuffer) { return capture(commandBuffer, stagingBuffer, vk::PipelineStageFlagBits::eAllCommands); }
    std::shared_ptr<vvv::Buffer> capture(vk::CommandBuffer commandBuffer, vk::PipelineStageFlags destinationStage) {
        auto stagingBuffer = std::make_shared<vvv::Buffer>(m_ctx, vvv::BufferSettings{.byteSize = memorySize()});
        capture(commandBuffer, *stagingBuffer.get(), destinationStage);
        return stagingBuffer;
    }
    std::shared_ptr<vvv::Buffer> capture(vk::CommandBuffer commandBuffer) { return capture(commandBuffer, vk::PipelineStageFlagBits::eAllCommands); }
    [[nodiscard]] std::pair<vvv::AwaitableHandle, std::shared_ptr<vvv::Buffer>> capture(vvv::detail::OpenGLStyleSubmitOptions opts = {},
                                                                                        vk::PipelineStageFlags destinationStage = vk::PipelineStageFlagBits::eAllCommands) {
        auto staging = std::make_shared<vvv::Buffer>(m_ctx, vvv::BufferSettings{
                                                                .label = "staging" + (m_label != "" ? "(" + m_label + ")" : ""),
                                                                .byteSize = memorySize(),
                                                            });

        auto awaitable = m_ctx->executeCommands([&](vk::CommandBuffer commandBuffer) { capture(commandBuffer, *staging, destinationStage); }, opts);

        return {awaitable, staging};
    }

    bool isUploaded() const { return m_uploaded; }
    void setUploaded(bool v) { m_uploaded = v; }

    /// Initialize GPU resources for the texture.
    void ensureResources() {
        if (!areResourcesInitialized()) {
            initResources();
        }
    }
    void initResources();
    bool areResourcesInitialized() { return sampler != static_cast<vk::Sampler>(nullptr); }

  private:
    /// Make sure all textures are downloadable to the CPU. (performance implications?)
    static vk::ImageUsageFlags defaultUsage(vk::ImageUsageFlags usage) { return usage | vk::ImageUsageFlagBits::eTransferSrc; }

    void checkGpuSupport() const;

    vk::ImageCreateInfo defaultImageCreateInfo() const;
    vk::ImageViewCreateInfo defaultCreateImageViewInfo() const;
    vk::SamplerCreateInfo defaultSamplerCreateInfo() const;

    bool m_uploaded = false;

    // store label here for naming staging buffers
    std::string m_label = "";
};

struct Texture::outputLdr : public Texture {
    /// Create a rgba8u texture that can be used for writing in a compute shader and blitting to the graphics queue.
    outputLdr(vvv::GpuContextPtr ctx, uint32_t width, uint32_t height)
        : Texture(ctx, vk::Format::eR8G8B8A8Unorm, width, height, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
                  TextureExclusiveQueueUsage) {}
};

struct Texture::colorAttachmentLDR : public Texture {
    /// Create a rgba8u texture that can be used for writing in a compute shader and render pass as a color attachment and blitting to the graphics queue.
    colorAttachmentLDR(vvv::GpuContextPtr ctx, uint32_t width, uint32_t height)
        : Texture(ctx, vk::Format::eR8G8B8A8Unorm, width, height,
                  vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc, TextureExclusiveQueueUsage) {}
};

struct Texture::depthAttachment : public Texture {
    /// Create a rgba8u texture that can be used for writing in a compute shader and blitting to the graphics queue.
    depthAttachment(vvv::GpuContextPtr ctx, uint32_t width, uint32_t height, vk::Format format = vk::Format::eD32Sfloat, vk::ImageUsageFlags usage = {}, std::set<uint32_t> queues = TextureExclusiveQueueUsage)
        : Texture(ctx, format, width, height, usage | vk::ImageUsageFlagBits::eDepthStencilAttachment, queues) {
    }
};

struct Texture::input3d : public Texture {
    input3d(vvv::GpuContextPtr ctx, vk::Format format, uint32_t width, uint32_t height, uint32_t depth)
        : Texture(ctx, format, width, height, depth, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, TextureExclusiveQueueUsage) {}
};

struct Texture::input2d : public Texture {
    input2d(vvv::GpuContextPtr ctx, vk::Format format, uint32_t width, uint32_t height)
        : Texture(ctx, format, width, height, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, TextureExclusiveQueueUsage) {}
};

struct Texture::input1d : public Texture {
    input1d(vvv::GpuContextPtr ctx, vk::Format format, uint32_t width)
        : Texture(ctx, format, width, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, TextureExclusiveQueueUsage) {}
};

} // namespace vvv