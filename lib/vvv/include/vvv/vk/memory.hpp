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

#include "vvv/core/GpuContext.hpp"
#include <vulkan/vulkan.hpp>

namespace vvv {

/// Get the index of a memory type that has all the requested property bits set
/// @param typeBits Bit mask with bits set for each memory type supported by the resource to request for (from VkMemoryRequirements)
/// @param properties Bit mask of properties for the memory type to request
/// @param (Optional) memTypeFound Pointer to a bool that is set to true if a matching memory type has been found
/// @return Index of the requested memory type
/// @throw Throws an exception if memTypeFound is null and no memory type could be found that supports the requested properties
uint32_t getMemoryType(vk::PhysicalDeviceMemoryProperties const &memoryProperties, uint32_t typeBits, vk::MemoryPropertyFlags requirementsMask);
uint32_t getMemoryType(vvv::GpuContextRef ctx, uint32_t typeBits, vk::MemoryPropertyFlags properties);
uint32_t getMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeBits, vk::MemoryPropertyFlags properties);
size_t getMemoryHeapSize(vvv::GpuContextRef ctx, vk::MemoryHeapFlagBits requirementMask = vk::MemoryHeapFlagBits::eDeviceLocal);
std::pair<size_t, size_t> getMemoryHeapBudgetAndUsage(vvv::GpuContextRef ctx, vk::MemoryHeapFlagBits requirementMask = vk::MemoryHeapFlagBits::eDeviceLocal);

void setImageLayout(vk::CommandBuffer const &commandBuffer, vk::Image image, vk::Format format, vk::ImageLayout oldImageLayout, vk::ImageLayout newImageLayout,
                    vk::PipelineStageFlags destinationStage = vk::PipelineStageFlags());

std::pair<vk::Buffer, vk::DeviceMemory> createBuffer(vvv::GpuContextRef ctx, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, const char *label = nullptr);

} // namespace vvv
