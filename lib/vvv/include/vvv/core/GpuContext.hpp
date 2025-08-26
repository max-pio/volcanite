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

#include "preamble_forward_decls.hpp"

#include "Synchronization.hpp"
#include "WindowingSystemIntegration.hpp"
#include "vvv/vk/debug_marker.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <optional>

namespace vvv {

std::shared_ptr<DebugUtilities> createDefaultDebugUtilities();

const bool EnableVulkanValidationLayersByDefault =
#ifndef NDEBUG
    true
#else
    false
#endif
    ;

/// Queue family indices without presentation queue.\n\n
///
/// If available, compute and transfer will be dedicated queues distinct
/// from the graphics queue.
/// If possible, the present queue will be the same as the graphics queue
/// to reduce queue transfers.
///
/// Note: it's recommended to retrieve the optional contents with `.value()`
/// instead of dereferencing. the first will throw an exception, the second
/// results in undefined behaviour.
struct QueueFamilyIndices {
    std::optional<uint32_t> graphics = {};
    std::optional<uint32_t> compute = {};
    std::optional<uint32_t> transfer = {};
    std::optional<uint32_t> present = {};
};

/// Globally caches created vulkan pipelines. This will speedup pipeline recreations and pipeline creation on application startup.
// Note that the official khronos examples cache way more stuff:
// https://github.com/KhronosGroup/Vulkan-Samples/blob/30e0ef953f9492726945d2042400a3808c8408f5/framework/resource_cache.h
class GpuPipelineCache {
  public:
    virtual ~GpuPipelineCache() = default;

    [[nodiscard]] virtual vk::PipelineCache getPipelineCache() const { return m_pipelineCache; }

    virtual void destroyPipelineCache(vk::Device device) { VK_DEVICE_DESTROY(device, m_pipelineCache); }

  protected:
    virtual std::string getPipelineCachePath() { return "vulkan_pipeline_cache.data"; }

    void writePipelineCacheToDisk(vk::Device device) {
        // Get size of pipeline cache
        if (m_pipelineCache == VK_NULL_HANDLE)
            throw std::runtime_error("pipeline cache is null");
        auto data = device.getPipelineCacheData(m_pipelineCache);

        std::ios_base::sync_with_stdio(false);
        auto cache_file = std::fstream(getPipelineCachePath(), std::ios::out | std::ios::binary | std::ios::trunc);
        cache_file.write((char *)&data[0], data.size());
        cache_file.close();
    }
    void readPipelineCacheFromDisk(vk::Device device) {
        // Try to read pipeline cache file if exists
        std::vector<char> pipeline_data;
        vk::PipelineCacheCreateInfo pipelineCacheCreateInfo;
        try {
            std::ifstream input(getPipelineCachePath(), std::ios::binary);
            pipeline_data = std::vector(std::istreambuf_iterator<char>(input), {});
            pipelineCacheCreateInfo.initialDataSize = pipeline_data.size();
            pipelineCacheCreateInfo.pInitialData = pipeline_data.data();
        } catch (std::runtime_error &ex) {
            Logger(Error) << "Pipeline cache create info failed: " << ex.what();
        }

        m_pipelineCache = device.createPipelineCache(pipelineCacheCreateInfo);
        if (m_pipelineCache == VK_NULL_HANDLE) {
            Logger(Warn) << "Error reading vulkan pipeline cache from " << getPipelineCachePath() << ". Resetting file.";
            std::filesystem::remove(getPipelineCachePath());
            m_pipelineCache = device.createPipelineCache(pipelineCacheCreateInfo);
            if (m_pipelineCache == VK_NULL_HANDLE)
                throw std::runtime_error("Reading pipeline cache " + getPipelineCachePath() + " failed.");
        }
    }

  private:
    vk::PipelineCache m_pipelineCache = nullptr;
};

namespace detail {
struct ManagedCommandBuffer {
    vk::CommandBuffer handle;
    /// indicates who currently has ownership of the command buffer
    AwaitableHandle awaitable;
};

struct OpenGLStyleSubmitOptions {
    /// Execute on the given queue
    uint32_t queueFamily = 0;
    /// If true, block the CPU until the operation finishes
    bool hostWait = false;
    AwaitableList await = {};
};
}; // namespace detail

/// @brief A collection of all vulkan resources that are usually acquired during application initialization.
///
/// The reference to this class MUST be stable. This allows renderers to internalize
/// a reference to the structure for lifetime management of resources. Methods
/// like `Renderer::initSwapchainResources` should just be understood as events
/// that only announce change for data within the stable class reference.
class GpuContext : public GpuPipelineCache {
  public:
    GpuContext(const std::shared_ptr<DebugUtilities> &debugUtilities);

    virtual void destroyGpuContext() {
        const auto device = getDevice();

        if (device != static_cast<vk::Device>(nullptr)) {
            // write the pipeline cache to disk if it was used. This will speed up subsequent invocations of the application.
            writePipelineCacheToDisk(device);
            destroyPipelineCache(device);
        }

        for (auto &[queueFamily, commandBuffers] : m_commandBuffers) {
            for (auto &cb : commandBuffers) {
                VK_DEVICE_FREE(device, m_commandPool[queueFamily], cb.handle);
            }
        }

        m_commandBuffers.clear();

        for (auto &[queueFamily, pool] : m_commandPool) {
            VK_DEVICE_DESTROY(device, pool);
        }

        m_commandPool.clear();

        m_queues.clear();

        debugMarker = nullptr;
        sync = nullptr;
    }

    /// announces that it's safe to call any function on the context. This method is reentrant.
    virtual void initContext() {
        const auto device = getDevice();

        readPipelineCacheFromDisk(device);
        // sync->initResources(device);

        // initialize the queue API
        const auto &queueIndices = getQueueFamilyIndices();

        if (queueIndices.present) {
            m_queues.insert({queueIndices.present.value(), device.getQueue(queueIndices.present.value(), 0)});
        }

        m_queues.insert({queueIndices.compute.value(), device.getQueue(queueIndices.compute.value(), 0)});
        m_queues.insert({queueIndices.graphics.value(), device.getQueue(queueIndices.graphics.value(), 0)});
        m_queues.insert({queueIndices.transfer.value(), device.getQueue(queueIndices.transfer.value(), 0)});

        // initialize the command buffer convenience API
        for (const auto &[queueFamilyIndex, _queue] : m_queues) {
            vk::CommandPoolCreateInfo cmdPoolInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamilyIndex);
            m_commandPool.insert({queueFamilyIndex, device.createCommandPool(cmdPoolInfo)});
        }
    }

    vk::Queue getQueue(uint32_t queueFamilyIndex = 0) const { return m_queues.at(queueFamilyIndex); }

    /// @brief Get a primary command buffer for the queue that is automatically released after the work finishes.
    ///
    /// Discouraged API: This is a suboptimal convenience API for research work. Use it for one-off work and similar
    /// convenience APIs that represent a shortcut to get a research prototype running.
    vk::CommandBuffer getCommandBuffer(AwaitableHandle awaitable, uint32_t queueFamilyIndex = 0) const;

    /// @brief Get a command buffer.
    ///
    /// The dependency list `awaitables` is internally copied, you must ensure that the given list of pointers is valid for the whole lifetime of the awaitable.
    std::pair<vk::CommandBuffer, AwaitableHandle> getCommandBuffer(AwaitableList awaitables = {}, uint32_t queueFamilyIndex = 0) const;

    /// Execute some GPU work in the style of OpenGL.
    vvv::AwaitableHandle executeCommands(std::function<void(vk::CommandBuffer)> writeCommands, detail::OpenGLStyleSubmitOptions opts = {}) const;

    virtual vk::Instance getInstance() const = 0;
    virtual vk::Device getDevice() const = 0;
    virtual vk::PhysicalDevice getPhysicalDevice() const = 0;
    virtual QueueFamilyIndices const &getQueueFamilyIndices() const = 0;

    virtual vk::PhysicalDeviceSubgroupProperties getPhysicalDeviceSubgroupProperties() const = 0;

    std::shared_ptr<DebugUtilities> debugMarker;

    // TODO: a single semaphore might not enough for multibuffering
    std::unique_ptr<Synchronization> sync;

    /// Methods to interact with the swapchain, resp. windowing system.
    /// @return `nullptr` if the context is not associated with a windowing system, for example if vulkan is only used for compute work
    virtual const WindowingSystemIntegration *getWsi() const { return nullptr; }
    //    virtual std::shared_ptr<const WindowingSystemIntegration> getWsi() const { return nullptr; }

    virtual bool hasDeviceExtension(char const *name) const = 0;
    virtual bool hasDeviceExtension(std::string name) const { return hasDeviceExtension(name.c_str()); }

    virtual bool hasInstanceExtension(char const *name) const = 0;
    virtual bool hasInstanceExtension(std::string name) const { return hasInstanceExtension(name.c_str()); }

    virtual PFN_vkVoidFunction getDeviceFunction(char const *name) = 0;
    virtual PFN_vkVoidFunction getDeviceFunction(std::string name) { return getDeviceFunction(name.c_str()); }

    virtual PFN_vkVoidFunction getInstanceFunction(char const *name) = 0;
    virtual PFN_vkVoidFunction getInstanceFunction(std::string name) { return getInstanceFunction(name.c_str()); }

    virtual void enableInstanceLayer(std::string layer) = 0;
    virtual void enableInstanceExtension(std::string ext) = 0;
    virtual bool hasEnabledInstanceExtension(char const *name) = 0;
    virtual bool hasEnabledInstanceExtension(std::string name) { return hasEnabledInstanceExtension(name.c_str()); }
    virtual bool hasEnabledInstanceLayer(char const *name) = 0;
    virtual bool hasEnabledInstanceLayer(std::string name) { return hasEnabledInstanceLayer(name.c_str()); }

    virtual void enableDeviceLayer(std::string layer) = 0;
    virtual void enableDeviceExtension(std::string ext) = 0;

    virtual vk::PhysicalDeviceFeatures &physicalDeviceFeatures() = 0;
    virtual vk::PhysicalDeviceVulkan12Features &physicalDeviceFeaturesV12() = 0;
    virtual vk::PhysicalDeviceVulkan13Features &physicalDeviceFeaturesV13() = 0;
    virtual void physicalDeviceAddExtensionFeatures(void *featuresKhr) = 0;

  protected:
    std::map<uint32_t, vk::Queue> m_queues;

    std::map<uint32_t, vk::CommandPool> m_commandPool;
    mutable std::map<uint32_t, std::vector<detail::ManagedCommandBuffer>> m_commandBuffers;
};

} // namespace vvv
