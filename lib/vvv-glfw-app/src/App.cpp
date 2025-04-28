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

#include "vvvwindow/App.hpp"
#include <glm/gtx/transform.hpp>
#include <vvv/util/Logger.hpp>
#include <vvv/vk/destroy.hpp>
#include <vvv/vk/swapchain.hpp>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include "stb/stb_image.hpp"
#include <GLFW/glfw3.h>

#ifdef IMGUI
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#include "imgui/imgui.h"
#include "imgui/implot/implot.h"

#include <fmt/core.h>
#endif

namespace vvv {

const uint32_t MAX_FRAMES_IN_FLIGHT = 2;
const auto IMAGE_NOT_IN_FLIGHT = std::numeric_limits<size_t>::max();

static void check_vk_result(VkResult err) {
    if (err != 0) {
        vvv::Logger(vvv::Error) << "Vulkan error " << vk::to_string(static_cast<vk::Result>(err));
        if (err < 0) {
            abort();
        }
    }
}

static void check_vk_result(vk::Result err) { check_vk_result(static_cast<VkResult>(err)); }

void Application::recreateSwapchain() {
    // TODO: use new API, otherwise not well defined
    getDevice().waitIdle();

    // Note: this is conservative: destroy the swapchain and everything that might depend on it
    // (Speak: Run the destructor up to the swapchain deletion)
    m_renderer->releaseSwapchain();
    destroyBlit();
    destroySwapChain();
    createSwapChain();
    createBlit();

    m_renderer->initSwapchainResources();

#ifdef IMGUI
    recreateSwapchainImGui();
#endif
}

void Application::renderFrame() {
    const auto frameIndex = currentInFlightFrameIndex();

    if (m_swapchain.frameInFlightAwaitable[frameIndex])
        sync->hostWaitOnDevice(vvv::AwaitableList{m_swapchain.frameInFlightAwaitable[frameIndex]});
    stateInFlight()->cleanKeepAlives(frameIndex);

    // TODO: fix Application synchronization
    // mark here the planing state protected by the fence as executed state? further below is also possible,
    // but whats correct? what has the tighter bounds?
    // a signal to a fence means the state has executed => can be marked => we observe the executed state when
    // waiting for the fence, so thats the right point to mark the planed state as executed.
    // the planned state that is signaled, is everything before the submit and the submit itself => need to record
    // the planing state after the submit.

    // Since device->acquireNextImageKHR throws an Exception in case of an outOfDateKHR even if we ask for a return value, we use the old-fashioned way without the Vulkan.hpp wrapper.
    // std::vector<vk::Semaphore> acquiteSwapchainImageSignalSemaphores = {m_swapchain.startFrameSemaphore[frameIndex], m_swapchain.presentCompleteSemaphore[frameIndex]};
    uint32_t currentImageIndex;
    VkResult nextImageResult = vkAcquireNextImageKHR(getDevice(), m_swapchain.swapchain, UINT64_MAX,
                                                     m_swapchain.presentCompleteSemaphore[frameIndex], nullptr,
                                                     &currentImageIndex);
    stateSwapchain()->setActiveIndex(currentImageIndex);

    switch (vk::Result(nextImageResult)) {
    case vk::Result::eSuccess:
        break;
    case vk::Result::eSuboptimalKHR:
        vvv::Logger(vvv::Warn)
            << "VK_SUBOPTIMAL_KHR: A swapchain no longer matches the surface properties exactly (returned from vkAcquireNextImageKHR)";
        break;
    case vk::Result::eErrorOutOfDateKHR:
        m_swapchain.pendingRecreation = true;
        break;
    default:
        assert(false);
    }

    if (m_swapchain.pendingRecreation) {
        recreateSwapchain();
        return;
    }

    // this check is necessary since vkAcquireNextImageKHR is not guranteed to emit swapchain images in a cycling manner.
    if (m_swapchain.imageInFlightFrame[currentImageIndex] != IMAGE_NOT_IN_FLIGHT) {
        const auto fenceIdx = m_swapchain.imageInFlightFrame[currentImageIndex];
        if (m_swapchain.frameInFlightAwaitable[fenceIdx])
            sync->hostWaitOnDevice(vvv::AwaitableList{m_swapchain.frameInFlightAwaitable[fenceIdx]});
    }
    m_swapchain.imageInFlightFrame[currentImageIndex] = frameIndex;

    // capture mouse position, normalize to screen extent and sent to renderer (if mouse not grabbed by ImGui)
#ifdef IMGUI
    if (!ImGui::GetIO().WantCaptureMouse)
#endif
    {
        double mouse_pos[2];
        glfwGetCursorPos(m_window, &mouse_pos[0], &mouse_pos[1]);
        m_renderer->setCursorPos(glm::vec2(mouse_pos[0] / static_cast<float>(m_swapchain.extent.width),
                                           mouse_pos[1] / static_cast<float>(m_swapchain.extent.height)));
    }

    // ------------------------ RECORD WORK FOR THE GPU
    const auto commandBuffer = m_swapchain.commandBuffers[currentImageIndex];

    // std::vector<vk::Semaphore> childRendererWaitSemaphores{m_swapchain.presentCompleteSemaphore[frameIndex]};
    // std::vector<vk::PipelineStageFlags> childRendererWaitStages{vk::PipelineStageFlagBits::eAllCommands};

    // const auto ldrRendererOutput = m_renderer->renderNextFrame(childRendererWaitSemaphores, childRendererWaitStages);
    const auto ldrRendererOutput = m_renderer->renderNextFrame({}, {});

    // Note: we do a one-time submit below, which automatically invalidates the command buffer. The reset to the initial
    // state required per specification, is implicitly performed by `commandBuffer.begin`. So fencing the command
    // buffer within a `begin` and `end` pair is enough for a complete, valid lifecycle of the command buffer.
    commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    renderFrameRecordCommands(commandBuffer, ldrRendererOutput);
    commandBuffer.end();

    // ------------------------ SUBMIT THE WORK TO THE GPU
    // make sure the swapchain allows us to write again. Since we only sync against the blit, we are guaranteed to
    // have the right queue type for `eColorAttachmentOutput`. If the sync against the swapchain would be passed
    // into the inner renderer, this would not be guaranteed. The inner renderer for example, could be compute queue only.
    // This would force us to use `eAllCommands` -- which would unnecessary restrict parallelism.
    vvv::BinaryAwaitableList swapchainPresentComplete{std::make_shared<vvv::BinaryAwaitable>(vvv::BinaryAwaitable{
        .semaphore = m_swapchain.presentCompleteSemaphore[frameIndex],
        .stages = vk::PipelineStageFlagBits::eAllCommands, // vk::PipelineStageFlagBits::eColorAttachmentOutput,
    })};

    // save video images
    if (m_video_frame.has_value()) {
        if (!m_record_in.has_value() || m_record_in->eof()) {
            m_video_frame = {};
        } else {
            ldrRendererOutput.texture->writePng(
                m_video_file_path + "_" + std::to_string(m_video_frame.value()) + ".png");
            m_video_frame = m_video_frame.value() + 1;
        }
    }

    const auto renderingUsageCompleteAwaitable = sync->submit(
        commandBuffer, getQueue(), ldrRendererOutput.renderingComplete, vk::PipelineStageFlagBits::eAllCommands,
        swapchainPresentComplete,
        &m_swapchain.blitToSwapchainImageComplete[frameIndex]);
    m_swapchain.frameInFlightAwaitable[frameIndex] = renderingUsageCompleteAwaitable;

    std::array<vk::Semaphore, 1> presentWaitSemaphores = {m_swapchain.blitToSwapchainImageComplete[frameIndex]};
    vk::PresentInfoKHR presentInfo(presentWaitSemaphores, m_swapchain.swapchain, currentImageIndex);

    vk::Result result = m_queues.present.presentKHR(&presentInfo);

    switch (result) {
    case vk::Result::eSuccess:
        break;
    case vk::Result::eSuboptimalKHR:
    case vk::Result::eErrorOutOfDateKHR:
        // vvv::Logger(vvv::WARN) << "vk::Queue::presentKHR returned << " << result;
        recreateSwapchain();
        break;
    default:
        assert(false);
    }

    stateInFlight()->incrementIndex();
}

void Application::renderFrameRecordCommands(vk::CommandBuffer commandBuffer,
                                            vvv::RendererOutput const &ldrRendererOutput) {
    assert(m_swapchain.depthFormat == vk::Format::eUndefined &&
           "This function does currently not setup depth buffering!");

    vk::ImageMemoryBarrier imageMemoryBarrier = ldrRendererOutput.texture->queueOwnershipTransfer(ldrRendererOutput.queueFamilyIndex, vk::AccessFlagBits::eShaderWrite,
                                                                                                  getQueueFamilyIndices().present.value(), vk::AccessFlagBits::eShaderRead);
    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
                                  {}, 0, nullptr, 0, nullptr,
                                  1, &imageMemoryBarrier);

    updateBlitDescriptorSet(ldrRendererOutput, currentInFlightFrameIndex());

    VkClearValue clearValues[1];
    memset(clearValues, 0, sizeof(clearValues));

    VkRenderPassBeginInfo rpBeginInfo;
    memset(&rpBeginInfo, 0, sizeof(rpBeginInfo));
    rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBeginInfo.renderPass = m_renderpass.renderpass;
    rpBeginInfo.framebuffer = m_renderpass.framebuffers[currentSwapChainImageIndex()];
    rpBeginInfo.renderArea.extent.width = m_swapchain.extent.width;
    rpBeginInfo.renderArea.extent.height = m_swapchain.extent.height;
    rpBeginInfo.clearValueCount = 1;
    rpBeginInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(commandBuffer, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_renderpass.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_renderpass.pipelineLayout, 0, 1,
                            reinterpret_cast<VkDescriptorSet const *>(&m_renderpass.descSet[currentInFlightFrameIndex()]),
                            0, nullptr);

    VkViewport viewport;
    viewport.x = viewport.y = 0;
    viewport.width = m_swapchain.extent.width;
    viewport.height = m_swapchain.extent.height;
    viewport.minDepth = 0;
    viewport.maxDepth = 1;

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor;
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent.width = viewport.width;
    scissor.extent.height = viewport.height;

    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

#ifdef IMGUI
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
#endif

    commandBuffer.endRenderPass();

    vk::ImageMemoryBarrier imageMemoryBarrierBack = ldrRendererOutput.texture->queueOwnershipTransfer(getQueueFamilyIndices().present.value(), vk::AccessFlagBits::eShaderRead,
                                                                                                      ldrRendererOutput.queueFamilyIndex, vk::AccessFlagBits::eShaderWrite);
    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eComputeShader,
                                  {}, 0, nullptr, 0, nullptr,
                                  1, &imageMemoryBarrierBack);
}

std::thread Application::execAsyncAttached() {
    std::thread guiThread(&Application::exec, this);
    return guiThread;
}

void Application::execAsync() { execAsyncAttached().detach(); }

int Application::exec() {
    if (!m_resources_acquired)
        acquireResources();

    double accum_display_time{0.0};
    size_t accum_display_frame_count{0};

    while (!glfwWindowShouldClose(m_window) && !ImGui::IsKeyDown(ImGuiKey_Escape)) {
        double startTime = glfwGetTime();
        glfwPollEvents();
        processHotKeys();

#ifdef IMGUI
        if (m_display_imgui)
            m_gui->renderGui();
        // do not capture mouse or keyboard input if in an imgui window
        m_camera_controller.updateCamera(!ImGui::GetIO().WantCaptureMouse, !ImGui::GetIO().WantCaptureKeyboard);
#else
        m_camera_controller.updateCamera(true, true);
#endif
        processParameterRecording();
        renderFrame();
        processVideoRecording();

        // update frame time tracking
        double frame_time = (glfwGetTime() - startTime) * 1000.;
        avg_ms += frame_time;
        var_ms += (frame_time * frame_time);
        min_ms = std::min(min_ms, frame_time);
        max_ms = std::max(max_ms, frame_time);
        avg_ms_samples++;

        // print FPS in window title
        accum_display_frame_count++;
        accum_display_time += frame_time;
        if (500 < accum_display_time) {
            assert(0 < accum_display_frame_count);

            std::ostringstream oss;
            double display_frame_time = accum_display_time / static_cast<double>(accum_display_frame_count);
            oss << getAppName() << "  " << (1000. / display_frame_time) << " fps (" << display_frame_time << "ms)";
            glfwSetWindowTitle(m_window, oss.str().c_str());

            accum_display_time = 0.0;
            accum_display_frame_count = 0;
        }
    }

    if (getDevice()) {
        getDevice().waitIdle();
    }

    return 0;
}

void Application::acquireResources() {
    createWindow();

    uint32_t glfwExtensionsCount;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);
    for (uint32_t i = 0; i < glfwExtensionsCount; i++) {
        enableInstanceExtension(glfwExtensions[i]);
    }

    enableDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    m_renderer->configureExtensionsAndLayersAndFeatures(this);

    createGpuContext();
    createQueues();
    createSwapChain();
    createBlit();

    m_renderer->initResources(this);
    m_renderer->initShaderResources();
    m_renderer->initSwapchainResources();

#ifdef IMGUI
    initImGui();
    m_gui->setGuiScaling(getScreenContentScale());
    m_renderer->initGui(this->getGui());
#endif

    m_resources_acquired = true;
}

void Application::createQueues() {
    m_queues.graphics = getDevice().getQueue(getQueueFamilyIndices().graphics.value(), 0);
    debugMarker->setName(m_queues.graphics, "Application.m_queues.graphics");

    m_queues.present = getDevice().getQueue(getQueueFamilyIndices().present.value(), 0);
    debugMarker->setName(m_queues.present, "Application.m_queues.present");
}

void Application::destroyQueues() {
    m_queues.present = nullptr;
    m_queues.graphics = nullptr;
}

void Application::releaseResources() {
    const auto device = getDevice();

    if (device) {
        device.waitIdle();
    }

    if (m_renderer) {
        m_renderer->releaseGui();
        m_renderer->releaseSwapchain();
        m_renderer->releaseShaderResources();
        m_renderer->releaseResources();
    }

    // some GUI components may hold GPU resources, e.g. TransferFunction2D
    m_gui->removeAllWindows();

#ifdef IMGUI
    shutdownImGui();
#endif
    destroyBlit();
    destroySwapChain();
    destroyQueues();
    destroyGpuContext();
    destroyWindow();
}

void Application::createWindow() {
    if (m_window != nullptr) {
        return;
    }

    glfwSetErrorCallback(errorCallback);

    // create glfw window
    if (!glfwInit()) {
        throw std::runtime_error("can't initialize glfw");
    }
    if (!glfwVulkanSupported()) {
        throw std::runtime_error("Vulkan not supported");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(static_cast<int>(m_startup_resolution.width),
                                static_cast<int>(m_startup_resolution.height), getAppName().c_str(),
                                m_fullscreen ? glfwGetPrimaryMonitor() : nullptr,
                                nullptr);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
    m_camera_controller.setWindow(m_window);

    GLFWimage icon = {.pixels = nullptr};
    if (vvv::Paths::hasDataPath("icons/volcanite_icon_256.png")) {
        int icon_channels;
        icon.pixels = stbi_load(vvv::Paths::findDataPath("icons/volcanite_icon_256.png").string().c_str(), &icon.width,
                                &icon.height, &icon_channels, STBI_rgb_alpha);
    }
    if (icon.pixels) {
        glfwSetWindowIcon(m_window, 1, &icon);
        stbi_image_free(icon.pixels);
    } else {
        Logger(Warn) << "Unable to load volcanite_icon_256.png application icon.";
    }
}

void Application::destroyWindow() {
    if (m_window != nullptr) {
        glfwDestroyWindow(m_window);
        glfwTerminate();
        m_window = nullptr;
    }
}

vk::SurfaceKHR Application::createSurface() {
    vk::SurfaceKHR surface = nullptr;
    VkResult err = glfwCreateWindowSurface(static_cast<VkInstance>(getInstance()), m_window, nullptr,
                                           reinterpret_cast<VkSurfaceKHR *>(&surface));
    check_vk_result(err);
    return surface;
}

void Application::createSwapChain() {
    m_swapchain.pendingRecreation = false;

    const auto surface = getSurface();
    const auto surfaceFormat = vvv::chooseSurfaceFormat(getPhysicalDevice().getSurfaceFormatsKHR(surface));
    const auto swapImageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage;

    vk::SurfaceCapabilitiesKHR surfaceCapabilities = getPhysicalDevice().getSurfaceCapabilitiesKHR(surface);

    // Note: minimal and maximal extend are identical to the current extent at least on my device.
    if (surfaceCapabilities.currentExtent.width != UINT32_MAX) {
        m_swapchain.extent = surfaceCapabilities.currentExtent;
    } else {
        int width, height;

        glfwGetFramebufferSize(m_window, &width, &height);

        m_swapchain.extent.width = std::clamp(static_cast<uint32_t>(width),
                                              surfaceCapabilities.minImageExtent.width,
                                              surfaceCapabilities.maxImageExtent.width);
        m_swapchain.extent.height = std::clamp(static_cast<uint32_t>(height),
                                               surfaceCapabilities.minImageExtent.height,
                                               surfaceCapabilities.maxImageExtent.height);
    }

    vk::SurfaceTransformFlagBitsKHR preTransform =
        (surfaceCapabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)
            ? vk::SurfaceTransformFlagBitsKHR::eIdentity
            : surfaceCapabilities.currentTransform;

    vk::CompositeAlphaFlagBitsKHR compositeAlpha = (surfaceCapabilities.supportedCompositeAlpha &
                                                    vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
                                                       ? vk::CompositeAlphaFlagBitsKHR::ePreMultiplied
                                                   : (surfaceCapabilities.supportedCompositeAlpha &
                                                      vk::CompositeAlphaFlagBitsKHR::ePostMultiplied)
                                                       ? vk::CompositeAlphaFlagBitsKHR::ePostMultiplied
                                                   : (surfaceCapabilities.supportedCompositeAlpha &
                                                      vk::CompositeAlphaFlagBitsKHR::eInherit)
                                                       ? vk::CompositeAlphaFlagBitsKHR::eInherit
                                                       : vk::CompositeAlphaFlagBitsKHR::eOpaque;

    vk::PresentModeKHR presentMode = vvv::chooseSwapPresentMode(
        getPhysicalDevice().getSurfacePresentModesKHR(surface), m_swapchain.vsync);

    const auto oldSwapchain = nullptr;

    vk::SwapchainCreateInfoKHR swapChainCreateInfo({}, surface, surfaceCapabilities.minImageCount,
                                                   surfaceFormat.format, surfaceFormat.colorSpace,
                                                   m_swapchain.extent, 1, swapImageUsage,
                                                   vk::SharingMode::eExclusive, {}, preTransform, compositeAlpha,
                                                   presentMode, true, oldSwapchain);

    uint32_t queueFamilyIndices[2] = {getQueueFamilyIndices().present.value(),
                                      getQueueFamilyIndices().graphics.value()};

    if (getQueueFamilyIndices().present != getQueueFamilyIndices().graphics) {
        // If the graphics and present queues are from different queue families, we either have to explicitly
        // transfer ownership of images between the queues, or we have to create the swapchain with imageSharingMode
        // as vk::SharingMode::eConcurrent
        swapChainCreateInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        swapChainCreateInfo.queueFamilyIndexCount = 2;
        swapChainCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
    }

    m_swapchain.swapchain = getDevice().createSwapchainKHR(swapChainCreateInfo);
    debugMarker->setName(m_swapchain.swapchain, "Application.m_swapchain.swapchain");
    m_swapchain.colorFormat = surfaceFormat.format;
    m_swapchain.images = getDevice().getSwapchainImagesKHR(m_swapchain.swapchain);

    const auto countSwapchainImages = m_swapchain.images.size();
    const auto maxInFlightFrames = MAX_FRAMES_IN_FLIGHT;

    setMultiBuffering(countSwapchainImages, maxInFlightFrames);

    m_swapchain.images.reserve(countSwapchainImages);

    vk::ComponentMapping componentMapping(vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG,
                                          vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA);
    vk::ImageSubresourceRange subResourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

    for (auto image : m_swapchain.images) {
        vk::ImageViewCreateInfo imageViewCreateInfo(vk::ImageViewCreateFlags(), image, vk::ImageViewType::e2D,
                                                    surfaceFormat.format, componentMapping, subResourceRange);

        m_swapchain.views.push_back(getDevice().createImageView(imageViewCreateInfo));
    }

    // sync primitives for swapchain
    m_swapchain.presentCompleteSemaphore.resize(maxInFlightFrames);
    m_swapchain.renderCompleteSemaphore.resize(maxInFlightFrames);
    m_swapchain.blitToSwapchainImageComplete.resize(maxInFlightFrames);

    m_swapchain.frameInFlightAwaitable.clear();
    m_swapchain.frameInFlightAwaitable.resize(maxInFlightFrames);
    m_swapchain.imageInFlightFrame.clear();
    m_swapchain.imageInFlightFrame.resize(countSwapchainImages, IMAGE_NOT_IN_FLIGHT);

    vk::SemaphoreCreateInfo semaphoreInfo{};

    for (size_t i = 0; i < maxInFlightFrames; i++) {
        m_swapchain.presentCompleteSemaphore[i] = getDevice().createSemaphore(semaphoreInfo);
        m_swapchain.renderCompleteSemaphore[i] = getDevice().createSemaphore(semaphoreInfo);
        m_swapchain.blitToSwapchainImageComplete[i] = getDevice().createSemaphore(semaphoreInfo);

        debugMarker->setName(m_swapchain.presentCompleteSemaphore[i],
                             "Application.m_swapchain.presentCompleteSemaphore." + std::to_string(i));
        debugMarker->setName(m_swapchain.renderCompleteSemaphore[i],
                             "Application.m_swapchain.renderCompleteSemaphore." + std::to_string(i));
        debugMarker->setName(m_swapchain.blitToSwapchainImageComplete[i],
                             "Application.m_swapchain.blitToSwapchainImageComplete." + std::to_string(i));
    }

    // command pools, command buffers, fences and presents etc
    // TODO: add an additional queue family if we want to present with a different present queue index?
    vk::CommandPoolCreateInfo cmdPoolInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                          getQueueFamilyIndices().graphics.value());
    m_swapchain.commandPool = getDevice().createCommandPool(cmdPoolInfo);
    debugMarker->setName(m_swapchain.commandPool, "Application.m_swapchain.commandPool");
    vk::CommandBufferAllocateInfo cmdBufferAllocInfo(m_swapchain.commandPool, vk::CommandBufferLevel::ePrimary,
                                                     swapChainImageCount());
    m_swapchain.commandBuffers = getDevice().allocateCommandBuffers(cmdBufferAllocInfo);

    for (int i = 0; i < m_swapchain.commandBuffers.size(); ++i) {
        debugMarker->setName(m_swapchain.commandBuffers[i],
                             "Application.m_swapchain.commandBuffer." + std::to_string(i));
    }
}

void Application::destroySwapChain() {
    VK_DEVICE_FREE_ALL(getDevice(), m_swapchain.commandPool, m_swapchain.commandBuffers)
    VK_DEVICE_DESTROY(getDevice(), m_swapchain.commandPool)
    VK_DEVICE_DESTROY_ALL(getDevice(), m_swapchain.renderCompleteSemaphore)
    VK_DEVICE_DESTROY_ALL(getDevice(), m_swapchain.presentCompleteSemaphore)
    VK_DEVICE_DESTROY_ALL(getDevice(), m_swapchain.blitToSwapchainImageComplete)
    VK_DEVICE_DESTROY_ALL(getDevice(), m_swapchain.views)
    m_swapchain.frameInFlightAwaitable.clear();
    m_swapchain.imageInFlightFrame.clear();
    m_swapchain.images.clear();
    VK_DEVICE_DESTROY(getDevice(), m_swapchain.swapchain)
}

void Application::createBlit() {
    createBlitDescriptorSet();
    createBlitShaders();
    createBlitRenderPass();
    createBlitFramebuffers();
    createBlitPipeline();
}

void Application::destroyBlit() {
    destroyBlitPipeline();
    destroyBlitFramebuffers();
    destroyBlitRenderPass();
    destroyBlitShaders();
    destroyBlitDescriptorSet();
}

void Application::createBlitFramebuffers() {
    m_renderpass.framebuffers.resize(m_swapchain.views.size(), nullptr);
    for (size_t i = 0; i < m_swapchain.views.size(); i++) {
        std::vector<vk::ImageView> attachments = {
            m_swapchain.views[i],
        };

        vk::FramebufferCreateInfo framebufferInfo({}, m_renderpass.renderpass, attachments,
                                                  m_swapchain.extent.width, m_swapchain.extent.height, 1);

        m_renderpass.framebuffers[i] = getDevice().createFramebuffer(framebufferInfo);
        debugMarker->setName(m_renderpass.framebuffers[i],
                             "Application.m_renderpass.framebuffers." + std::to_string(i));
    }
}

void Application::destroyBlitFramebuffers() { VK_DEVICE_DESTROY_ALL(getDevice(), m_renderpass.framebuffers) }

void Application::createBlitDescriptorSet() {
    vk::Device device = getDevice();
    const auto maxInFlightFrames = maximalInFlightFrameCount();
    assert(maxInFlightFrames > 0);

    const auto descType = vk::DescriptorType::eCombinedImageSampler;

    std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
        // Binding 0: Input image (read-only)
        vk::DescriptorSetLayoutBinding(0, descType, 1, vk::ShaderStageFlagBits::eFragment),
    };

    vk::DescriptorSetLayoutCreateInfo descSetLayoutCreateInfo({}, setLayoutBindings);
    m_renderpass.descSetLayout = device.createDescriptorSetLayout(descSetLayoutCreateInfo);

    vk::DescriptorPoolSize poolSize(descType, setLayoutBindings.size() * maxInFlightFrames);
    m_renderpass.descPool = device.createDescriptorPool(
        vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, maxInFlightFrames,
                                     poolSize));
    debugMarker->setName(m_renderpass.descPool, "Application.m_renderpass.descPool");

    const std::vector<vk::DescriptorSetLayout> descriptorSetLayouts(maxInFlightFrames, m_renderpass.descSetLayout);
    vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo(m_renderpass.descPool, descriptorSetLayouts);
    m_renderpass.descSet = device.allocateDescriptorSets(descriptorSetAllocateInfo);

    for (int i = 0; i < m_renderpass.descSet.size(); ++i) {
        debugMarker->setName(m_renderpass.descSet[i], "Application.m_renderpass.descSet." + std::to_string(i));
    }

    assert(m_renderpass.descSet.size() == maxInFlightFrames);

    // this variable is used to optimize descriptor writes by memorizing the descriptor set state
    m_renderpass.lastImageDescriptor.resize(maxInFlightFrames);
}

void Application::destroyBlitDescriptorSet() {
    m_renderpass.lastImageDescriptor = {};

    VK_DEVICE_FREE_ALL(getDevice(), m_renderpass.descPool, m_renderpass.descSet)
    VK_DEVICE_DESTROY(getDevice(), m_renderpass.descPool)
    VK_DEVICE_DESTROY(getDevice(), m_renderpass.descSetLayout)
}

void Application::updateBlitDescriptorSet(const vvv::RendererOutput &output, uint32_t inFlightFrameIdx) {
    auto lastImageDescriptor = m_renderpass.lastImageDescriptor[inFlightFrameIdx];

    // In theory there should never be a need to update the descriptor set when the inner rendering engine
    // performs simple ring buffering with a buffersize equal to the swapchain size. So early out when possible.
    if (lastImageDescriptor == output.texture->descriptor) {
        return;
    }

    const std::vector imageDescriptors{output.texture->descriptor};
    vk::WriteDescriptorSet writeDescriptorSet = vk::WriteDescriptorSet(m_renderpass.descSet[inFlightFrameIdx], 0, 0,
                                                                       vk::DescriptorType::eCombinedImageSampler,
                                                                       imageDescriptors);
    static_cast<vk::Device>(getDevice()).updateDescriptorSets({writeDescriptorSet}, {});
    m_renderpass.lastImageDescriptor[inFlightFrameIdx] = output.texture->descriptor;
}

void Application::createBlitShaders() {
    const auto shaderDirectory = vvv::getShaderIncludeDirectory();

    m_renderpass.shaderFragment = new vvv::Shader(
        SimpleGlslShaderRequest{.filename = "blit.frag", .label = "Application.m_shaderFragment"});
    m_renderpass.shaderVertex = new vvv::Shader(
        SimpleGlslShaderRequest{.filename = "blit.vert", .label = "Application.m_shaderVertex"});
}

void Application::destroyBlitShaders() {
    if (m_renderpass.shaderVertex != nullptr) {
        m_renderpass.shaderVertex->destroyModule(getDevice());
        delete m_renderpass.shaderVertex;
        m_renderpass.shaderVertex = nullptr;
    }
    if (m_renderpass.shaderFragment != nullptr) {
        m_renderpass.shaderFragment->destroyModule(getDevice());
        delete m_renderpass.shaderFragment;
        m_renderpass.shaderFragment = nullptr;
    }
}

void Application::createBlitRenderPass() {
    vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear;
    vk::ImageLayout colorFinalLayout = vk::ImageLayout::ePresentSrcKHR;

    std::vector<vk::AttachmentDescription> attachmentDescriptions;
    assert(m_swapchain.colorFormat != vk::Format::eUndefined);
    attachmentDescriptions.emplace_back(vk::AttachmentDescriptionFlags(), m_swapchain.colorFormat,
                                        vk::SampleCountFlagBits::e1, loadOp, vk::AttachmentStoreOp::eStore,
                                        vk::AttachmentLoadOp::eDontCare,
                                        vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined,
                                        colorFinalLayout);
    if (m_swapchain.depthFormat != vk::Format::eUndefined) {
        attachmentDescriptions.emplace_back(vk::AttachmentDescriptionFlags(), m_swapchain.depthFormat,
                                            vk::SampleCountFlagBits::e1, loadOp, vk::AttachmentStoreOp::eDontCare,
                                            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
                                            vk::ImageLayout::eUndefined,
                                            vk::ImageLayout::eDepthStencilAttachmentOptimal);
    }
    vk::AttachmentReference colorAttachment(0, vk::ImageLayout::eColorAttachmentOptimal);
    vk::AttachmentReference depthAttachment(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);
    vk::SubpassDescription subpassDescription(vk::SubpassDescriptionFlags(), vk::PipelineBindPoint::eGraphics, {},
                                              colorAttachment, {},
                                              (m_swapchain.depthFormat != vk::Format::eUndefined) ? &depthAttachment
                                                                                                  : nullptr);

    m_renderpass.renderpass = getDevice().createRenderPass(
        vk::RenderPassCreateInfo(vk::RenderPassCreateFlags(), attachmentDescriptions, subpassDescription));
}

void Application::destroyBlitRenderPass() { VK_DEVICE_DESTROY(getDevice(), m_renderpass.renderpass) }

void Application::createBlitPipeline() {
    m_renderpass.pipelineLayout = getDevice().createPipelineLayout(
        vk::PipelineLayoutCreateInfo({}, m_renderpass.descSetLayout));
    debugMarker->setName(m_renderpass.pipelineLayout, "Application.m_renderpass.pipelineLayout");

    std::array<vk::PipelineShaderStageCreateInfo, 2> pipelineShaderStageCreateInfos = {
        *m_renderpass.shaderVertex->pipelineShaderStageCreateInfo(this),
        *m_renderpass.shaderFragment->pipelineShaderStageCreateInfo(this),
    };

    vk::PipelineVertexInputStateCreateInfo emptyVertexInputState;

    vk::PipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo(
        vk::PipelineInputAssemblyStateCreateFlags(), vk::PrimitiveTopology::eTriangleList);

    vk::PipelineViewportStateCreateInfo pipelineViewportStateCreateInfo(vk::PipelineViewportStateCreateFlags(), 1,
                                                                        nullptr, 1, nullptr);

    vk::PipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo(
        vk::PipelineRasterizationStateCreateFlags(), false, false, vk::PolygonMode::eFill,
        vk::CullModeFlagBits::eNone,
        vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f);

    vk::PipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo({}, vk::SampleCountFlagBits::e1);

    const bool depthBuffered = false;

    vk::StencilOpState stencilOpState(vk::StencilOp::eKeep, vk::StencilOp::eKeep, vk::StencilOp::eKeep,
                                      vk::CompareOp::eAlways);

    vk::PipelineDepthStencilStateCreateInfo pipelineDepthStencilStateCreateInfo(
        vk::PipelineDepthStencilStateCreateFlags(), depthBuffered, depthBuffered, vk::CompareOp::eLessOrEqual,
        false, false,
        stencilOpState, stencilOpState);

    vk::ColorComponentFlags colorComponentFlags(
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
        vk::ColorComponentFlagBits::eA);

    vk::PipelineColorBlendAttachmentState pipelineColorBlendAttachmentState(false, vk::BlendFactor::eZero,
                                                                            vk::BlendFactor::eZero,
                                                                            vk::BlendOp::eAdd,
                                                                            vk::BlendFactor::eZero,
                                                                            vk::BlendFactor::eZero,
                                                                            vk::BlendOp::eAdd, colorComponentFlags);

    vk::PipelineColorBlendStateCreateInfo pipelineColorBlendStateCreateInfo(
        vk::PipelineColorBlendStateCreateFlags(), false, vk::LogicOp::eNoOp, pipelineColorBlendAttachmentState,
        {{1.0f, 1.0f, 1.0f, 1.0f}});

    std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo(vk::PipelineDynamicStateCreateFlags(),
                                                                      dynamicStates);

    vk::GraphicsPipelineCreateInfo graphicsPipelineCreateInfo(vk::PipelineCreateFlags(),
                                                              pipelineShaderStageCreateInfos,
                                                              &emptyVertexInputState,
                                                              &pipelineInputAssemblyStateCreateInfo, nullptr,
                                                              &pipelineViewportStateCreateInfo,
                                                              &pipelineRasterizationStateCreateInfo,
                                                              &pipelineMultisampleStateCreateInfo,
                                                              &pipelineDepthStencilStateCreateInfo,
                                                              &pipelineColorBlendStateCreateInfo,
                                                              &pipelineDynamicStateCreateInfo,
                                                              m_renderpass.pipelineLayout,
                                                              m_renderpass.renderpass);

    auto result = getDevice().createGraphicsPipeline(m_renderpass.pipelineCache, graphicsPipelineCreateInfo);
    assert(result.result == vk::Result::eSuccess);
    m_renderpass.pipeline = result.value;
    debugMarker->setName(m_renderpass.pipeline, "Application.m_renderpass.pipeline");
}

void Application::destroyBlitPipeline(){
    VK_DEVICE_DESTROY(getDevice(), m_renderpass.pipeline)
        VK_DEVICE_DESTROY(getDevice(), m_renderpass.pipelineLayout)}

vk::Extent2D Application::getScreenExtent() const {
    return m_swapchain.extent;
}

void Application::processHotKeys() {
    if (ImGui::GetIO().WantCaptureKeyboard)
        return;

    // shader reload
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        vvv::Logger(vvv::Info) << "reloading shaders";
        recreateShaderResources();
        writePipelineCacheToDisk(getDevice());
    }

    // parameter quick store / load
    if (!m_quick_access_file_fmt.empty()) {
        constexpr ImGuiKey quick_keys[10] = {ImGuiKey_0, ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5,
                                             ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9};
        for (int slot = 0; slot <= 9; slot++) {
            if (ImGui::IsKeyPressed(quick_keys[slot])) {
                const std::string path = fmt::vformat(m_quick_access_file_fmt, fmt::make_format_args(slot));
                // ctrl pressed: store. not pressed: load
                if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) {
                    if (!m_renderer->writeParameterFile(path)) {
                        Logger(Warn) << "Could not write configuration file " << path;
                    }
                } else if (std::filesystem::exists(path))
                    m_renderer->readParameterFile(path);
                break;
            }
        }
    }

    // record camera path and time stamps
    if (!m_record_in.has_value() && !m_video_frame.has_value() && ImGui::IsKeyPressed(ImGuiKey_F9)) {
        // stop recording of camera path
        if (m_record_out.has_value()) {
            m_record_out->close();
            m_record_out = {};
            if (m_video_timing.has_value()) {
                m_video_timing->close();
                m_video_timing = {};
                vvv::Logger(vvv::Info) << "compute video file from frames in " << m_video_file_path << " with:";
                vvv::Logger(vvv::Info) << " ffmpeg -f concat -safe 0 -i video_timing.txt video.mp4";
            }

            // output timing of path
            avg_ms /= static_cast<double>(avg_ms_samples);
            var_ms /= static_cast<double>(avg_ms_samples);
            vvv::Logger(vvv::Info) << "min / avg (std.dev.) / max [ms/frame]";
            vvv::Logger(vvv::Info) << std::fixed << std::setprecision(0) << min_ms << " / " << avg_ms << " ("
                                   << std::sqrt(var_ms - (avg_ms * avg_ms)) << ") " << " / " << max_ms
                                   << " | " << avg_ms_samples << " frames rendered.";
        } else {
            m_record_out = std::ofstream(m_record_file_path, std::ios::out | std::ios::binary);
            if (!m_record_out->is_open()) {
                vvv::Logger(vvv::Warn) << "could not open recording output file " << m_record_file_path;
                m_record_out = {};
                return;
            }

            // create an output file for our timings
            m_video_timing = std::ofstream(m_video_file_path + "_timing.txt", std::ios::out);
            if (!m_video_timing->is_open()) {
                vvv::Logger(vvv::Warn) << "could not open video timing file " << m_video_file_path << "_timing.txt";
                m_video_timing = {};
            }
            m_video_last_timestamp = glfwGetTime();

            min_ms = 9999999999.;
            avg_ms = 0.;
            var_ms = 0.;
            max_ms = 0.;
            avg_ms_samples = 0;
        }
    }
    // replay camera path
    else if (!m_record_out.has_value() && !m_video_timing.has_value() && !m_video_frame.has_value() &&
             (ImGui::IsKeyPressed(ImGuiKey_F10) || ImGui::IsKeyPressed(ImGuiKey_F11))) {
        // stop replay
        if (m_record_in.has_value()) {
            m_record_in->close();
            m_record_in = {};

            // output timing of path
            avg_ms /= static_cast<double>(avg_ms_samples);
            var_ms /= static_cast<double>(avg_ms_samples);
            vvv::Logger(vvv::Warn) << std::fixed << std::setprecision(0) << min_ms << " / " << avg_ms
                                   << " ($\\sigma=" << std::sqrt(var_ms - (avg_ms * avg_ms)) << "$) " << " / "
                                   << max_ms << " total avg ms " << avg_ms << " | " << avg_ms_samples << " frames rendered.";
        }
        // start replay
        else {
            m_record_in = std::ifstream(m_record_file_path, std::ios::in | std::ios::binary);
            if (!m_record_in->is_open()) {
                vvv::Logger(vvv::Warn) << "could not open recording input file " << m_record_file_path;
                m_record_in = {};
            }

            min_ms = 9999999999.;
            avg_ms = 0.;
            var_ms = 0.;
            max_ms = 0.;
            avg_ms_samples = 0;
        }
        m_video_frame_count = 0u;
    }
    // output images for camera path
    else if (!m_record_out.has_value() && !m_record_in.has_value() && !m_video_frame.has_value() &&
             !m_video_timing.has_value() && ImGui::IsKeyPressed(ImGuiKey_F12)) {
        // open the camera path file
        m_record_in = std::ifstream(m_record_file_path, std::ios::in | std::ios::binary);
        if (!m_record_in->is_open()) {
            vvv::Logger(vvv::Warn) << "could not open recording input file " << m_record_file_path;
            m_record_in = {};
            return;
        }
        m_video_frame = 0;
    } else if (ImGui::IsKeyPressed(ImGuiKey_F1)) {
        m_display_imgui = false;
    } else if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
        m_display_imgui = true;
    }
}

void Application::recreateShaderResources() {
    if (!getDevice()) {
        return;
    }

    getDevice().waitIdle();

    m_renderer->releaseSwapchain();
    m_renderer->releaseShaderResources();

    m_renderer->initShaderResources();
    m_renderer->initSwapchainResources();
}

void Application::recreateInnerRenderingEngine() {

    if (!getDevice()) {
        return;
    }

    getDevice().waitIdle();

    m_renderer->releaseGui();
    m_gui->removeAllWindows();
    m_renderer->releaseSwapchain();
    m_renderer->releaseShaderResources();
    m_renderer->releaseResources();

    m_renderer->initResources(this);
    m_renderer->initShaderResources();
    m_renderer->initSwapchainResources();
    m_renderer->initGui(this->getGui());
}

void Application::processParameterRecording() {
    // write
    if (m_record_out.has_value()) {
        getCamera()->writeTo(m_record_out.value());
    }
    // read
    else if (m_record_in.has_value()) {
        getCamera()->readFrom(m_record_in.value());
        if (m_record_in->eof()) {
            m_record_in->close();
            m_record_in = {};

            // output timing of path
            avg_ms /= static_cast<double>(avg_ms_samples);
            var_ms /= static_cast<double>(avg_ms_samples);
            vvv::Logger(vvv::Warn) << std::fixed << std::setprecision(0) << min_ms << " / " << avg_ms
                                   << " ($\\sigma=" << std::sqrt(var_ms - (avg_ms * avg_ms)) << "$) " << " / "
                                   << max_ms << " | " << avg_ms_samples << " frames rendered.";
        }
    }
}

void Application::processVideoRecording() {
    // write time stamps
    if (m_video_timing.has_value()) {
        *m_video_timing << "file '" << m_video_file_path << "_" << m_video_frame_count << ".png'" << std::endl;
        double new_time = glfwGetTime();
        *m_video_timing << "duration " << (new_time - m_video_last_timestamp) << std::endl;
        m_video_frame_count++;
        m_video_last_timestamp = new_time;
    }
}

#ifdef IMGUI

void setImGuiStyle() {
    // start with the light colors
    ImGui::StyleColorsLight();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 0.f;
    style.FrameRounding = 0.f;
    style.ScrollbarRounding = 0;
    style.Alpha = 0.75f;

    // progress bars / histograms should use calm colors as well
    style.Colors[ImGuiCol_PlotHistogram] = style.Colors[ImGuiCol_Button];
}

void Application::initImGui() {
    auto device = getDevice();

    // create vulkan objects for ImGui (only the descriptor pool so far)
    // descriptor pool
    vk::DescriptorPoolSize pool_sizes[] =
        {
            {vk::DescriptorType::eSampler, 1000},
            {vk::DescriptorType::eCombinedImageSampler, 1000},
            {vk::DescriptorType::eSampledImage, 1000},
            {vk::DescriptorType::eStorageImage, 1000},
            {vk::DescriptorType::eUniformTexelBuffer, 1000},
            {vk::DescriptorType::eStorageTexelBuffer, 1000},
            {vk::DescriptorType::eUniformBuffer, 1000},
            {vk::DescriptorType::eStorageBuffer, 1000},
            {vk::DescriptorType::eUniformBufferDynamic, 1000},
            {vk::DescriptorType::eStorageBufferDynamic, 1000},
            {vk::DescriptorType::eInputAttachment, 1000}};
    vk::DescriptorPoolCreateInfo pool_info = {};
    pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    pool_info.maxSets = 1000; // * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    m_imgui.descPool = device.createDescriptorPool(pool_info);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    recreateSwapchainImGui();

    setImGuiStyle();

    ImGui_ImplGlfw_InitForVulkan(m_window, true);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = getInstance();
    init_info.PhysicalDevice = getPhysicalDevice();
    init_info.Device = device;
    init_info.QueueFamily = getQueueFamilyIndices().graphics.value();
    init_info.Queue = m_queues.graphics;
    init_info.PipelineCache = m_renderpass.pipelineCache;
    init_info.DescriptorPool = m_imgui.descPool;
    init_info.Allocator = nullptr;
    init_info.MinImageCount = 2; // m_imgui.minImageCount; for whatever reason minImageCount is 3 and maxInFlightFrames is 2 here.. so we wait for the swapchain recreation to fix it
    init_info.ImageCount = maximalInFlightFrameCount();
    init_info.CheckVkResultFn = check_vk_result;
    init_info.RenderPass = m_renderpass.renderpass;
    ImGui_ImplVulkan_Init(&init_info);

    m_imgui.initialized = true;
}

void Application::shutdownImGui() {
    if (!m_imgui.initialized)
        return;

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    // destroy vulkan objects
    VK_DEVICE_DESTROY(getDevice(), m_imgui.descPool)

    m_imgui.initialized = false;
}

void Application::recreateSwapchainImGui() {
    vk::SurfaceCapabilitiesKHR surfaceCapabilities = getPhysicalDevice().getSurfaceCapabilitiesKHR(getSurface());
    if (m_imgui.minImageCount > surfaceCapabilities.minImageCount) {
        m_imgui.minImageCount = surfaceCapabilities.minImageCount;
        if (m_imgui.initialized)
            ImGui_ImplVulkan_SetMinImageCount(m_imgui.minImageCount);
    }
}

#endif

void Application::logLibraryAvailabilty() {
    vvv::logLibraryAvailabilty();
#ifdef IMGUI
    vvv::Logger(vvv::Debug) << "ImGUI " + std::string(ImGui::GetVersion()) << +" available.";
#endif
}

float Application::getScreenContentScale() const {
    float contentScaleX, contentScaleY;
    glfwGetMonitorContentScale(glfwGetPrimaryMonitor(), &contentScaleX, &contentScaleY);
    return std::max(contentScaleX, contentScaleY);
}

void Application::setWindowSize(int width, int height) const {
    if (m_window)
        glfwSetWindowSize(m_window, width, height);
}

void Application::setWindowResizable(bool resizable) const {
    if (m_window)
        glfwSetWindowAttrib(m_window, GLFW_RESIZABLE, resizable);
}

bool Application::isWindowResizable() const {
    if (m_window)
        return static_cast<bool>(glfwGetWindowAttrib(m_window, GLFW_RESIZABLE));
    return false;
}

void Application::framebufferResizeCallback(GLFWwindow *window, int _width, int _height) {
    auto app = reinterpret_cast<Application *>(glfwGetWindowUserPointer(window));
    app->m_swapchain.pendingRecreation = true;
}

} // namespace vvv
