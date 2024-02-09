#include "vvvwindow/App.hpp"
#include <vvv/vk/destroy.hpp>
#include <vvv/vk/swapchain.hpp>
#include <vvv/util/Logger.hpp>

#ifdef IMGUI
#include "imgui/imgui.h"
#include "imgui/implot/implot.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#endif

// TODO(Reiner): make this configurable for triple buffering
// TODO(Reiner): make sure this value is consistent with the swapchain image count: swapchain image count > WSI->getMaxFramesInFlight()
const uint32_t MAX_FRAMES_IN_FLIGHT = 2;
const auto IMAGE_NOT_IN_FLIGHT = std::numeric_limits<size_t>::max();

double Application::s_mouse_scroll_wheel = 0.f;

static void check_vk_result(VkResult err) {
    if (err != 0) {
        std::cerr << "Vulkan error " << vk::to_string(static_cast<vk::Result>(err));
        if (err < 0) {
            abort();
        }
    }
}

static void check_vk_result(vk::Result err) { check_vk_result(static_cast<VkResult>(err)); }

void Application::recreateSwapchain() {
    // TODO(Reiner): use new API, otherwise not well defined
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
    const auto device = getDevice();

    if (m_swapchain.frameInFlightAwaitable[frameIndex])
        sync->hostWaitOnDevice(vvv::AwaitableList{ m_swapchain.frameInFlightAwaitable[frameIndex] });
    stateInFlight()->cleanKeepAlives(frameIndex);

    // TODO(Reiner): mark here the planing state protected by the fence as executed state? further below is also possible, but whats correct? what has the tighter bounds?
    // a signal to a fence means the state has executed => can be marked => we observe the executed state when waiting for the fence, so thats the right point to mark the planed state as executed.
    // the planned state that is signaled, is everything before the submit and the submit itself => need to record the planing state after the submit.

    // Since device->acquireNextImageKHR throws an Exception in case of an outOfDateKHR even if we ask for a return value, we use the old-fashioned way without the Vulkan.hpp wrapper.
    // std::vector<vk::Semaphore> acquiteSwapchainImageSignalSemaphores = {m_swapchain.startFrameSemaphore[frameIndex], m_swapchain.presentCompleteSemaphore[frameIndex]};
    uint32_t currentImageIndex;
    VkResult nextImageResult = vkAcquireNextImageKHR(getDevice(), m_swapchain.swapchain, UINT64_MAX, m_swapchain.presentCompleteSemaphore[frameIndex], nullptr, &currentImageIndex);
    stateSwapchain()->setActiveIndex(currentImageIndex);

    switch (vk::Result(nextImageResult)) {
    case vk::Result::eSuccess:
        break;
    case vk::Result::eSuboptimalKHR:
        vvv::Logger(vvv::WARN) << "VK_SUBOPTIMAL_KHR: A swapchain no longer matches the surface properties exactly (returned from vkAcquireNextImageKHR)";
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
            sync->hostWaitOnDevice(vvv::AwaitableList { m_swapchain.frameInFlightAwaitable[fenceIdx] });
    }
    m_swapchain.imageInFlightFrame[currentImageIndex] = frameIndex;


    // ------------------------ RECORD WORK FOR THE GPU

    // TODO(Reiner): looks like you only need one command buffer if you submit with eOneTimeSubmit bit???
    const auto commandBuffer = m_swapchain.commandBuffers[currentImageIndex];

    //std::vector<vk::Semaphore> childRendererWaitSemaphores{m_swapchain.presentCompleteSemaphore[frameIndex]};
    //std::vector<vk::PipelineStageFlags> childRendererWaitStages{vk::PipelineStageFlagBits::eAllCommands};

    //const auto ldrRendererOutput = m_renderer->renderNextFrame(childRendererWaitSemaphores, childRendererWaitStages);
    // TODO(Patrick): remove awaitBinaryAwaitableList everywhere
    const auto ldrRendererOutput = m_renderer->renderNextFrame({}, {});

    // Note: we do a one-time submit below, which automatically invalidates the command buffer. The reset to the initial
    // state required per specification, is implicitly performed by `commandBuffer.begin`. So fencing the command
    // buffer within a `begin` and `end` pair is enough for a complete, valid lifecycle of the command buffer.
    commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    renderFrameRecordCommands(commandBuffer, ldrRendererOutput);
    commandBuffer.end();

    // ------------------------ SUBMIT THE WORK TO THE GPU

    // TODO(Reiner): the sample uses the frameIdx as we do here instead of a swapchain idx??? should not be valid?
    std::array<vk::CommandBuffer, 1> commandBuffers = {commandBuffer};

    //std::vector<vk::Semaphore> graphicsWaitSemaphores(ldrRendererOutput.renderingCompleteSemaphores);
    //std::vector<vk::PipelineStageFlags> graphicsWaitStages(ldrRendererOutput.renderingCompleteStageMasks);
    //std::vector<vk::Semaphore> graphicsSignalSemaphores(ldrRendererOutput.usageCompleteSemaphores);
    //graphicsSignalSemaphores.push_back(m_swapchain.renderCompleteSemaphore[frameIndex]);
    //vk::SubmitInfo submitInfo(graphicsWaitSemaphores, graphicsWaitStages, commandBuffers, graphicsSignalSemaphores);
    //m_queues.graphics.submit(submitInfo, m_swapchain.inFlightFences[frameIndex]); // fence signaled on complete


    // make sure the swapchain allows us to write again. Since we only sync against the blit, we are guaranteed to
    // have the right queue type for `eColorAttachmentOutput`. If the sync against the swapchain would be passed
    // into the inner renderer, this would not be guaranteed. The inner renderer for example, could be compute queue only.
    // This would force us to use `eAllCommands` -- which would unnecessary restrict parallelism.
    vvv::BinaryAwaitableList swapchainPresentComplete { std::make_shared<vvv::BinaryAwaitable>(vvv::BinaryAwaitable {
        .semaphore = m_swapchain.presentCompleteSemaphore[frameIndex],
        .stages = vk::PipelineStageFlagBits::eAllCommands,  //vk::PipelineStageFlagBits::eColorAttachmentOutput,
    })
    };

    // save video images
    if(m_video_frame.has_value()) {
        if(!m_record_in.has_value() || m_record_in->eof()) {
            m_video_frame = {};
        }
        else {
            ldrRendererOutput.texture->writePng(m_video_file_path + "_" + std::to_string(m_video_frame.value()) + ".png");
            m_video_frame = m_video_frame.value() + 1;
        }
    }

    const auto renderingUsageCompleteAwaitable = sync->submit(
        commandBuffer, getQueue(), ldrRendererOutput.renderingComplete, vk::PipelineStageFlagBits::eAllCommands, swapchainPresentComplete,
        &m_swapchain.blitToSwapchainImageComplete[frameIndex]);
    m_swapchain.frameInFlightAwaitable[frameIndex] = renderingUsageCompleteAwaitable;

    std::array<vk::Semaphore, 1> presentWaitSemaphores = {m_swapchain.blitToSwapchainImageComplete[frameIndex]};
    vk::PresentInfoKHR presentInfo(presentWaitSemaphores, m_swapchain.swapchain, currentImageIndex);

    // TODO(Reiner): why the hack are we presenting on the graphics queue and not the present queue? :D Add a sync barrier if the index differs
    // and present on the present queue
    vk::Result result = m_queues.graphics.presentKHR(&presentInfo);

    switch (result) {
    case vk::Result::eSuccess:
        break;
    case vk::Result::eSuboptimalKHR:
    case vk::Result::eErrorOutOfDateKHR:
        // TODO(Reiner): resize swapchain
        std::cerr << "vk::Queue::presentKHR returned << " << result << " !\n" << std::flush;
        recreateSwapchain();
        break;
    default:
        assert(false);
    }

    stateInFlight()->incrementIndex();
}

void Application::renderFrameRecordCommands(vk::CommandBuffer commandBuffer, vvv::RendererOutput const &ldrRendererOutput) {
    assert(m_swapchain.depthFormat == vk::Format::eUndefined && "This function does currently not setup depth buffering!");

    // SYNCHRONIZE BETWEEN CHILD RENDER AND BLIT RENDERER
    //
    // TODO(Reiner): its currently pretty unclear how this barrier should be generated more generically, e.g.
    // it's a good bet that we get a shader_write from compute or fragment, but it could be other stuff like a copy from disk
    // or whatever. Should we forward barrier info, or should we expect the other side to do the sync???
    vk::ImageMemoryBarrier imageMemoryBarrier;
    // TODO(Reiner): the layouts also have to match the release barrier!
    imageMemoryBarrier.oldLayout = ldrRendererOutput.texture->descriptor.imageLayout; // TODO(Reiner): would make sense to keep the layout as long as its valid for reading
    imageMemoryBarrier.newLayout = vk::ImageLayout::eGeneral;
    imageMemoryBarrier.image = ldrRendererOutput.texture->image;
    imageMemoryBarrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite; // TODO(Reiner): this stage was split in sync2
    imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;  // TODO(Reiner): this stage was split in sync2

    // TODO(Reiner): this needs to be filled for a queue transfer, right? or does this work since I marked that shit concurrent queue shared?
    // imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    // imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.srcQueueFamilyIndex = ldrRendererOutput.queueFamilyIndex;
    imageMemoryBarrier.dstQueueFamilyIndex = getQueueFamilyIndices().graphics.value();

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         reinterpret_cast<const VkImageMemoryBarrier *>(&imageMemoryBarrier));

    updateBlitDescriptorSet(ldrRendererOutput, currentInFlightFrameIndex());

    // TODO(Reiner): max put this here, but what the heck is the third attachement that is zeroed out here?
    VkClearColorValue clearColor = {{0, 0, 0, 1}};
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
                            reinterpret_cast<VkDescriptorSet const *>(&m_renderpass.descSet[currentInFlightFrameIndex()]), 0, nullptr);

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

    // Release queue owner ship, Note: not required if the prior iteration does not care about its contents
    // TODO(Reiner): its currently pretty unclear how this barrier should be generated more generically, e.g.
    // it's a good bet that we get a shader_write from compute or fragment, but it could be other stuff like a copy from disk
    // or whatever. Should we forward barrier info, or should we expect the other side to do the sync???
    //    VkImageMemoryBarrier releaseImageMemoryBarrier = {};
    //    releaseImageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    //    releaseImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    //    releaseImageMemoryBarrier.newLayout = static_cast<VkImageLayout>(ldrRendererOutput.imageDescriptor.imageLayout);
    //    releaseImageMemoryBarrier.image = ldrRendererOutput.image;
    //    releaseImageMemoryBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    //    releaseImageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;  // TODO(Reiner): this stage was split in sync2
    //    releaseImageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT; // TODO(Reiner): this stage was split in sync2
    //
    //    // TODO(Reiner): this needs to be filled for a queue transfer, right? or does this work since I marked that shit concurrent queue shared?
    //    // imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    //    // imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    //    releaseImageMemoryBarrier.srcQueueFamilyIndex = getQueueFamilyIndices().graphics;
    //    releaseImageMemoryBarrier.dstQueueFamilyIndex = getQueueFamilyIndices().compute;

    // vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &releaseImageMemoryBarrier);
}

std::thread Application::execAsyncAttached() {
    std::thread guiThread(&Application::exec, this);
    return guiThread;
}

void Application::execAsync() { execAsyncAttached().detach(); }

int Application::exec() {
    if(!m_resources_acquired)
        acquireResources();

    double accumulatedTime{0.0};
    size_t frameCount{0};

    while (!glfwWindowShouldClose(m_window)) {
        double startTime = glfwGetTime();
        glfwPollEvents();
        processHotKeys();

#ifdef IMGUI
        if(m_display_imgui)
            m_gui->renderGui();
#endif

        updateCamera();
        processParameterRecording();
        renderFrame();
        processVideoRecording();

        // print FPS in window title
        double endTime = glfwGetTime();
        accumulatedTime += endTime - startTime;
        ++frameCount;
        if (0.5 < accumulatedTime) {
            assert(0 < frameCount);

            std::ostringstream oss;
            double frame_time = accumulatedTime/static_cast<double>(frameCount);
            oss << getAppName() << "  " << 1. / frame_time << " fps (" << 1000.f*frame_time << "ms)";
            glfwSetWindowTitle(m_window, oss.str().c_str());

            accumulatedTime = 0.0;
            frameCount = 0;

            frame_time *= 1000.;
            avg_ms += frame_time;
            var_ms += (frame_time * frame_time);
            min_ms = std::min(min_ms, frame_time);
            max_ms = std::max(max_ms, frame_time);
            avg_ms_samples++;
        }
    }

    if (getDevice()) {
        getDevice().waitIdle();
    }

    // old: this destroyWindow() here ensures the window is immediately closed, but allows following code
    // to inspect vulkan state, e.g. download buffers etc, before all GPU state
    // is destroyed.
    // ----
    // Note from Max: this gives segfaults. Destroy the window at the end of releaseResources() instead
    // When destroying the window here, glfwTerminate() sometimes gives a segfault / a corrupted double linked list
    // within XCloseDisplay.
    // Could be: https://github.com/KhronosGroup/Vulkan-LoaderAndValidationLayers/issues/1894
    // http://www.xfree86.org/4.7.0/DRI11.html suggests that the (GL, but Vulkan here) can register a callback with Xlib.
    // When the application calls XCloseDisplay, this callback is called and will segfault if the driver had already
    // been unloaded, which could happen when the Vulkan instance is destroyed. Fix is to destroy the instance after
    // cleaning up the display connection.
//    destroyWindow();

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
        // TODO(Reiner): use sync to resolve waiting counting semaphores
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
    m_window = glfwCreateWindow(static_cast<int>(m_startup_resolution.width), static_cast<int>(m_startup_resolution.height), getAppName().c_str(), nullptr, nullptr);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetScrollCallback(m_window, &Application::glfwUpdateScrollWheel);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
}

void Application::destroyWindow() {
    if (m_window != nullptr) {
        glfwDestroyWindow(m_window);
        glfwTerminate(); // TODO(Reiner): when do we have to call `glfwTerminate`, inside or outside the if-conditional
        m_window = nullptr;
    }
}

vk::SurfaceKHR Application::createSurface() {
    vk::SurfaceKHR surface = nullptr;
    VkResult err = glfwCreateWindowSurface(static_cast<VkInstance>(getInstance()), m_window, nullptr, reinterpret_cast<VkSurfaceKHR *>(&surface));
    check_vk_result(err);
    return surface;
}

void Application::createSwapChain() {
    m_swapchain.pendingRecreation = false;

    const auto surface = getSurface();
    const auto surfaceFormat = vvv::chooseSurfaceFormat(getPhysicalDevice().getSurfaceFormatsKHR(surface));

    // TODO(Reiner): provide a method `customizeSwapchainImageUsage` among other hooks to not end up like Qt6
    const auto swapImageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage;

    vk::SurfaceCapabilitiesKHR surfaceCapabilities = getPhysicalDevice().getSurfaceCapabilitiesKHR(surface);

    // Note: minimal and maximal extend are identical to the current extent at least on my device.
    if (surfaceCapabilities.currentExtent.width != UINT32_MAX) {
        m_swapchain.extent = surfaceCapabilities.currentExtent;
    } else {
        int width, height;

        // TODO(Reiner): seems like this has to be done in a loop? Read GLFW docs
        // glfwGetFramebufferSize to retrieve the target extent.
        //        int width = 0, height = 0;
        //        glfwGetFramebufferSize(window, &width, &height);
        //        while (width == 0 || height == 0) {
        //            glfwGetFramebufferSize(window, &width, &height);
        //            glfwWaitEvents();
        //        }

        glfwGetFramebufferSize(m_window, &width, &height);

        m_swapchain.extent.width = std::clamp(static_cast<uint32_t>(width), surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
        m_swapchain.extent.height = std::clamp(static_cast<uint32_t>(height), surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
    }

    vk::SurfaceTransformFlagBitsKHR preTransform =
        (surfaceCapabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity) ? vk::SurfaceTransformFlagBitsKHR::eIdentity : surfaceCapabilities.currentTransform;

    vk::CompositeAlphaFlagBitsKHR compositeAlpha = (surfaceCapabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)    ? vk::CompositeAlphaFlagBitsKHR::ePreMultiplied
                                                   : (surfaceCapabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied) ? vk::CompositeAlphaFlagBitsKHR::ePostMultiplied
                                                   : (surfaceCapabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit)        ? vk::CompositeAlphaFlagBitsKHR::eInherit
                                                                                                                                                    : vk::CompositeAlphaFlagBitsKHR::eOpaque;

    vk::PresentModeKHR presentMode = vvv::chooseSwapPresentMode(getPhysicalDevice().getSurfacePresentModesKHR(surface), m_swapchain.vsync);

    const auto oldSwapchain = nullptr;

    vk::SwapchainCreateInfoKHR swapChainCreateInfo({}, surface, surfaceCapabilities.minImageCount, surfaceFormat.format, surfaceFormat.colorSpace, m_swapchain.extent, 1, swapImageUsage,
                                                   vk::SharingMode::eExclusive, {}, preTransform, compositeAlpha, presentMode, true, oldSwapchain);

    uint32_t queueFamilyIndices[2] = {getQueueFamilyIndices().present.value(), getQueueFamilyIndices().graphics.value()};

    if (getQueueFamilyIndices().present != getQueueFamilyIndices().graphics) {
        // If the graphics and present queues are from different queue families, we either have to explicitly
        // transfer ownership of images between the queues, or we have to create the swapchain with imageSharingMode
        // as vk::SharingMode::eConcurrent
        swapChainCreateInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        swapChainCreateInfo.queueFamilyIndexCount = 2;
        swapChainCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
    }

    // TODO(Reiner): rename to m_swap.chain, m_swap.images etc
    m_swapchain.swapchain = getDevice().createSwapchainKHR(swapChainCreateInfo);
    debugMarker->setName(m_swapchain.swapchain, "Application.m_swapchain.swapchain");
    m_swapchain.colorFormat = surfaceFormat.format;
    m_swapchain.images = getDevice().getSwapchainImagesKHR(m_swapchain.swapchain);

    const auto countSwapchainImages = m_swapchain.images.size();
    const auto maxInFlightFrames = MAX_FRAMES_IN_FLIGHT;

    setMultiBuffering(countSwapchainImages, maxInFlightFrames);

    m_swapchain.images.reserve(countSwapchainImages);

    vk::ComponentMapping componentMapping(vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA);
    vk::ImageSubresourceRange subResourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

    for (auto image : m_swapchain.images) {
        vk::ImageViewCreateInfo imageViewCreateInfo(vk::ImageViewCreateFlags(), image, vk::ImageViewType::e2D, surfaceFormat.format, componentMapping, subResourceRange);

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

        debugMarker->setName(m_swapchain.presentCompleteSemaphore[i], "Application.m_swapchain.presentCompleteSemaphore." + std::to_string(i));
        debugMarker->setName(m_swapchain.renderCompleteSemaphore[i], "Application.m_swapchain.renderCompleteSemaphore." + std::to_string(i));
        debugMarker->setName(m_swapchain.blitToSwapchainImageComplete[i], "Application.m_swapchain.blitToSwapchainImageComplete." + std::to_string(i));
    }

    // command pools, command buffers, fences and presents etc
    // TODO(Reiner): I think we need an additional queue family if we want to present with a different present queue index?
    vk::CommandPoolCreateInfo cmdPoolInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, getQueueFamilyIndices().graphics.value());
    m_swapchain.commandPool = getDevice().createCommandPool(cmdPoolInfo);
    debugMarker->setName(m_swapchain.commandPool, "Application.m_swapchain.commandPool");
    vk::CommandBufferAllocateInfo cmdBufferAllocInfo(m_swapchain.commandPool, vk::CommandBufferLevel::ePrimary, swapChainImageCount());
    m_swapchain.commandBuffers = getDevice().allocateCommandBuffers(cmdBufferAllocInfo);

    for (int i = 0; i < m_swapchain.commandBuffers.size(); ++i) {
        debugMarker->setName(m_swapchain.commandBuffers[i], "Application.m_swapchain.commandBuffer." + std::to_string(i));
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

        vk::FramebufferCreateInfo framebufferInfo({}, m_renderpass.renderpass, attachments, m_swapchain.extent.width, m_swapchain.extent.height, 1);

        m_renderpass.framebuffers[i] = getDevice().createFramebuffer(framebufferInfo);
        debugMarker->setName(m_renderpass.framebuffers[i], "Application.m_renderpass.framebuffers." + std::to_string(i));
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
    m_renderpass.descPool = device.createDescriptorPool(vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, maxInFlightFrames, poolSize));
    debugMarker->setName(m_renderpass.descPool, "Application.m_renderpass.descPool");

    const std::vector<vk::DescriptorSetLayout> descriptorSetLayouts(maxInFlightFrames, m_renderpass.descSetLayout);
    // TODO(Reiner): given that there is no API signature to allocate n-times the same descriptor set, we are probably doing something weird here.
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
    vk::WriteDescriptorSet writeDescriptorSet = vk::WriteDescriptorSet(m_renderpass.descSet[inFlightFrameIdx], 0, 0, vk::DescriptorType::eCombinedImageSampler, imageDescriptors);
    static_cast<vk::Device>(getDevice()).updateDescriptorSets({writeDescriptorSet}, {});
    m_renderpass.lastImageDescriptor[inFlightFrameIdx] = output.texture->descriptor;
}

void Application::createBlitShaders() {
    const auto shaderDirectory = vvv::getShaderIncludeDirectory();

    // TODO(Patrick): prevent rebuilding blit shader on window resize
    m_renderpass.shaderFragment = new vvv::Shader({.filename = "blit.frag", .label = "Application.m_shaderFragment"});
    m_renderpass.shaderVertex = new vvv::Shader({.filename = "blit.vert", .label = "Application.m_shaderVertex"});
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
    attachmentDescriptions.emplace_back(vk::AttachmentDescriptionFlags(), m_swapchain.colorFormat, vk::SampleCountFlagBits::e1, loadOp, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare,
                                        vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, colorFinalLayout);
    if (m_swapchain.depthFormat != vk::Format::eUndefined) {
        attachmentDescriptions.emplace_back(vk::AttachmentDescriptionFlags(), m_swapchain.depthFormat, vk::SampleCountFlagBits::e1, loadOp, vk::AttachmentStoreOp::eDontCare,
                                            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
    }
    vk::AttachmentReference colorAttachment(0, vk::ImageLayout::eColorAttachmentOptimal);
    vk::AttachmentReference depthAttachment(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);
    vk::SubpassDescription subpassDescription(vk::SubpassDescriptionFlags(), vk::PipelineBindPoint::eGraphics, {}, colorAttachment, {},
                                              (m_swapchain.depthFormat != vk::Format::eUndefined) ? &depthAttachment : nullptr);

    m_renderpass.renderpass = getDevice().createRenderPass(vk::RenderPassCreateInfo(vk::RenderPassCreateFlags(), attachmentDescriptions, subpassDescription));
}

void Application::destroyBlitRenderPass() { VK_DEVICE_DESTROY(getDevice(), m_renderpass.renderpass) }

void Application::createBlitPipeline() {
    m_renderpass.pipelineLayout = getDevice().createPipelineLayout(vk::PipelineLayoutCreateInfo({}, m_renderpass.descSetLayout));
    debugMarker->setName(m_renderpass.pipelineLayout, "Application.m_renderpass.pipelineLayout");

    std::array<vk::PipelineShaderStageCreateInfo, 2> pipelineShaderStageCreateInfos = {
        *m_renderpass.shaderVertex->pipelineShaderStageCreateInfo(this),
        *m_renderpass.shaderFragment->pipelineShaderStageCreateInfo(this),
    };

    vk::PipelineVertexInputStateCreateInfo emptyVertexInputState;

    vk::PipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo(vk::PipelineInputAssemblyStateCreateFlags(), vk::PrimitiveTopology::eTriangleList);

    vk::PipelineViewportStateCreateInfo pipelineViewportStateCreateInfo(vk::PipelineViewportStateCreateFlags(), 1, nullptr, 1, nullptr);

    vk::PipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo(vk::PipelineRasterizationStateCreateFlags(), false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone,
                                                                                  vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f);

    vk::PipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo({}, vk::SampleCountFlagBits::e1);

    const bool depthBuffered = false;

    vk::StencilOpState stencilOpState(vk::StencilOp::eKeep, vk::StencilOp::eKeep, vk::StencilOp::eKeep, vk::CompareOp::eAlways);

    vk::PipelineDepthStencilStateCreateInfo pipelineDepthStencilStateCreateInfo(vk::PipelineDepthStencilStateCreateFlags(), depthBuffered, depthBuffered, vk::CompareOp::eLessOrEqual, false, false,
                                                                                stencilOpState, stencilOpState);

    vk::ColorComponentFlags colorComponentFlags(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    vk::PipelineColorBlendAttachmentState pipelineColorBlendAttachmentState(false, vk::BlendFactor::eZero, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::BlendFactor::eZero, vk::BlendFactor::eZero,
                                                                            vk::BlendOp::eAdd, colorComponentFlags);

    vk::PipelineColorBlendStateCreateInfo pipelineColorBlendStateCreateInfo(vk::PipelineColorBlendStateCreateFlags(), false, vk::LogicOp::eNoOp, pipelineColorBlendAttachmentState,
                                                                            {{1.0f, 1.0f, 1.0f, 1.0f}});

    std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo(vk::PipelineDynamicStateCreateFlags(), dynamicStates);

    vk::GraphicsPipelineCreateInfo graphicsPipelineCreateInfo(vk::PipelineCreateFlags(), pipelineShaderStageCreateInfos, &emptyVertexInputState, &pipelineInputAssemblyStateCreateInfo, nullptr,
                                                              &pipelineViewportStateCreateInfo, &pipelineRasterizationStateCreateInfo, &pipelineMultisampleStateCreateInfo,
                                                              &pipelineDepthStencilStateCreateInfo, &pipelineColorBlendStateCreateInfo, &pipelineDynamicStateCreateInfo, m_renderpass.pipelineLayout,
                                                              m_renderpass.renderpass);

    auto result = getDevice().createGraphicsPipeline(m_renderpass.pipelineCache, graphicsPipelineCreateInfo);
    assert(result.result == vk::Result::eSuccess);
    m_renderpass.pipeline = result.value;
    debugMarker->setName(m_renderpass.pipeline, "Application.m_renderpass.pipeline");
}

void Application::destroyBlitPipeline(){VK_DEVICE_DESTROY(getDevice(), m_renderpass.pipeline) VK_DEVICE_DESTROY(getDevice(), m_renderpass.pipelineLayout)}

vk::Extent2D Application::getScreenExtent() const {
    return m_swapchain.extent;
}

void Application::processHotKeys() {
    // shader reload
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        vvv::Logger(vvv::INFO) << "reloading shaders";
        recreateShaderResources();
        writePipelineCacheToDisk(getDevice());
    }

    // record camera path and time stamps
    if (!m_record_in.has_value() && !m_video_frame.has_value() && ImGui::IsKeyPressed(ImGuiKey_F9)) {
        // stop recording of camera path
        if(m_record_out.has_value()) {
            m_record_out->close();
            m_record_out = {};
            if(m_video_timing.has_value()) {
                m_video_timing->close();
                m_video_timing = {};
                vvv::Logger(vvv::INFO) << "compute video file from frames in " << m_video_file_path << " with:";
                vvv::Logger(vvv::INFO) << " ffmpeg -f concat -safe 0 -i video_timing.txt video.mp4";
            }

            // output timing of path
            avg_ms /= static_cast<double>(avg_ms_samples);
            var_ms /= static_cast<double>(avg_ms_samples);
            vvv::Logger(vvv::INFO) << "min / avg (std.dev.) / max [ms/frame]";
            vvv::Logger(vvv::INFO) << std::fixed << std::setprecision(0) << min_ms << " / " << avg_ms  << " (" << std::sqrt(var_ms - (avg_ms * avg_ms)) << ") " << " / " << max_ms << " total avg ms " << avg_ms;
        }
        else {
            m_record_out = std::ofstream(m_record_file_path, std::ios::out | std::ios::binary);
            if(!m_record_out->is_open()) {
                vvv::Logger(vvv::WARN) << "could not open recording output file " << m_record_file_path;
                m_record_out = {};
                return;
            }

            // create an output file for our timings
            m_video_timing = std::ofstream(m_video_file_path + "_timing.txt", std::ios::out);
            if(!m_video_timing->is_open()) {
                vvv::Logger(vvv::WARN) << "could not open video timing file " << m_video_file_path << "_timing.txt";
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
    else if (!m_record_out.has_value() && !m_video_timing.has_value() && !m_video_frame.has_value() && (ImGui::IsKeyPressed(ImGuiKey_F10) || ImGui::IsKeyPressed(ImGuiKey_F11))) {
        // stop replay
        if(m_record_in.has_value()) {
            m_record_in->close();
            m_record_in = {};

            // output timing of path
            avg_ms /= static_cast<double>(avg_ms_samples);
            var_ms /= static_cast<double>(avg_ms_samples);
            vvv::Logger(vvv::WARN) << std::fixed << std::setprecision(0) << min_ms << " / " << avg_ms  << " ($\\sigma=" << std::sqrt(var_ms - (avg_ms * avg_ms)) << "$) " << " / " << max_ms << " total avg ms " << avg_ms;
        }
        // start replay
        else {
            m_record_in = std::ifstream(m_record_file_path, std::ios::in | std::ios::binary);
            if(!m_record_in->is_open()) {
                vvv::Logger(vvv::WARN) << "could not open recording input file " << m_record_file_path;
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
    else if(!m_record_out.has_value() && !m_record_in.has_value() && !m_video_frame.has_value() && !m_video_timing.has_value() && ImGui::IsKeyPressed(ImGuiKey_F12)) {
        // open the camera path file
        m_record_in = std::ifstream(m_record_file_path, std::ios::in | std::ios::binary);
        if(!m_record_in->is_open()) {
            vvv::Logger(vvv::WARN) << "could not open recording input file " << m_record_file_path;
            m_record_in = {};
            return;
        }
        m_video_frame = 0;
    }
    else if(ImGui::IsKeyPressed(ImGuiKey_F1)) {
        m_display_imgui = false;
    }
    else if(ImGui::IsKeyPressed(ImGuiKey_F2)) {
        m_display_imgui = true;
    }
}

void Application::recreateShaderResources() {
    if(!getDevice()) {
        return;
    }

    getDevice().waitIdle();

    m_renderer->releaseSwapchain();
    m_renderer->releaseShaderResources();

    m_renderer->initShaderResources();
    m_renderer->initSwapchainResources();
}

void Application::recreateInnerRenderingEngine() {

    if(!getDevice()) {
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
    if(m_record_out.has_value()) {
        getCamera()->writeTo(m_record_out.value());
    }
    // read
    else if(m_record_in.has_value()) {
        getCamera()->readFrom(m_record_in.value());
        if(m_record_in->eof()) {
            m_record_in->close();
            m_record_in = {};

            // output timing of path
            avg_ms /= static_cast<double>(avg_ms_samples);
            var_ms /= static_cast<double>(avg_ms_samples);
            vvv::Logger(vvv::WARN) << std::fixed << std::setprecision(0) << min_ms << " / " << avg_ms  << " ($\\sigma=" << std::sqrt(var_ms - (avg_ms * avg_ms)) << "$) " << " / " << max_ms;
        }
    }
}

void Application::processVideoRecording() {
    // write time stamps
    if(m_video_timing.has_value()) {
        *m_video_timing << "file '" << m_video_file_path << "_" << m_video_frame_count << ".png'" << std::endl;
        double new_time = glfwGetTime();
        *m_video_timing << "duration " << (new_time - m_video_last_timestamp) << std::endl;
        m_video_frame_count++;
        m_video_last_timestamp = new_time;
    }
}

void Application::glfwUpdateScrollWheel(GLFWwindow *window, double xoffset, double yoffset) {
    s_mouse_scroll_wheel += yoffset;
}

void Application::updateCamera() {
    // read scroll wheel value
    float scrollWheelDelta = s_mouse_scroll_wheel - m_mouse_scroll_wheel_previous_frame;
    m_mouse_scroll_wheel_previous_frame= s_mouse_scroll_wheel;
    // do not process mouse input if the pointer is in a GUI window
    if (ImGui::GetIO().WantCaptureMouse)
        scrollWheelDelta = 0.f;

    // do not process keyboard input if ImGui obtains text input
    bool captureKeyboard = !ImGui::GetIO().WantCaptureKeyboard;

    auto camera = getCamera();

    // Figure out how much time has passed since the last invocation
    static double last_time = 0.0;
    double now = glfwGetTime();
    double elapsed_time = (last_time == 0.0) ? 0.0 : (now - last_time);
    float time_delta = (float)elapsed_time;
    last_time = now;

    static const float mouse_radians_per_pixel = 1.0f * std::numbers::pi / 1000.0f;

    int left_mouse_state = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_1);
    int right_mouse_state = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_2);
    // do not process mouse input if the pointer is in a GUI window
    if (ImGui::GetIO().WantCaptureMouse) {
        left_mouse_state = GLFW_RELEASE;
        right_mouse_state = GLFW_RELEASE;
    }
    double mouse_position_double[2];
    glfwGetCursorPos(m_window, &mouse_position_double[0], &mouse_position_double[1]);
    float mouse_position[2] = {(float)mouse_position_double[0], (float)mouse_position_double[1]};
    if (!camera->rotate_camera && (right_mouse_state == GLFW_PRESS || left_mouse_state == GLFW_PRESS)) {
        camera->rotate_camera = true;
        camera->rotation_x_0 = camera->rotation_x - mouse_position[1] * mouse_radians_per_pixel;
        camera->rotation_y_0 = camera->rotation_y - mouse_position[0] * mouse_radians_per_pixel;
    }
    // in orbital mode, shift and control can lock rotation axes
    if (camera->orbital && camera->rotate_camera && glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        camera->rotation_x_0 = camera->rotation_x - mouse_position[1] * mouse_radians_per_pixel;
    }
    if (camera->orbital && camera->rotate_camera && glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
        camera->rotation_y_0 = camera->rotation_y - mouse_position[0] * mouse_radians_per_pixel;
    }

    if ((left_mouse_state == GLFW_RELEASE && right_mouse_state != GLFW_PRESS) || (right_mouse_state == GLFW_RELEASE && left_mouse_state != GLFW_PRESS))
        camera->rotate_camera = false;
    if (camera->rotate_camera) {
        camera->rotation_x = camera->rotation_x_0 + mouse_radians_per_pixel * mouse_position[1];
        camera->rotation_y = camera->rotation_y_0 + mouse_radians_per_pixel * mouse_position[0];
        camera->rotation_x = (camera->rotation_x < -std::numbers::pi) ? -std::numbers::pi : camera->rotation_x;
        camera->rotation_x = (camera->rotation_x > std::numbers::pi) ? std::numbers::pi : camera->rotation_x;
    }
    if(camera->orbital) {
        if (captureKeyboard && !camera->rotate_camera && glfwGetKey(m_window, GLFW_KEY_R)) {
            camera->rotation_y += 0.01f;
        }
        constexpr float pi_eps = std::numbers::pi / 2.f - 0.001f;
        camera->rotation_x = glm::clamp(camera->rotation_x, -pi_eps, pi_eps);

        float final_speed = camera->speed * 0.5f;
        final_speed *= (glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 2.0f : 1.0f;
        final_speed *= (glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) ? 0.1f : 1.0f;
        float step = time_delta * final_speed;
        // Determine camera movement
        float forward = 0.0f;
        if (captureKeyboard) {
            forward += (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) ? step : 0.0f;
            forward -= (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) ? step : 0.0f;
        }
        camera->orbital_radius -= (forward + scrollWheelDelta/10.f) * final_speed * camera->orbital_radius;
        camera->orbital_radius = glm::max(0.001f, camera->orbital_radius);
        camera->position_world_space = glm::vec3(camera->orbital_radius * cos(camera->rotation_y) * cos(camera->rotation_x),
                                                   camera->orbital_radius * sin(camera->rotation_x),
                                                   camera->orbital_radius * sin(camera->rotation_y) * cos(camera->rotation_x));

        if (scrollWheelDelta != 0.f || camera->rotate_camera) {
            camera->onCameraUpdate();
        }
    }
    else {
        // Modify the speed
        float final_speed = camera->speed * 0.5f;
        final_speed *= (glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 2.0f : 1.0f;
        final_speed *= (glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) ? 0.1f : 1.0f;
        float step = time_delta * final_speed;
        // Determine camera movement
        float forward = 0.0f, right = 0.0f, vertical = 0.0f;
        if (captureKeyboard) {
            forward += (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) ? step : 0.0f;
            forward -= (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) ? step : 0.0f;
            right += (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS) ? step : 0.0f;
            right -= (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS) ? step : 0.0f;
            vertical += (glfwGetKey(m_window, GLFW_KEY_E) == GLFW_PRESS) ? step : 0.0f;
            vertical -= (glfwGetKey(m_window, GLFW_KEY_Q) == GLFW_PRESS) ? step : 0.0f;
        }
        // Implement camera movement
        float cos_y = cosf(camera->rotation_y), sin_y = sinf(camera->rotation_y);
        camera->position_world_space[0] +=  sin_y * forward;
        camera->position_world_space[0] +=  cos_y * right;
        camera->position_world_space[2] += -cos_y * forward;
        camera->position_world_space[2] +=  sin_y * right;
        camera->position_world_space[1] +=  vertical;

        if (forward != 0.0f || right != 0.0f || vertical != 0.0f || camera->rotate_camera) {
            camera->onCameraUpdate();
        }
    }


}

#ifdef IMGUI
void Application::initImGui() {
    auto device = getDevice();

    // create vulkan objects for ImGui (only the descriptor pool so far)
    // descriptor pool
    vk::DescriptorPoolSize pool_sizes[] =
        {
            { vk::DescriptorType::eSampler, 1000 },
            { vk::DescriptorType::eCombinedImageSampler, 1000 },
            { vk::DescriptorType::eSampledImage, 1000 },
            { vk::DescriptorType::eStorageImage, 1000 },
            { vk::DescriptorType::eUniformTexelBuffer, 1000 },
            { vk::DescriptorType::eStorageTexelBuffer, 1000 },
            { vk::DescriptorType::eUniformBuffer, 1000 },
            { vk::DescriptorType::eStorageBuffer, 1000 },
            { vk::DescriptorType::eUniformBufferDynamic, 1000 },
            { vk::DescriptorType::eStorageBufferDynamic, 1000 },
            { vk::DescriptorType::eInputAttachment, 1000 }
        };
    vk::DescriptorPoolCreateInfo pool_info = {};
    pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    pool_info.maxSets = 1000;// * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    m_imgui.descPool = device.createDescriptorPool(pool_info);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    recreateSwapchainImGui();

    ImGui::StyleColorsLight();

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
    ImGui_ImplVulkan_Init(&init_info, m_renderpass.renderpass);

    m_imgui.initialized = true;
}

void Application::shutdownImGui() {
    if(!m_imgui.initialized)
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
    m_imgui.minImageCount = surfaceCapabilities.minImageCount;
    if(m_imgui.initialized)
        ImGui_ImplVulkan_SetMinImageCount(m_imgui.minImageCount);
}
#endif

void Application::logLibraryAvailabilty() {
    vvv::logLibraryAvailabilty();
#ifdef IMGUI
    vvv::Logger(vvv::DEBUG) << "ImGUI " + std::string(ImGui::GetVersion()) << + " available.";
#endif
}
float Application::getScreenContentScale() const {
    float contentScaleX, contentScaleY;
    glfwGetMonitorContentScale(glfwGetPrimaryMonitor(), &contentScaleX, &contentScaleY);
    return std::max(contentScaleX, contentScaleY);
}

void Application::setWindowSize(int width, int height) const {
    if(m_window)
        glfwSetWindowSize(m_window, width, height);
}

void Application::setWindowResizable(bool resizable) const {
    if(m_window)
        glfwSetWindowAttrib(m_window, GLFW_RESIZABLE, resizable);
}

bool Application::isWindowResizable() const {
    if(m_window)
        return static_cast<bool>(glfwGetWindowAttrib(m_window, GLFW_RESIZABLE));
    return false;
}
