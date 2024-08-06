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

#include <vvv/vk/debug_marker.hpp>

const std::string vvv::DebugMarkerExt::ExtensionName = VK_EXT_DEBUG_MARKER_EXTENSION_NAME;
const std::string vvv::DebugUtilsExt::ExtensionName = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

void vvv::DebugMarkerExt::enable(GpuContextRwPtr ctx) {
    if (isEnabled()) {
        return;
    }

    extensionPresent = ctx->hasDeviceExtension(ExtensionName);
    device = ctx->getDevice();

    if (extensionPresent) {
        // The debug marker extension is not part of the core, so function pointers need to be loaded manually
        vkDebugMarkerSetObjectTag = reinterpret_cast<PFN_vkDebugMarkerSetObjectTagEXT>(ctx->getDeviceFunction("vkDebugMarkerSetObjectTagEXT"));
        vkDebugMarkerSetObjectName = reinterpret_cast<PFN_vkDebugMarkerSetObjectNameEXT>(ctx->getDeviceFunction("vkDebugMarkerSetObjectNameEXT"));
        vkCmdDebugMarkerBegin = reinterpret_cast<PFN_vkCmdDebugMarkerBeginEXT>(ctx->getDeviceFunction("vkCmdDebugMarkerBeginEXT"));
        vkCmdDebugMarkerEnd = reinterpret_cast<PFN_vkCmdDebugMarkerEndEXT>(ctx->getDeviceFunction("vkCmdDebugMarkerEndEXT"));
        vkCmdDebugMarkerInsert = reinterpret_cast<PFN_vkCmdDebugMarkerInsertEXT>(ctx->getDeviceFunction("vkCmdDebugMarkerInsertEXT"));
        // Set flag if at least one function pointer is present
        active = (vkDebugMarkerSetObjectName != nullptr);
    }
}

void vvv::DebugMarkerExt::setObjectName(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, const char *name) const {
    if (active) {
        VkDebugMarkerObjectNameInfoEXT nameInfo = {};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = objectType;
        nameInfo.object = object;
        nameInfo.pObjectName = name;
        vkDebugMarkerSetObjectName(device, &nameInfo);
    }
}

void vvv::DebugMarkerExt::setObjectTag(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, uint64_t name, size_t tagSize, const void *tag) const {
    if (active) {
        VkDebugMarkerObjectTagInfoEXT tagInfo = {};
        tagInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_TAG_INFO_EXT;
        tagInfo.objectType = objectType;
        tagInfo.object = object;
        tagInfo.tagName = name;
        tagInfo.tagSize = tagSize;
        tagInfo.pTag = tag;
        vkDebugMarkerSetObjectTag(device, &tagInfo);
    }
}

void vvv::DebugMarkerExt::beginRegion(VkCommandBuffer cmdbuffer, const char *pMarkerName, glm::vec4 color) const {
    if (active) {
        VkDebugMarkerMarkerInfoEXT markerInfo = {};
        markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
        memcpy(markerInfo.color, &color[0], sizeof(float) * 4);
        markerInfo.pMarkerName = pMarkerName;
        vkCmdDebugMarkerBegin(cmdbuffer, &markerInfo);
    }
}

void vvv::DebugMarkerExt::insert(VkCommandBuffer cmdbuffer, std::string markerName, glm::vec4 color) const {
    if (active) {
        VkDebugMarkerMarkerInfoEXT markerInfo = {};
        markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
        memcpy(markerInfo.color, &color[0], sizeof(float) * 4);
        markerInfo.pMarkerName = markerName.c_str();
        vkCmdDebugMarkerInsert(cmdbuffer, &markerInfo);
    }
}

void vvv::DebugMarkerExt::endRegion(VkCommandBuffer cmdBuffer) const {
    if (vkCmdDebugMarkerEnd) {
        vkCmdDebugMarkerEnd(cmdBuffer);
    }
}

void vvv::DebugMarkerExt::beginRegion(VkQueue queue, const char *pMarkerName, glm::vec4 color) const {
    throw std::runtime_error("vvv::DebugMarkerExt::beginRegion not supported");
}

void vvv::DebugMarkerExt::endRegion(VkQueue queue) const {
    throw std::runtime_error("vvv::DebugMarkerExt::endRegion not supported");
}

void vvv::DebugMarkerExt::insert(VkQueue queue, std::string markerName, glm::vec4 color) const {
    throw std::runtime_error("vvv::DebugMarkerExt::insert not supported");
}

void vvv::DebugUtilsExt::enable(GpuContextRwPtr ctx) {
    if (isEnabled()) {
        return;
    }

    extensionPresent = ctx->hasInstanceExtension(ExtensionName);
    device = ctx->getDevice();

    if (extensionPresent) {
        // The debug marker extension is not part of the core, so function pointers need to be loaded manually
        vkCmdBeginDebugUtilsLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(ctx->getDeviceFunction("vkCmdBeginDebugUtilsLabelEXT"));
        vkSetDebugUtilsObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(ctx->getDeviceFunction("vkSetDebugUtilsObjectNameEXT"));
        vkCmdEndDebugUtilsLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(ctx->getDeviceFunction("vkCmdEndDebugUtilsLabelEXT"));
        vkCmdInsertDebugUtilsLabel = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(ctx->getDeviceFunction("vkCmdInsertDebugUtilsLabelEXT"));
        vkQueueBeginDebugUtilsLabel = reinterpret_cast<PFN_vkQueueBeginDebugUtilsLabelEXT>(ctx->getDeviceFunction("vkQueueBeginDebugUtilsLabelEXT"));
        vkQueueEndDebugUtilsLabel = reinterpret_cast<PFN_vkQueueEndDebugUtilsLabelEXT>(ctx->getDeviceFunction("vkQueueEndDebugUtilsLabelEXT"));
        vkQueueInsertDebugUtilsLabel = reinterpret_cast<PFN_vkQueueInsertDebugUtilsLabelEXT>(ctx->getDeviceFunction("vkQueueInsertDebugUtilsLabelEXT"));
        vkSetDebugUtilsObjectTag = reinterpret_cast<PFN_vkSetDebugUtilsObjectTagEXT>(ctx->getDeviceFunction("vkSetDebugUtilsObjectTagEXT"));

        // Set flag if at least one function pointer is present
        active = (vkSetDebugUtilsObjectName != nullptr);
    }
}

void vvv::DebugUtilsExt::setObjectTag(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, uint64_t name, size_t tagSize, const void *tag) const {
    if (active) {
        VkDebugUtilsObjectTagInfoEXT info = {VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_TAG_INFO_EXT};
        info.objectType = convert_VkDebugReportObjectTypeEXT_to_VkObjectType(objectType);
        info.objectHandle = object;
        info.tagName = 1;
        info.tagSize = tagSize;
        info.pTag = tag;
        vkSetDebugUtilsObjectTag(device, &info);
    }
}

void vvv::DebugUtilsExt::setObjectName(VkDevice device, uint64_t object, VkDebugReportObjectTypeEXT objectType, const char *name) const {
    if (active) {
        VkDebugUtilsObjectNameInfoEXT nameInfo = {VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
        nameInfo.objectType = convert_VkDebugReportObjectTypeEXT_to_VkObjectType(objectType);
        nameInfo.objectHandle = object;
        nameInfo.pObjectName = name;
        vkSetDebugUtilsObjectName(device, &nameInfo);
    }
}

void vvv::DebugUtilsExt::beginRegion(VkCommandBuffer cmdbuffer, const char *pMarkerName, glm::vec4 color) const {
    if (active) {
        VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = pMarkerName;
        memcpy(label.color, &color[0], sizeof(float) * 4);
        vkCmdBeginDebugUtilsLabel(cmdbuffer, &label);
    }
}

void vvv::DebugUtilsExt::endRegion(VkCommandBuffer cmdBuffer) const {
    if (vkCmdEndDebugUtilsLabel) {
        vkCmdEndDebugUtilsLabel(cmdBuffer);
    }
}

void vvv::DebugUtilsExt::insert(VkCommandBuffer cmdbuffer, std::string markerName, glm::vec4 color) const {
    if (active) {
        VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = markerName.c_str();
        label.color[0] = color[0];
        label.color[1] = color[1];
        label.color[2] = color[2];
        label.color[3] = color[3];
        vkCmdInsertDebugUtilsLabel(cmdbuffer, &label);
    }
}

void vvv::DebugUtilsExt::beginRegion(VkQueue queue, const char *pMarkerName, glm::vec4 color) const {
    if (active) {
        VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = pMarkerName;
        memcpy(label.color, &color[0], sizeof(float) * 4);
        vkQueueBeginDebugUtilsLabel(queue, &label);
    }
}

void vvv::DebugUtilsExt::endRegion(VkQueue queue) const {
    if (vkQueueEndDebugUtilsLabel) {
        vkQueueEndDebugUtilsLabel(queue);
    }
}

void vvv::DebugUtilsExt::insert(VkQueue queue, std::string markerName, glm::vec4 color) const {
    if (active) {
        VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = markerName.c_str();
        label.color[0] = color[0];
        label.color[1] = color[1];
        label.color[2] = color[2];
        label.color[3] = color[3];
        vkQueueInsertDebugUtilsLabel(queue, &label);
    }
}