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

#include "vvv/core/HeadlessRendering.hpp"

#include <vvv/vk/destroy.hpp>
#include <vvv/util/Logger.hpp>
#include <vvv/util/util.hpp>

namespace vvv {

static void check_vk_result(VkResult err) {
    if (err != 0) {
        std::cerr << "Vulkan error " << vk::to_string(static_cast<vk::Result>(err));
        if (err < 0) {
            abort();
        }
    }
}

static void check_vk_result(vk::Result err) { check_vk_result(static_cast<VkResult>(err)); }

void HeadlessRendering::recreateSwapchain() {
    // TODO(Reiner): use new API, otherwise not well defined
    getDevice().waitIdle();

    // Note: this is conservative: destroy the swapchain and everything that might depend on it
    // (Speak: Run the destructor up to the swapchain deletion)
    m_renderer->releaseSwapchain();
    m_renderer->initSwapchainResources();

    m_pendingRecreation = false;
}

RendererOutput HeadlessRendering::renderFrame(AwaitableList awaitBeforeExecution) {
    if(m_pendingRecreation)
        recreateSwapchain();
    return m_renderer->renderNextFrame(awaitBeforeExecution, {});
}

//std::thread HeadlessRendering::execAsyncAttached() {
//    std::thread renderThread(&HeadlessRendering::exec, this);
//    return renderThread;
//}
//
//void HeadlessRendering::execAsync() { execAsyncAttached().detach(); }

std::shared_ptr<Texture> HeadlessRendering::renderFrames(size_t number_of_frames, void (*frameFinishedCallback)(vvv::Texture *)) {
    if(!isGpuContextCreated()) {
        Logger(ERROR) << "GPU context not available. You must call acquireResources() before rendering.";
        return nullptr;
    }

    // ToDo: decouple HeadlessRendering::exec in an initialization method and multiple render calls, respect m_pendingRecreation
    // e.g.: hr.init(); hr.setRenderResolution(400, 400); hr.renderToFile(120); hr.setRenderParametersFromFile(path); auto output = hr.render(60);

    RendererOutput rendererOutput = {nullptr, {}};
    MiniTimer timer;
    for (size_t frame_idx = 0; frame_idx < number_of_frames; frame_idx++) {
        // render one frame after the other = wait for the last renderingComplete to finish
        // ToDo: should we use MultiBuffering in headless mode as well?
        rendererOutput = renderFrame({rendererOutput.renderingComplete});

        if(frameFinishedCallback) {
            frameFinishedCallback(rendererOutput.texture);
        }
    }
    double endTime = timer.elapsed();

    if (getDevice()) {
        getDevice().waitIdle();
    }

    double frame_time = endTime / static_cast<double>(number_of_frames);
    Logger(INFO) << "rendering of " << number_of_frames << " frames finished with " << 1. / frame_time << " fps (" << 1000.f * frame_time << "ms/frame)";

    // copy the last output texture to a new texture that we can return.
    // this way the original rendering texture could be overwritten or destroyed without affecting the return texture.
    auto ret_tex = std::make_shared<Texture>(this, rendererOutput.texture->format, rendererOutput.texture->width, rendererOutput.texture->height,
                                             vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage);
    ret_tex->ensureResources();
    const auto layoutTransformDone = ret_tex->setImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits::eAllCommands);
    rendererOutput.renderingComplete.push_back(layoutTransformDone);
    sync->hostWaitOnDevice(rendererOutput.renderingComplete);
    sync->hostWaitOnDevice({this->executeCommands([rendererOutput, ret_tex](vk::CommandBuffer cmd){
        auto width = rendererOutput.texture->width;
        auto height = rendererOutput.texture->height;
        const auto originalLayout = rendererOutput.texture->descriptor.imageLayout;
        rendererOutput.texture->setImageLayout(cmd, vk::ImageLayout::eTransferSrcOptimal);
        vk::ImageCopy copyRegion(vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), vk::Offset3D(0, 0, 0),
                                 vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), vk::Offset3D(0, 0, 0), vk::Extent3D(width, height, 1));
        cmd.copyImage(rendererOutput.texture->image, vk::ImageLayout::eTransferSrcOptimal, ret_tex->image, vk::ImageLayout::eTransferDstOptimal, {copyRegion});
        rendererOutput.texture->setImageLayout(cmd, originalLayout);
    })});
    return ret_tex;
}

void HeadlessRendering::acquireResources() {
    m_renderer->configureExtensionsAndLayersAndFeatures(this);

    createGpuContext();
    createQueues();

    m_renderer->initResources(this);
    m_renderer->initShaderResources();
    m_renderer->initSwapchainResources();

    m_renderer->initGui(this->getGui());
}

void HeadlessRendering::createQueues() {
    m_queues.graphics = getDevice().getQueue(getQueueFamilyIndices().graphics.value(), 0);
    debugMarker->setName(m_queues.graphics, "Application.m_queues.graphics");
    m_queues.present = nullptr;     // we do not need a present queue in headless rendering
}

void HeadlessRendering::destroyQueues() {
    m_queues.present = nullptr;
    m_queues.graphics = nullptr;
}

void HeadlessRendering::releaseResources() {
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

    destroyQueues();
    destroyGpuContext();
}

void HeadlessRendering::recreateShaderResources() {
    if (!getDevice()) {
        return;
    }

    getDevice().waitIdle();

    m_renderer->releaseSwapchain();
    m_renderer->releaseShaderResources();

    m_renderer->initShaderResources();
    m_renderer->initSwapchainResources();
}

void HeadlessRendering::recreateInnerRenderingEngine() {
    if (!getDevice()) {
        return;
    }

    getDevice().waitIdle();

    m_renderer->releaseGui();
    m_renderer->releaseSwapchain();
    m_renderer->releaseShaderResources();
    m_renderer->releaseResources();

    m_renderer->initResources(this);
    m_renderer->initShaderResources();
    m_renderer->initSwapchainResources();
}

} // namespace vvv
