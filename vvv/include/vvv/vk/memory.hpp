#pragma once

#include "vvv/core/GpuContext.hpp"
#include "vvv/vk/destroy.hpp"
#include <vulkan/vulkan.hpp>

namespace vvv {

/**
 * Get the index of a memory type that has all the requested property bits set
 *
 * @param typeBits Bit mask with bits set for each memory type supported by the resource to request for (from VkMemoryRequirements)
 * @param properties Bit mask of properties for the memory type to request
 * @param (Optional) memTypeFound Pointer to a bool that is set to true if a matching memory type has been found
 *
 * @return Index of the requested memory type
 *
 * @throw Throws an exception if memTypeFound is null and no memory type could be found that supports the requested properties
 */

uint32_t getMemoryType(vk::PhysicalDeviceMemoryProperties const &memoryProperties, uint32_t typeBits, vk::MemoryPropertyFlags requirementsMask);
uint32_t getMemoryType(vvv::GpuContextRef ctx, uint32_t typeBits, vk::MemoryPropertyFlags properties);
uint32_t getMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeBits, vk::MemoryPropertyFlags properties);

void setImageLayout(vk::CommandBuffer const &commandBuffer, vk::Image image, vk::Format format, vk::ImageLayout oldImageLayout, vk::ImageLayout newImageLayout,
                    vk::PipelineStageFlags destinationStage = vk::PipelineStageFlags());

// TODO(Reiner): there is quite some code in the codebase that is not yet using this helper
std::pair<vk::Buffer, vk::DeviceMemory> createBuffer(vvv::GpuContextRef ctx, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, const char *label = nullptr);

} // namespace vvv
