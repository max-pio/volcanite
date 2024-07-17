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
#include <vulkan/vulkan.hpp>

namespace vvv {

vk::SurfaceFormatKHR chooseSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const &formats) {
    // for (const auto &availableFormat : availableFormats) {
    //     if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
    //         return availableFormat;
    //     }
    // }

    // return availableFormats[0];

    assert(!formats.empty());
    vk::SurfaceFormatKHR pickedFormat = formats[0];
    if (formats.size() == 1) {
        if (formats[0].format == vk::Format::eUndefined) {
            pickedFormat.format = vk::Format::eB8G8R8A8Unorm;
            pickedFormat.colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        }
    } else {
        // request several formats, the first found will be used
        vk::Format requestedFormats[] = {vk::Format::eB8G8R8A8Unorm, vk::Format::eR8G8B8A8Unorm, vk::Format::eB8G8R8Unorm, vk::Format::eR8G8B8Unorm};
        vk::ColorSpaceKHR requestedColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        for (size_t i = 0; i < sizeof(requestedFormats) / sizeof(requestedFormats[0]); i++) {
            vk::Format requestedFormat = requestedFormats[i];
            auto it = std::find_if(formats.begin(), formats.end(),
                                   [requestedFormat, requestedColorSpace](vk::SurfaceFormatKHR const &f) { return (f.format == requestedFormat) && (f.colorSpace == requestedColorSpace); });
            if (it != formats.end()) {
                pickedFormat = *it;
                break;
            }
        }
    }
    assert(pickedFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear);
    return pickedFormat;
}

vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &availablePresentModes) {
    for (const auto &availablePresentMode : availablePresentModes) {
        if (availablePresentMode == vk::PresentModeKHR::eMailbox) {
            return availablePresentMode;
        }
    }

    return vk::PresentModeKHR::eFifo;
}

vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &availablePresentModes, bool use_vsync) {
    if (use_vsync == true) {
        return vk::PresentModeKHR::eFifo;
    }

    auto no_vsync_present_mode = vk::PresentModeKHR::eFifo;

    // prefer `mailbox`, use `immediate` as a fallback
    for (const auto &availablePresentMode : availablePresentModes) {
        if (availablePresentMode == vk::PresentModeKHR::eImmediate && no_vsync_present_mode == vk::PresentModeKHR::eFifo)
            no_vsync_present_mode = vk::PresentModeKHR::eImmediate;
        if (availablePresentMode == vk::PresentModeKHR::eMailbox)
            no_vsync_present_mode = vk::PresentModeKHR::eMailbox;
    }

    if (no_vsync_present_mode == vk::PresentModeKHR::eFifo) {
        throw std::runtime_error("swapchain without vsync is not supported");
    }

    return no_vsync_present_mode;
}

/// Also known as `max concurrently in flight frames` and `frame lag`.
uint32_t chooseSwapchainImageCount(const vk::SurfaceCapabilitiesKHR &capabilities) {
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    return imageCount;
}

/// Clamp swap extent to the valid range
vk::Extent2D chooseSwapExtent(vk::Extent2D extent, const vk::SurfaceCapabilitiesKHR &capabilities) {
    // if (capabilities.currentExtent.width != UINT32_MAX) {
    //    return capabilities.currentExtent;
    //} else {
    // int width, height;
    // glfwGetFramebufferSize(window, &width, &height);

    vk::Extent2D actualExtent(extent);

    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
    //}
}

} // namespace vvv