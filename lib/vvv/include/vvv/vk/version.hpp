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
#include <sstream>

namespace vvv {

// uint32_t getVulkanApiVersion(vk::Instance inst) {
//     // this function did not exist prior to vulkan 1.1. So failure to dynamically get a reference
//     // to the function indicates vulkan 1.0.
//     auto FN_vkEnumerateInstanceVersion = PFN_vkEnumerateInstanceVersion(inst.getProcAddr("vkEnumerateInstanceVersion"));
//
//     if (FN_vkEnumerateInstanceVersion == nullptr)
//         return VK_API_VERSION_1_0;
//     else {
//         uint32_t instanceVersion;
//         auto result = FN_vkEnumerateInstanceVersion(&instanceVersion);
//         return instanceVersion;
//     }
// }

inline std::string getVersionString(uint32_t versionBitmask) {
    uint32_t uMajorAPIVersion = versionBitmask >> 22;
    uint32_t uMinorAPIVersion = ((versionBitmask << 10) >> 10) >> 12;
    uint32_t uPatchAPIVersion = (versionBitmask << 20) >> 20;

    int majorAPIVersion = uMajorAPIVersion;
    int minorAPIVersion = uMinorAPIVersion;
    int patchAPIVersion = uPatchAPIVersion;

    std::stringstream ss;
    ss << majorAPIVersion << "." << minorAPIVersion << "." << patchAPIVersion;
    return ss.str();
}

} // namespace vvv
