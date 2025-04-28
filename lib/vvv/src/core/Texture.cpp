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

#include "vvv/core/Texture.hpp"

namespace vvv {

void Texture::setName(const std::string &name) {
    m_label = name;
    if (image)
        m_ctx->debugMarker->setName(image, name);
    if (view)
        m_ctx->debugMarker->setName(view, name);
    if (sampler)
        m_ctx->debugMarker->setName(sampler, name);
    if (deviceMemory)
        m_ctx->debugMarker->setName(deviceMemory, name);
}

size_t Texture::memorySize(vk::ImageAspectFlags aspectMask) const {
    const size_t block_size = (FormatElementSize(static_cast<VkFormat>(format), static_cast<VkImageAspectFlags>(aspectMask)));
    const VkExtent3D block_extent = FormatTexelBlockExtent(static_cast<VkFormat>(format));
    const uint32_t texels_per_block = block_extent.width * block_extent.height * block_extent.depth;
    const uint32_t texels = width * height * depth;
    const uint32_t blocks = texels / texels_per_block;

    return blocks * block_size;
}

void Texture::initResources() {
    checkGpuSupport();

    const auto imageCreateInfo = defaultImageCreateInfo();
    image = m_ctx->getDevice().createImage(imageCreateInfo);

    const auto memReqs = m_ctx->getDevice().getImageMemoryRequirements(image);
    const auto memType = getMemoryType(*m_ctx, memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    const vk::MemoryAllocateInfo memAllocInfo(memReqs.size, memType);
    deviceMemory = m_ctx->getDevice().allocateMemory(memAllocInfo);
    m_ctx->getDevice().bindImageMemory(image, deviceMemory, 0);

    sampler = m_ctx->getDevice().createSampler(defaultSamplerCreateInfo());

    // Create image view

    view = m_ctx->getDevice().createImageView(defaultCreateImageViewInfo());

    // Fill image descriptor image info to be used descriptor set setup
    descriptor.imageLayout = vk::ImageLayout::eUndefined;
    descriptor.imageView = view;
    descriptor.sampler = sampler;

    setName(m_label);
}

vk::ImageCreateInfo Texture::defaultImageCreateInfo() const {
    const vk::Extent3D extent{width, height, depth};
    const auto arrayLayers = 1;
    const auto imageType = lookupImageType[static_cast<TextureDimensionIndex>(dims)];

    const auto sharingMode = queues.size() > 1 ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive;

    vk::ImageCreateInfo imgCreateInfo(vk::ImageCreateFlags(), imageType, format, extent, mipLevels, arrayLayers, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, usage, sharingMode);

    if (sharingMode == vk::SharingMode::eConcurrent) {
        imgCreateInfo.queueFamilyIndexCount = queues.size();
        imgCreateInfo.pQueueFamilyIndices = queues.data();
    }

    return imgCreateInfo;
}

vk::ImageMemoryBarrier Texture::queueOwnershipTransfer(uint32_t fromQueueFamilyIndex, vk::AccessFlagBits srcAccess, uint32_t toQueueFamilyIndex, vk::AccessFlagBits dstAccess,
                                                       vk::ImageLayout transitionToLayout) const {

    // const bool hasQueueTransition = fromQueueFamilyIndex != toQueueFamilyIndex;
    // const bool hasLayoutTransition = transitionToLayout != descriptor.imageLayout;

    // if(!hasQueueTransition && hasLayoutTransition) {
    //  the barrier is at most an execution barrier, ignore it.
    // return;
    //}

    vk::ImageMemoryBarrier imageMemoryBarrier;
    imageMemoryBarrier.oldLayout = descriptor.imageLayout;
    imageMemoryBarrier.newLayout = transitionToLayout;
    imageMemoryBarrier.image = image;
    imageMemoryBarrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    imageMemoryBarrier.srcAccessMask = srcAccess;
    imageMemoryBarrier.dstAccessMask = dstAccess;
    imageMemoryBarrier.srcQueueFamilyIndex = fromQueueFamilyIndex;
    imageMemoryBarrier.dstQueueFamilyIndex = toQueueFamilyIndex;
    return imageMemoryBarrier;
}

vk::ImageViewCreateInfo Texture::defaultCreateImageViewInfo() const {
    const auto imageViewType = lookupImageViewType[static_cast<TextureDimensionIndex>(dims)];

    vk::ImageViewCreateInfo viewCreateInfo{};
    viewCreateInfo.image = image;
    viewCreateInfo.viewType = imageViewType;
    viewCreateInfo.format = format;
    viewCreateInfo.components = {vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA};
    viewCreateInfo.subresourceRange.aspectMask = aspectMask;
    viewCreateInfo.subresourceRange.baseMipLevel = 0;
    viewCreateInfo.subresourceRange.baseArrayLayer = 0;
    viewCreateInfo.subresourceRange.layerCount = 1;
    viewCreateInfo.subresourceRange.levelCount = 1;

    return viewCreateInfo;
}

vk::SamplerCreateInfo Texture::defaultSamplerCreateInfo() const {
    vk::SamplerCreateInfo samplerCreateInfo{};

    const auto formatProps = m_ctx->getPhysicalDevice().getFormatProperties(format);
    const bool supportsLinear = static_cast<bool>(formatProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear);

    samplerCreateInfo.magFilter = supportsLinear ? vk::Filter::eLinear : vk::Filter::eNearest;
    samplerCreateInfo.minFilter = supportsLinear ? vk::Filter::eLinear : vk::Filter::eNearest;
    samplerCreateInfo.mipmapMode = supportsLinear ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest;
    samplerCreateInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerCreateInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerCreateInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    samplerCreateInfo.mipLodBias = 0.0f;
    samplerCreateInfo.compareOp = vk::CompareOp::eNever;
    samplerCreateInfo.minLod = 0.0f;
    samplerCreateInfo.maxLod = 0.0f;
    samplerCreateInfo.maxAnisotropy = 1.0;
    samplerCreateInfo.anisotropyEnable = false;
    samplerCreateInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;

    return samplerCreateInfo;
}

void Texture::checkGpuSupport() const {
    vk::FormatProperties formatProperties = m_ctx->getPhysicalDevice().getFormatProperties(format);

    // Check if format supports transfer
    if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eTransferDst)) {
        throw std::runtime_error("Device does not support flag TRANSFER_DST for selected texture format");
    }

    // Check if GPU supports requested 3D texture dimensions
    if (dims == TextureDimensions::e3D) {
        uint32_t maxImageDimension3D(m_ctx->getPhysicalDevice().getProperties().limits.maxImageDimension3D);
        if (width > maxImageDimension3D || height > maxImageDimension3D || depth > maxImageDimension3D) {
            std::ostringstream err;
            err << "Requested texture dimensions  " << width << "x" << height << "x" << depth << " are greater than supported 2D texture dimension " << maxImageDimension3D << "x"
                << maxImageDimension3D << "x" << maxImageDimension3D;
            throw std::runtime_error(err.str());
        }
    } else {
        uint32_t maxImageDimension2D(m_ctx->getPhysicalDevice().getProperties().limits.maxImageDimension2D);
        if (width > maxImageDimension2D || height > maxImageDimension2D) {
            std::ostringstream err;
            err << "Requested texture dimensions  " << width << "x" << height << " are greater than supported 2D texture dimension " << maxImageDimension2D << "x" << maxImageDimension2D;
            throw std::runtime_error(err.str());
        }
    }
}

void Texture::capture(vk::CommandBuffer commandBuffer, vvv::Buffer const &stagingBuffer, vk::PipelineStageFlags destinationStage) {

    // there is nothing that prevents us from supporting more usage types. e.g. sampled buffers could be read using a
    // blit pass to a staging buffer with eTransferSrc set.
    // if (!(usage & vk::ImageUsageFlagBits::eTransferSrc)) {
    //    throw std::runtime_error("texture does not support transfer");
    //}
    // copy the image to the staging buffer

    const auto originalLayout = descriptor.imageLayout;
    setImageLayout(commandBuffer, vk::ImageLayout::eTransferSrcOptimal);

    vk::BufferImageCopy copyRegion(0, width, height, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), vk::Offset3D(0, 0, 0), vk::Extent3D(width, height, depth));
    // commandBuffer.copyImageToBuffer(image, stagingBuffer.textureBuffer, vk::ImageLayout::eTransferSrcOptimal, copyRegion);
    commandBuffer.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, stagingBuffer.getBuffer(), {copyRegion});
    setImageLayout(commandBuffer, originalLayout, destinationStage);
}

void Texture::upload(vk::CommandBuffer commandBuffer, vvv::Buffer const &staging, const void *const rawData, vk::ImageLayout destinationImageLayout, vk::PipelineStageFlags destinationStage) {

    ensureResources();

    const auto device = m_ctx->getDevice();

    vk::MemoryRequirements memoryRequirements = device.getBufferMemoryRequirements(staging.getBuffer());
    void *data = device.mapMemory(staging.getMemory(), 0, memoryRequirements.size, vk::MemoryMapFlags());

    // instead of loading into CPU memory using `Volume` + memcpy, could also just load the data directly into this
    const auto memSizeTexture = memorySize();
    std::memcpy(data, rawData, memSizeTexture);

    device.unmapMemory(staging.getMemory());

    setImageLayout(commandBuffer, vk::ImageLayout::eTransferDstOptimal);

    vk::BufferImageCopy copyRegion(0, width, height, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), vk::Offset3D(0, 0, 0), vk::Extent3D(width, height, depth));
    commandBuffer.copyBufferToImage(staging.getBuffer(), image, vk::ImageLayout::eTransferDstOptimal, copyRegion);
    // Set the layout for the texture image from eTransferDstOptimal to SHADER_READ_ONLY
    if (destinationImageLayout != vk::ImageLayout::eTransferDstOptimal) {
        setImageLayout(commandBuffer, destinationImageLayout, destinationStage);
    }
}

std::pair<vvv::AwaitableHandle, std::shared_ptr<vvv::Buffer>> Texture::upload(const void *const rawData, vk::ImageLayout destinationImageLayout, vk::PipelineStageFlags destinationStage,
                                                                              vvv::detail::OpenGLStyleSubmitOptions opts) {
    auto staging = std::make_shared<vvv::Buffer>(m_ctx, vvv::BufferSettings{
                                                            .label = "staging" + (m_label != "" ? "(" + m_label + ")" : ""),
                                                            .byteSize = memorySize(),
                                                        });

    auto awaitable = m_ctx->executeCommands([&](vk::CommandBuffer commandBuffer) { upload(commandBuffer, *staging, rawData, destinationImageLayout, vk::PipelineStageFlagBits::eAllCommands); }, opts);

    return {awaitable, staging};
}

} // namespace vvv