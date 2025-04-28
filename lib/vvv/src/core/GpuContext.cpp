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

#include <vvv/core/GpuContext.hpp>

namespace vvv {

GpuContext::GpuContext(const std::shared_ptr<DebugUtilities> &debugUtilities)
    : debugMarker(debugUtilities == nullptr ? createDefaultDebugUtilities() : debugUtilities), sync(std::make_unique<Synchronization>(this)) {}

std::shared_ptr<DebugUtilities> createDefaultDebugUtilities() {
    std::shared_ptr<DebugUtilities> debugUtilities;
    if (EnableVulkanValidationLayersByDefault) {
        debugUtilities = std::make_shared<DebugUtilsExt>();
    } else {
        debugUtilities = std::make_shared<DebugNoop>();
    }

    return debugUtilities;
}

// std::pair<vk::CommandBuffer, AwaitableHandle > GpuContext::getCommandBuffer(vk::ArrayProxyNoTemporaries<AwaitableHandle > awaitables, uint32_t queueFamilyIndex) const {
//     const auto awaitable = sync->createAwaitable(awaitables);
//     const auto cb = getCommandBuffer(&awaitable, queueFamilyIndex);
//     return {cb, awaitable};
// }

std::pair<vk::CommandBuffer, AwaitableHandle> GpuContext::getCommandBuffer(AwaitableList awaitables, uint32_t queueFamilyIndex) const {
    // copy from temporary buffer to permanent buffer
    const auto awaitable = sync->createAwaitable(awaitables);
    const auto cb = getCommandBuffer(awaitable, queueFamilyIndex);
    return {cb, awaitable};
}

vk::CommandBuffer GpuContext::getCommandBuffer(AwaitableHandle awaitable, uint32_t queueFamilyIndex) const {

    // check if an already allocated command buffer is available.
    if (!m_commandPool.contains(queueFamilyIndex)) {
        throw std::runtime_error("unknown queue family " + std::to_string(queueFamilyIndex));
    }

    // try to find an unused command buffer
    for (auto &cb : m_commandBuffers[queueFamilyIndex]) {
        if (sync->isAwaitableResolved(cb.awaitable)) {
            // the command buffer is currently unused, because the dispatch using the command buffer
            // was already signaled.
            cb.awaitable = awaitable;
            return cb.handle;
        }
    }

    // no unused command buffer, allocate a new one. Our pool currently has limited size, so this might throw.
    vk::CommandBufferAllocateInfo cmdBufferAllocInfo(m_commandPool.at(queueFamilyIndex), vk::CommandBufferLevel::ePrimary, 1);
    std::vector<vk::CommandBuffer> commandBuffers = getDevice().allocateCommandBuffers(cmdBufferAllocInfo);
    debugMarker->setName(commandBuffers[0], "await=" + std::to_string(awaitable->semaphoreId) + "&queue=" + std::to_string(queueFamilyIndex));
    m_commandBuffers[queueFamilyIndex].push_back({.handle = commandBuffers[0], .awaitable = awaitable});

    return commandBuffers[0];
}

vvv::AwaitableHandle GpuContext::executeCommands(std::function<void(vk::CommandBuffer)> writeCommands, detail::OpenGLStyleSubmitOptions opts) const {
    auto [commandBuffer, commandBufferAwaitable] = getCommandBuffer(opts.await, opts.queueFamily);

    commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    writeCommands(commandBuffer);
    commandBuffer.end();
    sync->submit(commandBuffer, commandBufferAwaitable, opts.queueFamily);

    if (opts.hostWait) {
        sync->hostWaitOnDevice({commandBufferAwaitable});
    }

    return commandBufferAwaitable;
}

}; // namespace vvv
