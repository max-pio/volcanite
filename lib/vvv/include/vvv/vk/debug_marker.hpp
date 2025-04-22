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
#include <glm/glm.hpp>
#include <iostream>
#include <string>
#include <vulkan/vulkan.hpp>

namespace vvv {

class GpuContext;
typedef GpuContext *const GpuContextRwPtr;

/// @brief Utilities to (a) assign names to vulkan objects and (b) to label ranges in queues and command buffers with colored labels.
///
/// Since this is simply a convenience feature for development, this class MUST NOT throw in any failure case.
/// It MUST NOT throw if enabling the extension fails and it MUST NOT throw if any marker type is not supported
/// by the particular implementation. It MUST NOT throw if any method is called without calling `enable` first.
/// In case of any failure, just don't attach the debug marker and fail silently.
///
/// note: This class still contains some Vulkan 1.0 support that is no longer required.
class DebugUtilities {
  public:
    /// Call once on startup to enable debugging. Subsequent invocations are ignored.
    virtual void enable(GpuContextRwPtr ctx) = 0;

    /// Raw object labeling function. Use the convenience methods `setName` instead.
    virtual void setObjectName(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, const char *name) const = 0;
    /// Allows annotation with any object
    virtual void setObjectTag(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, uint64_t name, size_t tagSize, const void *tag) const = 0;

    virtual void endRegion(VkCommandBuffer cmdBuffer) const = 0;
    virtual void beginRegion(VkCommandBuffer queue, const char *pMarkerName, glm::vec4 color) const = 0;
    virtual void insert(VkCommandBuffer cmdbuffer, std::string markerName, glm::vec4 color) const = 0;

    virtual void endRegion(VkQueue cmdBuffer) const = 0;
    virtual void beginRegion(VkQueue queue, const char *pMarkerName, glm::vec4 color) const = 0;
    virtual void insert(VkQueue cmdbuffer, std::string markerName, glm::vec4 color) const = 0;

    void setName(vk::Image v, std::string name) const { setObjectName(device, (uint64_t)static_cast<VkImage>(v), VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, (name + "[Image]").c_str()); }
    void setName(vk::ImageView v, std::string name) const { setObjectName(device, (uint64_t)static_cast<VkImageView>(v), VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT, (name + "[ImageView]").c_str()); }
    void setName(vk::Buffer v, std::string name) const { setObjectName(device, (uint64_t)static_cast<VkBuffer>(v), VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, (name + "[Buffer]").c_str()); }
    void setName(vk::Sampler v, std::string name) const { setObjectName(device, (uint64_t)static_cast<VkSampler>(v), VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT, (name + "[Sampler]").c_str()); }
    void setName(vk::Pipeline v, std::string name) const { setObjectName(device, (uint64_t)static_cast<VkPipeline>(v), VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, (name + "[Pipeline]").c_str()); }
    void setName(vk::Queue v, std::string name) const { setObjectName(device, (uint64_t)static_cast<VkQueue>(v), VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT, (name + "[Queue]").c_str()); }
    void setName(vk::Semaphore v, std::string name) const { setObjectName(device, (uint64_t)static_cast<VkSemaphore>(v), VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT, (name + "[Semaphore]").c_str()); }
    void setName(vk::Fence v, std::string name) const { setObjectName(device, (uint64_t)static_cast<VkFence>(v), VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT, (name + "[Fence]").c_str()); }

    void setName(vk::PipelineLayout v, std::string name) const {
        setObjectName(device, (uint64_t)static_cast<VkPipelineLayout>(v), VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT, (name + "[PipelineLayout]").c_str());
    }
    void setName(vk::Framebuffer v, std::string name) const {
        setObjectName(device, (uint64_t)static_cast<VkFramebuffer>(v), VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT, (name + "[Framebuffer]").c_str());
    }
    void setName(vk::SwapchainKHR v, std::string name) const {
        setObjectName(device, (uint64_t)static_cast<VkSwapchainKHR>(v), VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT, (name + "[SwapchainKHR]").c_str());
    }

    void setName(vk::ShaderModule v, std::string name) const {
        setObjectName(device, (uint64_t)static_cast<VkShaderModule>(v), VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT, (name + "[ShaderModule]").c_str());
    }
    void setName(vk::CommandBuffer v, std::string name) const {
        std::string label = name + "[CommandBuffer]";
        setObjectName(device, (uint64_t)static_cast<VkCommandBuffer>(v), VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT, label.c_str());
    }
    void setName(vk::DeviceMemory v, std::string name) const {
        setObjectName(device, (uint64_t)static_cast<VkDeviceMemory>(v), VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, (name + "[DeviceMemory]").c_str());
    }
    void setName(vk::CommandPool v, std::string name) const {
        setObjectName(device, (uint64_t)static_cast<VkCommandPool>(v), VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_POOL_EXT, (name + "[CommandPool]").c_str());
    }
    void setName(vk::DescriptorSet v, std::string name) const {
        setObjectName(device, (uint64_t)static_cast<VkDescriptorSet>(v), VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT, (name + "[DescriptorSet]").c_str());
    }
    void setName(vk::DescriptorPool v, std::string name) const {
        setObjectName(device, (uint64_t)static_cast<VkDescriptorPool>(v), VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT, (name + "[DescriptorPool]").c_str());
    }
    void setName(vk::DescriptorSetLayout v, std::string name) const {
        setObjectName(device, (uint64_t)static_cast<VkDescriptorSetLayout>(v), VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT, (name + "[DescriptorSetLayout]").c_str());
    }

    /// Check if the extension is enabled, resp. if calling `enable` was successful
    bool isEnabled() const { return active; }
    /// False if `enable` failed because the debug extension is not available. `false` if `enable` was not yet invoked.
    bool isExtensionSupported() const { return extensionPresent; }
    virtual std::string extensionName() const = 0;

    virtual ~DebugUtilities() {}

  protected:
    bool active = false;
    bool extensionPresent = false;
    VkDevice device = nullptr;
};

class DebugUtilsExt : public DebugUtilities {
  public:
    static const std::string ExtensionName;
    std::string extensionName() const override { return ExtensionName; }

    void enable(GpuContextRwPtr ctx) override;
    void setObjectTag(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, uint64_t name, size_t tagSize, const void *tag) const override;
    void setObjectName(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, const char *name) const override;
    void endRegion(VkCommandBuffer cmdBuffer) const override;
    void beginRegion(VkCommandBuffer queue, const char *pMarkerName, glm::vec4 color) const override;
    void insert(VkCommandBuffer cmdbuffer, std::string markerName, glm::vec4 color) const override;
    void endRegion(VkQueue cmdBuffer) const override;
    void beginRegion(VkQueue queue, const char *pMarkerName, glm::vec4 color) const override;
    void insert(VkQueue cmdbuffer, std::string markerName, glm::vec4 color) const override;

  private:
    VkObjectType convert_VkDebugReportObjectTypeEXT_to_VkObjectType(VkDebugReportObjectTypeEXT v) const {
        if (v == VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT)
            return VK_OBJECT_TYPE_SWAPCHAIN_KHR;

        // Note: this is dangerous, but the enums are almost identical
        return static_cast<VkObjectType>(v);
    }

    PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabel = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectName = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabel = nullptr;
    PFN_vkCmdInsertDebugUtilsLabelEXT vkCmdInsertDebugUtilsLabel = nullptr;
    PFN_vkQueueBeginDebugUtilsLabelEXT vkQueueBeginDebugUtilsLabel = nullptr;
    PFN_vkQueueEndDebugUtilsLabelEXT vkQueueEndDebugUtilsLabel = nullptr;
    PFN_vkQueueInsertDebugUtilsLabelEXT vkQueueInsertDebugUtilsLabel = nullptr;
    PFN_vkSetDebugUtilsObjectTagEXT vkSetDebugUtilsObjectTag = nullptr;
};

// Legacy Debug Markers for Vulkan 1.0. Use DebugUtil for later Vulkan versions.
// adapted from https://github.com/SaschaWillems/Vulkan/blob/3b1ff1eecead5933403e9115146f5c80aeef9f5e/examples/debugmarker/debugmarker.cpp
// released under an MIT license
class DebugMarkerExt : public DebugUtilities {
  public:
    static const std::string ExtensionName;
    std::string extensionName() const override { return VK_EXT_DEBUG_MARKER_EXTENSION_NAME; }

    void enable(GpuContextRwPtr ctx) override;
    void setObjectTag(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, uint64_t name, size_t tagSize, const void *tag) const override;
    void setObjectName(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, const char *name) const override;
    void endRegion(VkCommandBuffer cmdBuffer) const override;
    void beginRegion(VkCommandBuffer queue, const char *pMarkerName, glm::vec4 color) const override;
    void insert(VkCommandBuffer cmdbuffer, std::string markerName, glm::vec4 color) const override;
    void endRegion(VkQueue cmdBuffer) const override;
    void beginRegion(VkQueue queue, const char *pMarkerName, glm::vec4 color) const override;
    void insert(VkQueue cmdbuffer, std::string markerName, glm::vec4 color) const override;

  private:
    PFN_vkDebugMarkerSetObjectTagEXT vkDebugMarkerSetObjectTag = nullptr;
    PFN_vkDebugMarkerSetObjectNameEXT vkDebugMarkerSetObjectName = nullptr;
    PFN_vkCmdDebugMarkerBeginEXT vkCmdDebugMarkerBegin = nullptr;
    PFN_vkCmdDebugMarkerEndEXT vkCmdDebugMarkerEnd = nullptr;
    PFN_vkCmdDebugMarkerInsertEXT vkCmdDebugMarkerInsert = nullptr;
};

class DebugNoop : public DebugUtilities {
  public:
    std::string extensionName() const override { return ""; }

    void enable(GpuContextRwPtr ctx) override {}
    void setObjectTag(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, uint64_t name, size_t tagSize, const void *tag) const override {}
    void setObjectName(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, const char *name) const override {}
    void endRegion(VkCommandBuffer cmdBuffer) const override {}
    void beginRegion(VkCommandBuffer queue, const char *pMarkerName, glm::vec4 color) const override {}
    void insert(VkCommandBuffer cmdbuffer, std::string markerName, glm::vec4 color) const override {}
    void endRegion(VkQueue cmdBuffer) const override {}
    void beginRegion(VkQueue queue, const char *pMarkerName, glm::vec4 color) const override {}
    void insert(VkQueue cmdbuffer, std::string markerName, glm::vec4 color) const override {}
};

} // namespace vvv
