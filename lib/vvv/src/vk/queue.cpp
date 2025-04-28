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

#include <vvv/vk/queue.hpp>

#include <vvv/util/Logger.hpp>

uint32_t getQueueFamilyIndex(const std::vector<vk::QueueFamilyProperties> &queueFamilyProperties, vk::QueueFlags queueFlags) {
    // Dedicated queue for compute
    // Try to find a queue family index that supports compute but not graphics
    if (queueFlags & vk::QueueFlagBits::eCompute) {
        for (uint32_t i = 0; i < queueFamilyProperties.size(); i++) {
            if ((queueFamilyProperties[i].queueFlags & queueFlags) && !(queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics)) {
                return i;
            }
        }
    }

    // Dedicated queue for transfer
    // Try to find a queue family index that supports transfer but not graphics and compute
    if (queueFlags & vk::QueueFlagBits::eTransfer) {
        for (uint32_t i = 0; i < queueFamilyProperties.size(); i++) {
            if ((queueFamilyProperties[i].queueFlags & queueFlags) && !(queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
                !(queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eCompute)) {
                return i;
            }
        }
    }

    // For other queue types or if no separate compute queue is present, return the first one to support the requested flags
    for (uint32_t i = 0; i < queueFamilyProperties.size(); i++) {
        if (queueFamilyProperties[i].queueFlags & queueFlags) {
            return i;
        }
    }

    throw std::runtime_error("Could not find a matching queue family index");
}

uint32_t createGraphicsQueues(std::vector<vk::QueueFamilyProperties> const &queueFamilyProperties, vk::PhysicalDevice physicalDevice, std::vector<vk::DeviceQueueCreateInfo> *queueCreateInfos) {

    uint32_t graphicsQueueFamilyIndex = getQueueFamilyIndex(queueFamilyProperties, vk::QueueFlagBits::eGraphics);

    if (queueCreateInfos != nullptr) {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &defaultQueuePriority;
        queueCreateInfos->emplace_back(queueInfo);
    }

    return graphicsQueueFamilyIndex;
}

/// Create a present and graphics queue, preferring a single queue that can do both
std::pair<uint32_t, uint32_t> createGraphicsQueues(std::vector<vk::QueueFamilyProperties> const &queueFamilyProperties, vk::PhysicalDevice physicalDevice, vk::SurfaceKHR const &surface,
                                                   std::vector<vk::DeviceQueueCreateInfo> *queueCreateInfos) {
    assert(queueFamilyProperties.size() < std::numeric_limits<uint32_t>::max());

    for (size_t i = 0; i < queueFamilyProperties.size(); i++) {
        if ((queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics) && physicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(i), surface)) {

            if (queueCreateInfos != nullptr) {
                VkDeviceQueueCreateInfo queueInfo{};
                queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueInfo.queueFamilyIndex = static_cast<uint32_t>(i);
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &defaultQueuePriority;
                queueCreateInfos->push_back(queueInfo);
            }

            return std::make_pair(static_cast<uint32_t>(i), static_cast<uint32_t>(i));
        }
    }

    uint32_t graphicsQueueFamilyIndex = getQueueFamilyIndex(queueFamilyProperties, vk::QueueFlagBits::eGraphics);

    if (queueCreateInfos != nullptr) {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &defaultQueuePriority;
        queueCreateInfos->push_back(queueInfo);
    }

    // there's nothing like a single family index that supports both graphics and present. Thus, start looking for an other family
    // index that supports present

    for (size_t i = 0; i < queueFamilyProperties.size(); i++) {
        if (physicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(i), surface)) {

            if (queueCreateInfos != nullptr) {
                VkDeviceQueueCreateInfo queueInfo{};
                queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueInfo.queueFamilyIndex = static_cast<uint32_t>(i);
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &defaultQueuePriority;
                queueCreateInfos->push_back(queueInfo);
            }

            return std::make_pair(graphicsQueueFamilyIndex, static_cast<uint32_t>(i));
        }
    }

    throw std::runtime_error("Either no present queue available");
}

/// Create dedicated queues for compute and transfer if available. Then try to match the default (graphics) queue. otherwise terminate.
vvv::QueueFamilyIndices findQueueFamilyIndices(vk::QueueFlags requestedQueueTypes, std::vector<vk::QueueFamilyProperties> const &familyProps, std::vector<vk::DeviceQueueCreateInfo> *queueCreateInfos,
                                               uint32_t defaultGraphicsQueue) {

    // Just as some guidance, a RTX 2070 reports the following queues:
    //
    // ```
    // 0: 16 { Graphics | Compute | Transfer | SparseBinding } 1x1x1
    // 1: 2 { Transfer | SparseBinding } 1x1x1
    // 2: 8 { Compute | Transfer | SparseBinding } 1x1x1
    // ```
    //
    // ```
    //    for (int i = 0; i < familyCount; ++i) {
    //        const vk::QueueFamilyProperties family(familyProps[i]);
    //        std::cout << i << ": " << family.queueCount << " " << to_string(family.queueFlags) << std::endl; }
    // ```

    // Get queue family indices for the requested queue family types
    // Note that the indices may overlap depending on the implementation

    vvv::QueueFamilyIndices queueFamilyIndices;

    // Graphics queue
    if (requestedQueueTypes & vk::QueueFlagBits::eGraphics) {
        queueFamilyIndices.graphics = getQueueFamilyIndex(familyProps, vk::QueueFlagBits::eGraphics);

        if (queueCreateInfos != nullptr && queueFamilyIndices.graphics != defaultGraphicsQueue) {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = queueFamilyIndices.graphics.value();
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &defaultQueuePriority;
            queueCreateInfos->push_back(queueInfo);
        }
    } else {
        assert(familyProps[defaultGraphicsQueue].queueFlags & vk::QueueFlagBits::eGraphics);
        queueFamilyIndices.graphics = defaultGraphicsQueue;
    }

    // Dedicated compute queue
    if (requestedQueueTypes & vk::QueueFlagBits::eCompute) {
        queueFamilyIndices.compute = getQueueFamilyIndex(familyProps, vk::QueueFlagBits::eCompute);
        if (queueCreateInfos != nullptr && queueFamilyIndices.compute != queueFamilyIndices.graphics) {
            // If compute family index differs, we need an additional queue create info for the compute queue
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = queueFamilyIndices.compute.value();
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &defaultQueuePriority;
            queueCreateInfos->push_back(queueInfo);
        }
    } else {
        // Else we use the same queue
        assert(familyProps[queueFamilyIndices.graphics.value()].queueFlags & vk::QueueFlagBits::eCompute);
        queueFamilyIndices.compute = queueFamilyIndices.graphics;
    }

    // Dedicated transfer queue
    if (requestedQueueTypes & vk::QueueFlagBits::eTransfer) {
        queueFamilyIndices.transfer = getQueueFamilyIndex(familyProps, vk::QueueFlagBits::eTransfer);
        if (queueCreateInfos != nullptr && (queueFamilyIndices.transfer != queueFamilyIndices.graphics) && (queueFamilyIndices.transfer != queueFamilyIndices.compute)) {
            // If compute family index differs, we need an additional queue create info for the compute queue
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = queueFamilyIndices.transfer.value();
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &defaultQueuePriority;
            queueCreateInfos->push_back(queueInfo);
        }
    } else {
        // Else we use the same queue
        assert(familyProps[queueFamilyIndices.graphics.value()].queueFlags & vk::QueueFlagBits::eTransfer);
        queueFamilyIndices.transfer = queueFamilyIndices.graphics;
    }

    return queueFamilyIndices;
}

vvv::QueueFamilyIndices findQueueFamilyIndices(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR const &surface, std::vector<vk::DeviceQueueCreateInfo> *queueCreateInfos) {
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
    assert(queueFamilyProperties.size() < std::numeric_limits<uint32_t>::max());

    vvv::QueueFamilyIndices familyIndices;

    if (surface == static_cast<vk::SurfaceKHR>(nullptr)) {
        familyIndices.graphics = createGraphicsQueues(queueFamilyProperties, physicalDevice, queueCreateInfos);
    } else {
        // this can be generalized to a createJointQueue() and createAnyQueue()/createFirstQueue
        const auto [graphics, present] = createGraphicsQueues(queueFamilyProperties, physicalDevice, surface, queueCreateInfos);

        familyIndices.graphics = graphics;
        familyIndices.present = present;
    }

    // this can be simplified to a createDedicatedQueue()
    const auto dedicated = findQueueFamilyIndices(vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer, queueFamilyProperties, queueCreateInfos, familyIndices.graphics.value());
    familyIndices.compute = dedicated.compute;
    familyIndices.transfer = dedicated.transfer;

    for (int i = 0; i < queueFamilyProperties.size(); i++) {
        auto flags = queueFamilyProperties[i].queueFlags;
        vvv::Logger(vvv::Debug) << "Queue Family " << i << ": "
                                << (flags & vk::QueueFlagBits::eGraphics ? "graphics " : "") << (flags & vk::QueueFlagBits::eCompute ? "compute " : "")
                                << (flags & vk::QueueFlagBits::eTransfer ? "transfer " : "") << (flags & vk::QueueFlagBits::eSparseBinding ? "sparse_binding " : "")
                                << (flags & vk::QueueFlagBits::eProtected ? "protected " : "")
                                << (i == familyIndices.graphics ? "(graphics queue) " : "") << (i == familyIndices.present ? "(present queue) " : "")
                                << (i == familyIndices.compute ? "(compute queue) " : "") << (i == familyIndices.transfer ? "(transfer queue) " : "");
    }

    return familyIndices;
}