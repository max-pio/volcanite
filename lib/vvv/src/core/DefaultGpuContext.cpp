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

#include <vvv/core/preamble.hpp>

#include <vvv/core/DefaultGpuContext.hpp>

#include <vvv/vk/queue.hpp>

#include <vvv/util/Logger.hpp>

#include <cstdlib>
#include <thread>

bool vvv::DefaultGpuContext::hasEnabledInstanceExtension(const char *name) { return std::find(m_builder.instanceExtensions.begin(), m_builder.instanceExtensions.end(), name) != m_builder.instanceExtensions.end(); }
bool vvv::DefaultGpuContext::hasEnabledInstanceLayer(const char *name) { return std::find(m_builder.instanceLayers.begin(), m_builder.instanceLayers.end(), name) != m_builder.instanceLayers.end(); }

PFN_vkVoidFunction vvv::DefaultGpuContext::getDeviceFunction(const char *name) { return getDevice().getProcAddr(name); }
PFN_vkVoidFunction vvv::DefaultGpuContext::getInstanceFunction(const char *name) { return getInstance().getProcAddr(name); }

vk::Instance vvv::DefaultGpuContext::getInstance() const { return m_gpu.instance; }
vk::Device vvv::DefaultGpuContext::getDevice() const { return m_gpu.device; }
vk::PhysicalDevice vvv::DefaultGpuContext::getPhysicalDevice() const { return m_gpu.physicalDevice; }
const vvv::QueueFamilyIndices &vvv::DefaultGpuContext::getQueueFamilyIndices() const { return m_gpu.queueFamilyIndices; }

void vvv::DefaultGpuContext::createGpuContext() {
#if 0
    // if you ever run into the unfortunate problem that the application crashes when mounted in a GPU debugger. You can
    // make use of the following trick: Enable the sleep line below. Startup the application in the GPU debugger, the
    // GPU debugger will probably not complain since glfw already initialized a window, and you have time to attach
    // the CPU debugger to the process. For example, to attach gdb to a binary called `virtualfridge` that was started
    // via RenderDoc do:
    //
    // ```
    // gdb virtualfridge `pidof virtualfridge`
    // ```
    //
    // Continuing once should lead you to the location of the SEGFAULT in the RenderDoc layers that are wedged between
    // the vulkan API and our application.
    std::this_thread::sleep_for(std::chrono::seconds(3));
#endif
    if (isGpuContextCreated()) {
        return;
    }

    if (m_builder.enableDebug) {
        enableInstanceLayer("VK_LAYER_KHRONOS_validation");
    }

    if (!debugMarker->extensionName().empty()) {
        enableInstanceExtension(debugMarker->extensionName());
    }

    // GpuContext provides a simple synchronization API using a timeline semaphore
    physicalDeviceFeaturesV12().setTimelineSemaphore(true);
    physicalDeviceFeaturesV13().setDynamicRendering(true);

    createInstance();
    setupDebugMessenger();
    m_gpu.surface = createSurface();
    createPhysicalDevice();
    createLogicalDevice();
}

void vvv::DefaultGpuContext::destroyGpuContext() {
    GpuContext::destroyGpuContext();
    destroyLogicalDevice();
    destroyPhysicalDevice();
    destroySurface();
    destroyDebugMessenger();
    destroyInstance();
}

bool is_instance_extension_supported(std::string name) {
    const auto extensions = vk::enumerateInstanceExtensionProperties(); // get number of extensions
    for (auto const &extension : extensions) {
        std::string ex_name = extension.extensionName;
        if (ex_name == name) {
            return true;
        }
    }
    return false;
}

bool log_supported_instance_extensions() {
    const auto extensions = vk::enumerateInstanceExtensionProperties(); // get number of extensions
    auto logline = vvv::Logger(vvv::Debug);
    logline << "supported instance extensions: ";
    for (auto const &extension : extensions) {
        logline << extension.extensionName << ", ";
    }
    return false;
}

bool is_instance_layer_supported(std::string name) {
    const auto layers = vk::enumerateInstanceLayerProperties();
    for (auto const &layer : layers) {
        std::string l_name = layer.layerName;
        if (l_name == name)
            return true;
    }
    return false;
}

void log_supported_instance_layers() {
    const auto layers = vk::enumerateInstanceLayerProperties();
    auto logline = vvv::Logger(vvv::Debug);
    logline << "supported instance layers: ";
    for (auto const &layer : layers) {
        logline << layer.layerName << ", ";
    }
}

bool is_device_extension_supported(vk::PhysicalDevice device, std::string name) {
    const auto extensions = device.enumerateDeviceExtensionProperties(); // get number of extensions
    for (auto const &extension : extensions) {
        std::string ex_name = extension.extensionName;
        if (ex_name == name) {
            return true;
        }
    }
    return false;
}

bool vvv::DefaultGpuContext::hasDeviceExtension(const char *name) const { return is_device_extension_supported(m_gpu.physicalDevice, std::string(name)); }
bool vvv::DefaultGpuContext::hasInstanceExtension(const char *name) const { return is_instance_extension_supported(std::string(name)); }

bool log_supported_device_extensions(vk::PhysicalDevice device) {
    const auto extensions = device.enumerateDeviceExtensionProperties(); // get number of extensions
    auto logline = vvv::Logger(vvv::Debug);
    logline << "supported device extensions: ";
    for (auto const &extension : extensions) {
        logline << extension.extensionName << ", ";
    }
    return false;
}

void vvv::DefaultGpuContext::createInstance() {
#if (VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1)
#if VK_HEADER_VERSION >= 255
    VULKAN_HPP_DEFAULT_DISPATCHER.init();
#else
    static vk::detail::DynamicLoader dl;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
#endif
#endif

    // create vulkan instance

    std::vector<const char *> extensions{};

    for (const auto &ext : m_builder.instanceExtensions) {
        extensions.push_back(ext.c_str());
    }

    log_supported_instance_extensions();
    Logger(Debug) << "enabling instance extensions:";

    for (const auto &ext : extensions) {
        Logger(Debug) << "    " << (is_instance_extension_supported(ext) ? "[x] " : "[ ] ") << ext;
    }

    std::vector<const char *> instanceLayers;
    instanceLayers.reserve(m_builder.instanceLayers.size());

    for (size_t i = 0; i < m_builder.instanceLayers.size(); ++i)
        instanceLayers.push_back(const_cast<char *>(m_builder.instanceLayers[i].c_str()));

    log_supported_instance_layers();
    Logger(Debug) << "enabling instance layers:";

    const auto allLayers = vk::enumerateInstanceLayerProperties();
    for (const auto &layer : instanceLayers) {
        Logger(Debug) << (is_instance_layer_supported(layer) ? "    [x] " : "    [ ] ") << layer;
    }

    vk::ApplicationInfo applicationInfo(m_builder.appName.c_str(), 1, m_builder.appName.c_str(), 1, VK_API_VERSION_1_3);

    vk::InstanceCreateInfo instanceCreateInfo({}, &applicationInfo, instanceLayers, extensions);

    const auto debugCreateInfo = getDebugMessengerCreateInfo();
    if (isDebugMessengerEnabled()) {
        instanceCreateInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
    }

    // enable GLSL debugPrintfEXT() output and synchronization validation by default
    // TODO: enabling DebugPrintf by default makes it impossible to enable GPU assisted validation (can only use one)
    vk::ValidationFeaturesEXT valFeatures;
    auto features = {vk::ValidationFeatureEnableEXT::eDebugPrintf,
                     vk::ValidationFeatureEnableEXT::eSynchronizationValidation};
    valFeatures.setEnabledValidationFeatures(features);
    valFeatures.pNext = instanceCreateInfo.pNext;
    instanceCreateInfo.pNext = &valFeatures;

    try {
        m_gpu.instance = vk::createInstance(instanceCreateInfo);
    } catch (std::runtime_error &e) {
        Logger(Error) << "Error encountered in vk::createInstance(): " << e.what();
        Logger(Info) << "Try running with VK_LOADER_DEBUG=all to see errors from broken layers.";
        throw;
    }

#if (VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1)
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_gpu.instance);
#endif
}

void vvv::DefaultGpuContext::destroyInstance() { VK_DESTROY(m_gpu.instance) }

#if VK_HEADER_VERSION >= 309
static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugUtilsMessengerCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity, vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
                                                                    vk::DebugUtilsMessengerCallbackDataEXT const *pCallbackData, void * /*pUserData*/) {
#else
static VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                                                  VkDebugUtilsMessengerCallbackDataEXT const *pCallbackData, void * /*pUserData*/) {
#endif

    // Note: set to VK_TRUE, to abort after the first set of validation errors
    auto shouldAbort = VK_FALSE;

#if !defined(NDEBUG)
    // per specification pMessageIdName may not be NULL, but RenderDoc emits a single message at startup that has this field set to null.
    // See https://github.com/baldurk/renderdoc/blob/aa26252a778ee9cd795557e346cf8780f56aa834/renderdoc/driver/vulkan/wrappers/vk_misc_funcs.cpp#L1772
    // released under an MIT license
    if (pCallbackData->pMessageIdName != nullptr && strcmp(pCallbackData->pMessageIdName, "Loader Message") == 0) {
        // blocks info about loaded layers, extensions, etc
        return VK_FALSE;
    }

    // Note: comment in to ignore destroy device errors:
    //    if (pCallbackData->messageIdNumber == 1901072314) {
    //        // VUID-vkDestroyDevice-device-00378
    //        return VK_FALSE;
    //    }

    if (pCallbackData->messageIdNumber == 648835635) {
        // UNASSIGNED-khronos-Validation-debug-build-warning-message
        return VK_FALSE;
    }
    if (pCallbackData->messageIdNumber == 767975156) {
        // UNASSIGNED-BestPractices-vkCreateInstance-specialuse-extension
        return VK_FALSE;
    }
#endif

    if (pCallbackData->messageIdNumber == 2094043421) {
        // VUID-VkSwapchainCreateInfoKHR-imageExtent-01274 may lag if the swapchain rebuild is too slow.
        shouldAbort = VK_FALSE;
    }

    // shorter message format for printf in shaders
    if (std::string(pCallbackData->pMessageIdName).find("DEBUG-PRINTF") != std::string::npos) {
        // the messageIdNumber from the debugPrintfEXT readme seems not reliable:
        // pCallbackData->messageIdNumber == 0x4fe1fef9) // for me it may be: 0x76589099
        // https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/docs/debug_printf.md

        // old format:
        // messageIDName = <UNASSIGNED-DEBUG-PRINTF>
        // "<Validation Information: [ UNASSIGNED-DEBUG-PRINTF ] Object 0: handle =
        // 0x1b6f9b0, type = VK_OBJECT_TYPE_DEVICE; | MessageID = 0x92394c89 |
        // ".length == 140

        // example:                                                            start ---v
        // "Validation Information: [ WARNING-DEBUG-PRINTF ] | MessageID = 0x76589099 | vkQueueSubmit():
        const size_t pos = std::string(pCallbackData->pMessage).find("MessageID =");
        if (pos == std::string::npos) {
            vvv::Logger(vvv::Debug) << "[shader] " << pCallbackData->pMessage;
        } else {
            vvv::Logger(vvv::Debug) << "[shader] " << (pCallbackData->pMessage + pos + 24);
        }

        shouldAbort = VK_FALSE;
        return shouldAbort;
    }

    auto severity = static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(messageSeverity);
    vvv::loglevel level = vvv::Info;
    if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose)
        level = vvv::Debug;
    else if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo)
        level = vvv::Info;
    else if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
        level = vvv::Warn;
    else if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
        level = vvv::Error;
    auto err = vvv::Logger(level);

    // color validation message if it fits this known pattern
    std::string message = pCallbackData->pMessage;
    auto pos = message.find("encountered the following validation error at ");
    auto pos1 = message.find(": ", pos);
    if (pos1 != std::string::npos)
        pos1 += 2;
    auto pos2 = message.find("The Vulkan spec states: ", pos1);
    auto pos3 = pos2 + std::string("The Vulkan spec states: ").size();
    auto pos4 = message.find(" (http", pos2);
    const auto bold_on = "\033[1m";
    const auto bold_off = "\033[22m";
    if (vvv::Logger::getUseColors() && pos1 != std::string::npos && pos2 != std::string::npos && pos3 != std::string::npos && pos4 != std::string::npos)
        message = message.substr(0, pos1) + bold_on + message.substr(pos1, pos2 - pos1) + bold_off + message.substr(pos2, pos3 - pos2) + bold_on + message.substr(pos3, pos4 - pos3) + bold_off + message.substr(pos4);

    err << vk::to_string(static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(messageTypes)) << ":\n";
    err << "\tmessageIDName   = <" << pCallbackData->pMessageIdName << ">\n";
    err << "\tmessageIdNumber = " << pCallbackData->messageIdNumber << "\n";
    err << "\tmessage         = <" << message << ">\n";
    if (0 < pCallbackData->queueLabelCount) {
        err << "\tQueue Labels:\n";
        for (uint8_t i = 0; i < pCallbackData->queueLabelCount; i++) {
            err << "\t\tlabelName = <" << pCallbackData->pQueueLabels[i].pLabelName << ">\n";
        }
    }
    if (0 < pCallbackData->cmdBufLabelCount) {
        err << "\tCommandBuffer Labels:\n";
        for (uint8_t i = 0; i < pCallbackData->cmdBufLabelCount; i++) {
            err << "\t\tlabelName = <" << pCallbackData->pCmdBufLabels[i].pLabelName << ">\n";
        }
    }
    if (0 < pCallbackData->objectCount) {
        err << "\tObjects:\n";
        for (uint8_t i = 0; i < pCallbackData->objectCount; i++) {
            err << "\t\tObject " << i << "\n";
            err << "\t\t\tobjectType   = " << vk::to_string(static_cast<vk::ObjectType>(pCallbackData->pObjects[i].objectType)) << "\n";
            err << "\t\t\tobjectHandle = " << pCallbackData->pObjects[i].objectHandle << "\n";
            if (pCallbackData->pObjects[i].pObjectName) {
                err << "\t\t\tobjectName   = <" << pCallbackData->pObjects[i].pObjectName << ">\n";
            }
        }
    }
    return shouldAbort;
}

vk::DebugUtilsMessengerCreateInfoEXT vvv::DefaultGpuContext::getDebugMessengerCreateInfo() const {
    const vk::DebugUtilsMessengerCreateInfoEXT debugUtilsInfo = {{},
                                                                 vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                                                                     vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
                                                                 vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                                                                     vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
                                                                 debugUtilsMessengerCallback};

    return debugUtilsInfo;
}

void vvv::DefaultGpuContext::setupDebugMessenger() {
    if (isDebugMessengerEnabled()) {
        const vk::DebugUtilsMessengerCreateInfoEXT debugUtilsInfo = getDebugMessengerCreateInfo();

        m_gpu.debugUtilsMessenger = getInstance().createDebugUtilsMessengerEXT(debugUtilsInfo);
    }
}

void vvv::DefaultGpuContext::destroyDebugMessenger() {
    if (m_gpu.debugUtilsMessenger != static_cast<decltype(m_gpu.debugUtilsMessenger)>(nullptr)) {
        getInstance().destroyDebugUtilsMessengerEXT(m_gpu.debugUtilsMessenger);
        m_gpu.debugUtilsMessenger = nullptr;
    }
}

bool isBlacklistedPhysicalDevice(const vk::PhysicalDeviceProperties &properties) {

    // llvmpipe is some non-conforming test driver installed with LLVM
    std::string dev_name = properties.deviceName;
    if (dev_name.find("llvmpipe") != std::string::npos) {
        return true;
    }

    return false;
}

void vvv::DefaultGpuContext::destroySurface() {
    if (m_gpu.surface != static_cast<decltype(m_gpu.surface)>(nullptr)) {
        m_gpu.instance.destroySurfaceKHR(m_gpu.surface);
        m_gpu.surface = nullptr;
    }
}

void vvv::DefaultGpuContext::createPhysicalDevice() {
    const auto devices = getInstance().enumeratePhysicalDevices();

    std::optional<int> envSelection = {};
    std::optional<int> firstDiscreteSelection = {};
    std::optional<int> firstSelection = {};

    // parse env variable
    char *envStr = std::getenv("VOLCANITE_DEVICE");
    if (envStr) {
        try {
            int selection = std::stoi(std::string(envStr));
            if (selection >= 0 && selection < devices.size())
                envSelection = selection;
            else
                Logger(Warn) << "Environment variable VOLCANITE_DEVICE is out of range. VOLCANITE_DEVICE will be ignored.";
        } catch (std::invalid_argument &e) {
            Logger(Warn) << "Environment variable VOLCANITE_DEVICE is not a valid number. VOLCANITE_DEVICE will be ignored. " << e.what();
        } catch (std::out_of_range &e) {
            Logger(Warn) << "Environment variable VOLCANITE_DEVICE is not a valid number. VOLCANITE_DEVICE will be ignored. " << e.what();
        }
    }

    // search for first (discrete) GPU
    for (int i = 0; i < devices.size(); i++) {
        const auto deviceProperties = devices[i].getProperties2();
        bool discrete = deviceProperties.properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;

        if (isBlacklistedPhysicalDevice(deviceProperties.properties))
            continue;

        if (!firstSelection.has_value())
            firstSelection = i;
        if (discrete && !firstDiscreteSelection.has_value())
            firstDiscreteSelection = i;
    }

    if (envSelection.has_value())
        m_gpu.physicalDevice = devices[envSelection.value()];
    else if (firstDiscreteSelection)
        m_gpu.physicalDevice = devices[firstDiscreteSelection.value()];
    else if (firstSelection)
        m_gpu.physicalDevice = devices[firstSelection.value()];

    for (int i = 0; i < devices.size(); i++) {
        bool selected = devices[i] == m_gpu.physicalDevice;
        auto deviceType = devices[i].getProperties2().properties.deviceType;
        Logger(Info) << "Physical Device " << i << ": " << devices[i].getProperties2().properties.deviceName << (selected ? " (selected)" : "") << " (" + to_string(deviceType) + ")";
    }
}

void vvv::DefaultGpuContext::destroyPhysicalDevice() {}

void vvv::DefaultGpuContext::createLogicalDevice() {

    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfo = {};

    m_gpu.queueFamilyIndices = findQueueFamilyIndices(getPhysicalDevice(), m_gpu.surface, &queueCreateInfo);

    // Note: features2 exposes ray tracing info. See https://github.com/KhronosGroup/Vulkan-Hpp/blob/6d5d6661f39b7162027ad6f75d4d2e902eac4d55/samples/RayTracing/RayTracing.cpp#L759-L766
    // auto supportedFeatures = getPhysicalDevice().getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceDescriptorIndexingFeaturesEXT>();

    std::vector<char const *> enabledDeviceLayers = {};

    enabledDeviceLayers.reserve(m_builder.deviceLayers.size());

    for (const auto &deviceLayer : m_builder.deviceLayers)
        enabledDeviceLayers.push_back(const_cast<char *>(deviceLayer.c_str()));

    std::vector<char const *> enabledDeviceExtensions = {};

    Logger(Debug) << "enabling device layers:";

    for (const auto &layer : enabledDeviceLayers) {
        Logger(Debug) << "    " << layer;
    }

    enabledDeviceExtensions.reserve(m_builder.deviceExtensions.size());

    for (const auto &deviceExtension : m_builder.deviceExtensions)
        enabledDeviceExtensions.push_back(const_cast<char *>(deviceExtension.c_str()));

    log_supported_device_extensions(getPhysicalDevice());
    Logger(Debug) << "enabling device extensions:";

    for (const auto &ext : enabledDeviceExtensions) {
        Logger(Debug) << "    " << (is_device_extension_supported(getPhysicalDevice(), ext) ? "[x] " : "[ ] ") << ext;
    }

    vk::DeviceCreateInfo deviceCreateInfo({}, queueCreateInfo, enabledDeviceLayers, enabledDeviceExtensions, nullptr);
    m_builder.deviceFeatures2.pNext = &m_builder.deviceFeaturesV12;
    m_builder.deviceFeaturesV12.pNext = &m_builder.deviceFeaturesV13;
    deviceCreateInfo.pNext = &m_builder.deviceFeatures2;

    m_gpu.device = getPhysicalDevice().createDevice(deviceCreateInfo);

#if (VULKAN_HPP_DISPATCH_LOADER_DYNAMIC == 1)
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_gpu.device);
#endif

    debugMarker->enable(this);

    // m_gpu.queueGraphics = getDevice().getQueue(getQueueFamilyIndices().graphics, 0);
    // debugMarker->setName(m_gpu.queueGraphics, "Application.m_gpu.queueGraphics");

    // if (m_gpu.queueFamilyIndices.present) {
    //     m_gpu.queuePresent = getDevice().getQueue(m_gpu.queueFamilyIndices.present, 0);
    //     debugMarker->setName(m_gpu.queuePresent, "Application.m_gpu.queuePresent");
    // }

    initContext();
}

void vvv::DefaultGpuContext::destroyLogicalDevice(){
    VK_DESTROY(m_gpu.device)}

vk::PhysicalDeviceSubgroupProperties vvv::DefaultGpuContext::getPhysicalDeviceSubgroupProperties() const {
    vk::PhysicalDeviceSubgroupProperties subgroupProperties;
    vk::PhysicalDeviceProperties2 deviceProperties2;
    deviceProperties2.pNext = &subgroupProperties;

    if (hasDeviceExtension("VK_EXT_memory_budget"))
        getPhysicalDevice().getProperties2(&deviceProperties2);
    return subgroupProperties;
}
