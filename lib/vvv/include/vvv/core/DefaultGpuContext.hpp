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

#include "preamble.hpp"

namespace vvv {

const std::string DefaultAppName = "(unnamed)";

struct GpuContextOptions {
    std::shared_ptr<DebugUtilities> debugUtilities = nullptr;
    std::string appName = DefaultAppName;
    bool enableDebug = vvv::EnableVulkanValidationLayersByDefault;
};

struct HeadFeaturesKhr {
    VkStructureType sType;
    void *pNext;
};

/// A collection of all vulkan resources that are usually acquired during
/// application initialization.
///
/// The reference to this class MUST be stable. This allows renderers to internalize
/// a reference to the structure for lifetime management of resources. Methods
/// like `Renderer::initSwapchainResources` should just be understood as events
/// that only announce change for data within the stable class reference.
class DefaultGpuContext : public GpuContext {
  public:
    explicit DefaultGpuContext(GpuContextOptions const opts) : GpuContext(opts.debugUtilities) {
        m_builder.appName = opts.appName;
        m_builder.enableDebug = opts.enableDebug;
        m_builder.deviceFeatures2.pNext = &m_builder.deviceFeaturesV12;
    }

    DefaultGpuContext() : DefaultGpuContext(GpuContextOptions{}) {}

    // builders
    void enableInstanceLayer(std::string layer) override { m_builder.instanceLayers.push_back(layer); }
    void enableInstanceExtension(std::string ext) override { m_builder.instanceExtensions.push_back(ext); }

    void enableDeviceLayer(std::string layer) override { m_builder.deviceLayers.push_back(layer); }
    void enableDeviceExtension(std::string ext) override { m_builder.deviceExtensions.push_back(ext); }
    vk::PhysicalDeviceFeatures &physicalDeviceFeatures() override { return m_builder.deviceFeatures2.features; }
    vk::PhysicalDeviceVulkan12Features &physicalDeviceFeaturesV12() override { return m_builder.deviceFeaturesV12; }
    vk::PhysicalDeviceVulkan13Features &physicalDeviceFeaturesV13() override { return m_builder.deviceFeaturesV13; }

    /// Lots of extensions require you to enable features on some <...FeaturesKHR> struct. You can enable these features
    /// by passing them to this function. Make sure to keep the pointer valid until after the context is created.
    void physicalDeviceAddExtensionFeatures(void *featuresKhr) override {
        // put the new extension features object at the beginning of the linked list of config objects
        // we do not have to check for nullpointers since we always manually append `PhysicalDeviceVulkan12Features` at startup.
        static_cast<HeadFeaturesKhr *>(featuresKhr)->pNext = m_builder.deviceFeatures2.pNext;
        m_builder.deviceFeatures2.pNext = featuresKhr;
    }

    bool hasDeviceExtension(const char *name) const override;
    bool hasInstanceExtension(const char *name) const override;
    bool hasEnabledInstanceExtension(const char *name) override;
    bool hasEnabledInstanceLayer(const char *name) override;
    PFN_vkVoidFunction getDeviceFunction(const char *name) override;
    PFN_vkVoidFunction getInstanceFunction(const char *name) override;
    vk::Instance getInstance() const override;
    vk::Device getDevice() const override;
    vk::PhysicalDevice getPhysicalDevice() const override;
    vvv::QueueFamilyIndices const &getQueueFamilyIndices() const override;

    vk::PhysicalDeviceSubgroupProperties getPhysicalDeviceSubgroupProperties() const override;

    std::string const &getAppName() { return m_builder.appName; }

    /// Acquire all GPU resources. This method is reintrant.
    void createGpuContext();
    /// Release all GPU resources. This method is reintrant.
    void destroyGpuContext() override;
    /// Check if GPU resources are currently acquired or not.
    bool isGpuContextCreated() const { return m_gpu.device != static_cast<vk::Device>(nullptr); }

    ~DefaultGpuContext() { DefaultGpuContext::destroyGpuContext(); }

  protected:
    /// by default, a context without present capabilities will be created
    virtual vk::SurfaceKHR createSurface() { return nullptr; };
    virtual void destroySurface();
    vk::SurfaceKHR getSurface() const { return m_gpu.surface; }

  private:
    vk::DebugUtilsMessengerCreateInfoEXT getDebugMessengerCreateInfo() const;
    bool isDebugMessengerEnabled() const { return std::find(m_builder.instanceLayers.begin(), m_builder.instanceLayers.end(), "VK_LAYER_KHRONOS_validation") != m_builder.instanceLayers.end(); }

    void createInstance();
    void setupDebugMessenger();

    /// The physical device is selected according to the following rules in order:\n
    /// 1.) the device with the number specified with the environment variable "VVV_DEVICE"\n
    /// 2.) the first (not blacklisted) discrete GPU\n
    /// 3.) the first (not blacklisted) GPU
    void createPhysicalDevice();
    void createLogicalDevice();
    void destroyInstance();
    void destroyDebugMessenger();
    void destroyPhysicalDevice();
    void destroyLogicalDevice();

    struct {
        /// A list of vulkan instance layers that should be enabled by user request
        std::vector<std::string> instanceLayers = {};
        std::vector<std::string> instanceExtensions = {};
        std::vector<std::string> deviceLayers = {};
        std::vector<std::string> deviceExtensions = {};
        vk::PhysicalDeviceFeatures2 deviceFeatures2 = {};
        vk::PhysicalDeviceVulkan12Features deviceFeaturesV12 = {};
        vk::PhysicalDeviceVulkan13Features deviceFeaturesV13 = {};
        std::string appName = DefaultAppName;
        bool enableDebug = vvv::EnableVulkanValidationLayersByDefault;
    } m_builder;

    /// state bound to the lifetime of the device and instance
    struct {
        vk::Instance instance = nullptr;
        vk::PhysicalDevice physicalDevice = nullptr;
        vk::Device device = nullptr;
        vk::SurfaceKHR surface = nullptr;
        vk::DebugUtilsMessengerEXT debugUtilsMessenger = nullptr;
        vvv::QueueFamilyIndices queueFamilyIndices;
    } m_gpu;
};

}; // namespace vvv
