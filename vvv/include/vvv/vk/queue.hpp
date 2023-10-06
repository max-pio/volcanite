#pragma once
#include <vvv/core/preamble.hpp>

const float defaultQueuePriority(0.0f);

uint32_t getQueueFamilyIndex(const std::vector<vk::QueueFamilyProperties> &queueFamilyProperties, vk::QueueFlags queueFlags);

uint32_t createGraphicsQueues(std::vector<vk::QueueFamilyProperties> const &queueFamilyProperties, vk::PhysicalDevice physicalDevice, std::vector<vk::DeviceQueueCreateInfo> *queueCreateInfos);

/*! Create a present and graphics queue, preferring a single queue that can do both */
std::pair<uint32_t, uint32_t> createGraphicsQueues(std::vector<vk::QueueFamilyProperties> const &queueFamilyProperties, vk::PhysicalDevice physicalDevice, vk::SurfaceKHR const &surface,
                                                   std::vector<vk::DeviceQueueCreateInfo> *queueCreateInfos = nullptr);

/*! Create dedicated queues for compute and transfer if available. Then try to match the default (graphics) queue. otherwise terminate. */
vvv::QueueFamilyIndices findQueueFamilyIndices(vk::QueueFlags requestedQueueTypes, std::vector<vk::QueueFamilyProperties> const &familyProps,
                                               std::vector<vk::DeviceQueueCreateInfo> *queueCreateInfos = nullptr, uint32_t defaultGraphicsQueue = 0);

vvv::QueueFamilyIndices findQueueFamilyIndices(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR const &surface, std::vector<vk::DeviceQueueCreateInfo> *queueCreateInfos = nullptr);