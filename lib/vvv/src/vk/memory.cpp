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

#include "vvv/vk/memory.hpp"

/// Get the index of a memory type that has all the requested property bits set
/// @param typeBits Bit mask with bits set for each memory type supported by the resource to request for (from VkMemoryRequirements)
/// @param properties Bit mask of properties for the memory type to request
/// @param (Optional) memTypeFound Pointer to a bool that is set to true if a matching memory type has been found
/// @return Index of the requested memory type
/// @throw Throws an exception if memTypeFound is null and no memory type could be found that supports the requested properties
uint32_t vvv::getMemoryType(vk::PhysicalDeviceMemoryProperties const &memoryProperties, uint32_t typeBits, vk::MemoryPropertyFlags requirementsMask) {
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        if ((typeBits & 1) == 1) {
            if ((memoryProperties.memoryTypes[i].propertyFlags & requirementsMask) == requirementsMask) {
                return i;
            }
        }
        typeBits >>= 1;
    }

    throw std::runtime_error("Could not find a matching memory type");
}

uint32_t vvv::getMemoryType(vvv::GpuContextRef ctx, uint32_t typeBits, vk::MemoryPropertyFlags properties) {
    const auto memoryProperties = ctx.getPhysicalDevice().getMemoryProperties();
    return getMemoryType(memoryProperties, typeBits, properties);
}

uint32_t vvv::getMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeBits, vk::MemoryPropertyFlags properties) {
    const auto memoryProperties = physicalDevice.getMemoryProperties();
    return getMemoryType(memoryProperties, typeBits, properties);
}

size_t vvv::getMemoryHeapSize(vvv::GpuContextRef ctx, vk::MemoryHeapFlagBits requirementMask) {
    // See https://asawicki.info/news_1740_vulkan_memory_types_on_pc_and_how_to_use_them for a description of vendor
    // specific memory types. Keep in mind that these methods do not work 100% of the time, as there are so many nuances
    // in terms of vendor memory implementation and access.

    size_t total_heap_memory = 0ul;
    const auto memoryProperties = ctx.getPhysicalDevice().getMemoryProperties();
    for (uint32_t heap_idx = 0; heap_idx < memoryProperties.memoryHeapCount; heap_idx++) {
        if (memoryProperties.memoryHeaps[heap_idx].flags & requirementMask) {
            total_heap_memory += memoryProperties.memoryHeaps[heap_idx].size;
        }
    }

    if (total_heap_memory == 0ul)
        throw std::runtime_error("Could not find a matching memory heap");
    return total_heap_memory;
}

/// Obtains the total heap memory size that this application can use as well as the currently used heap memory size.
/// The remaining available heap memory size is (total size - used size). Both numbers are reported as a number of bytes.
/// @returns pair of the total heap memory in bytes and the currently used heap memory in bytes. */
std::pair<size_t, size_t> vvv::getMemoryHeapBudgetAndUsage(vvv::GpuContextRef ctx, vk::MemoryHeapFlagBits requirementMask) {
    // See https://asawicki.info/news_1740_vulkan_memory_types_on_pc_and_how_to_use_them for a description of vendor
    // specific memory types. Keep in mind that these methods do not work 100% of the time, as there are so many nuances
    // in terms of vendor memory implementation and access.

    vk::PhysicalDeviceMemoryBudgetPropertiesEXT budgetProperties;
    vk::PhysicalDeviceMemoryProperties2 memoryProperties2;
    memoryProperties2.pNext = &budgetProperties;

    if (ctx.hasDeviceExtension("VK_EXT_memory_budget")) {
        ctx.getPhysicalDevice().getMemoryProperties2(&memoryProperties2);
    } else {
        throw std::runtime_error("Could not query video heap budget and usage because VK_EXT_memory_budget vulkan extension is missing/not enabled");
    }

    size_t budget_heap_memory = 0ul;
    size_t used_heap_memory = 0ul;

    const auto memoryProperties = ctx.getPhysicalDevice().getMemoryProperties();
    for (uint32_t heap_idx = 0; heap_idx < memoryProperties.memoryHeapCount; heap_idx++) {
        if (memoryProperties.memoryHeaps[heap_idx].flags & requirementMask) {
            budget_heap_memory += budgetProperties.heapBudget[heap_idx];
            used_heap_memory += budgetProperties.heapUsage[heap_idx];
        }
    }

    if (budget_heap_memory == 0ul)
        throw std::runtime_error("Could not find a matching memory heap");

    return std::make_pair(budget_heap_memory, used_heap_memory);
}

// using code from https://github.com/KhronosGroup/Vulkan-Hpp/blob/6d5d6661f39b7162027ad6f75d4d2e902eac4d55/samples/utils/utils.cpp
// released under the Apache License 2.0
// another implementation available on the web is: https://github.com/nvpro-samples/nvpro_core/blob/f2c05e161bba9ab9a8c96c0173bf0edf7c168dfa/nvvk/images_vk.cpp#L108-L116
void vvv::setImageLayout(vk::CommandBuffer const &commandBuffer, vk::Image image, vk::Format format, vk::ImageLayout oldImageLayout, vk::ImageLayout newImageLayout,
                         vk::PipelineStageFlags destinationStage) {
    vk::AccessFlags sourceAccessMask;
    switch (oldImageLayout) {
    case vk::ImageLayout::eTransferDstOptimal:
        sourceAccessMask = vk::AccessFlagBits::eTransferWrite;
        break;
    case vk::ImageLayout::eTransferSrcOptimal:
        sourceAccessMask = vk::AccessFlagBits::eTransferRead;
        break;
    case vk::ImageLayout::ePreinitialized:
        sourceAccessMask = vk::AccessFlagBits::eHostWrite;
        break;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        sourceAccessMask = vk::AccessFlagBits::eShaderRead;
        break;
    case vk::ImageLayout::eColorAttachmentOptimal:
        sourceAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        break;
    case vk::ImageLayout::eDepthStencilAttachmentOptimal:
        sourceAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        break;
    case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
        sourceAccessMask = vk::AccessFlagBits::eShaderRead;
        break;
    case vk::ImageLayout::eGeneral: // sourceAccessMask is empty
    case vk::ImageLayout::eUndefined:
        break;
    default:
        assert(false);
        break;
    }

    vk::PipelineStageFlags sourceStage;
    switch (oldImageLayout) {
    case vk::ImageLayout::eGeneral:
    case vk::ImageLayout::ePreinitialized:
        sourceStage = vk::PipelineStageFlagBits::eHost;
        break;
    case vk::ImageLayout::eTransferDstOptimal:
    case vk::ImageLayout::eTransferSrcOptimal:
        sourceStage = vk::PipelineStageFlagBits::eTransfer;
        break;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        sourceStage = vk::PipelineStageFlagBits::eAllCommands; // return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        break;
    case vk::ImageLayout::eColorAttachmentOptimal:
        sourceStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        break;
    case vk::ImageLayout::eDepthStencilAttachmentOptimal:
        sourceStage = vk::PipelineStageFlagBits::eLateFragmentTests;
        break;
    case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
        sourceStage = vk::PipelineStageFlagBits::eAllCommands;
        break;
    case vk::ImageLayout::eUndefined:
        sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
        break;
    default:
        assert(false);
        break;
    }

    vk::AccessFlags destinationAccessMask;
    switch (newImageLayout) {
    case vk::ImageLayout::eColorAttachmentOptimal:
        destinationAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        break;
    case vk::ImageLayout::eDepthStencilAttachmentOptimal:
        destinationAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        break;
    case vk::ImageLayout::eGeneral: // empty destinationAccessMask
    case vk::ImageLayout::ePresentSrcKHR:
        break;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
    case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
        destinationAccessMask = vk::AccessFlagBits::eShaderRead;
        break;
    case vk::ImageLayout::eTransferSrcOptimal:
        destinationAccessMask = vk::AccessFlagBits::eTransferRead;
        break;
    case vk::ImageLayout::eTransferDstOptimal:
        destinationAccessMask = vk::AccessFlagBits::eTransferWrite;
        break;
    default:
        assert(false);
        break;
    }

    if (destinationStage == vk::PipelineStageFlags()) {
        switch (newImageLayout) {
        case vk::ImageLayout::eColorAttachmentOptimal:
            destinationStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            break;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            destinationStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
            break;
        case vk::ImageLayout::eGeneral:
            destinationStage = vk::PipelineStageFlagBits::eHost;
            break;
        case vk::ImageLayout::ePresentSrcKHR:
            destinationStage = vk::PipelineStageFlagBits::eBottomOfPipe;
            break;
        case vk::ImageLayout::eShaderReadOnlyOptimal:
        case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
            destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
            break;
        case vk::ImageLayout::eTransferDstOptimal:
        case vk::ImageLayout::eTransferSrcOptimal:
            destinationStage = vk::PipelineStageFlagBits::eTransfer;
            break;
        default:
            assert(false);
            break;
        }
    }

    vk::ImageAspectFlags aspectMask;
    if (newImageLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal || newImageLayout == vk::ImageLayout::eDepthStencilReadOnlyOptimal) {

        aspectMask = vk::ImageAspectFlagBits::eDepth;
        if (format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint) {
            aspectMask |= vk::ImageAspectFlagBits::eStencil;
        }
    } else {
        aspectMask = vk::ImageAspectFlagBits::eColor;
    }

    vk::ImageSubresourceRange imageSubresourceRange(aspectMask, 0, 1, 0, 1);
    vk::ImageMemoryBarrier imageMemoryBarrier(sourceAccessMask, destinationAccessMask, oldImageLayout, newImageLayout, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, imageSubresourceRange);
    return commandBuffer.pipelineBarrier(sourceStage, destinationStage, {}, nullptr, nullptr, imageMemoryBarrier);
}

/// Create a buffer with exclusive sharing mode -- meaning the buffer has to be transferred explicitly between queues.
std::pair<vk::Buffer, vk::DeviceMemory> vvv::createBuffer(vvv::GpuContextRef ctx, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, const char *label) {
    // without this assertion, zero-sized allocations result in a out of memory error
    assert(size > 0 && "vulkan buffers MUST allocate at least one byte");

    const auto device = ctx.getDevice();
    vk::BufferCreateInfo bufferInfo({}, size, usage);

    const auto buffer = device.createBuffer(bufferInfo);
    const auto memRequirements = device.getBufferMemoryRequirements(buffer);
    const auto memoryTypeIndex = getMemoryType(ctx, memRequirements.memoryTypeBits, properties);
    vk::MemoryAllocateInfo allocInfo(memRequirements.size, memoryTypeIndex);
    const auto bufferMemory = device.allocateMemory(allocInfo);
    device.bindBufferMemory(buffer, bufferMemory, 0);

    if (label != nullptr && !std::string(label).empty()) {
        ctx.debugMarker->setName(buffer, label);
        ctx.debugMarker->setName(bufferMemory, label);
    }

    return {buffer, bufferMemory};
}