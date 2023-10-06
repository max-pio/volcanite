#pragma once

#include <vulkan/vulkan.hpp>

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

std::string getVersionString(uint32_t versionBitmask) {
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
