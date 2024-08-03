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