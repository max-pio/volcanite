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

std::shared_ptr<Texture> HeadlessRendering::renderFrames(size_t number_of_frames,
                                                         std::string record_file_in,
                                                         std::string video_fmt_file_out,
                                                         void (*frameFinishedCallback)(RendererOutput *)) {
    if(!isGpuContextCreated()) {
        Logger(ERROR) << "GPU context not available. You must call acquireResources() before rendering.";
        return nullptr;
    }

    // TODO: decouple HeadlessRendering::exec in an initialization method and multiple render calls, respect m_pendingRecreation
    // e.g.: hr.init(); hr.setRenderResolution(400, 400); hr.renderToFile(120); hr.setRenderParametersFromFile(path); auto output = hr.render(60);

    // pre-recorded camera path playback
    std::optional<std::ifstream> m_record_in = {};
    if (!record_file_in.empty()) {
        m_record_in = std::ifstream(record_file_in, std::ios::in | std::ios::binary);
        if (!m_record_in->is_open()) {
            throw std::runtime_error("could not open recording input file " + record_file_in);
        }
    }

    RendererOutput rendererOutput = {nullptr, {}};
    size_t frame_idx;
    MiniTimer timer;
    for (frame_idx = 0u; frame_idx < number_of_frames || (number_of_frames == 0 && m_record_in.has_value()); frame_idx++) {

        if (m_record_in.has_value()) {
            getCamera()->readFrom(m_record_in.value());
            if (m_record_in->eof()) {
                m_record_in->close();
                m_record_in = {};
                // stop the rendering if not target frame count was given once the recording file finished
                if (number_of_frames == 0u)
                    break;
            }
        }

        // render one frame after the other = wait for the last renderingComplete to finish

        rendererOutput = renderFrame({rendererOutput.renderingComplete});
        if (!video_fmt_file_out.empty()) {
            m_renderer->exportCurrentFrameToImage(std::vformat(video_fmt_file_out,
                                                               std::make_format_args(frame_idx)));
        }

        if(frameFinishedCallback) {
            frameFinishedCallback(&rendererOutput);
        }
    }
//    sync->hostWaitOnDevice(rendererOutput.renderingComplete);
    m_renderer->stopFrameTimeTracking(rendererOutput.renderingComplete);
    double endTime = timer.elapsed();

    double frame_time = endTime / static_cast<double>(frame_idx);
    Logger(INFO) << "rendering of " << frame_idx << " frames finished with " << 1. / frame_time << " fps (" << 1000.f * frame_time << "ms/frame)";

    if (getDevice()) {
        getDevice().waitIdle();
    }

    // copy the last output texture to a new texture that we can return.
    // this way the original rendering texture could be overwritten or destroyed without affecting the return texture.
    auto ret_tex = std::make_shared<Texture>(this, rendererOutput.texture->format, rendererOutput.texture->width, rendererOutput.texture->height,
                                             vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage, std::set<uint32_t>{rendererOutput.queueFamilyIndex});
    ret_tex->ensureResources();
    const auto layoutTransformDone = ret_tex->setImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits::eAllCommands, {.queueFamily=rendererOutput.queueFamilyIndex});
    rendererOutput.renderingComplete.push_back(layoutTransformDone);
    sync->hostWaitOnDevice({this->executeCommands([rendererOutput, ret_tex](vk::CommandBuffer cmd){
        auto width = rendererOutput.texture->width;
        auto height = rendererOutput.texture->height;
        const auto originalLayout = rendererOutput.texture->descriptor.imageLayout;
        rendererOutput.texture->setImageLayout(cmd, vk::ImageLayout::eTransferSrcOptimal);
        vk::ImageCopy copyRegion(vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), vk::Offset3D(0, 0, 0),
                                 vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), vk::Offset3D(0, 0, 0), vk::Extent3D(width, height, 1));
        cmd.copyImage(rendererOutput.texture->image, vk::ImageLayout::eTransferSrcOptimal, ret_tex->image, vk::ImageLayout::eTransferDstOptimal, {copyRegion});
        rendererOutput.texture->setImageLayout(cmd, originalLayout);
    }, {.queueFamily=rendererOutput.queueFamilyIndex})});

    // export the final frame to the video path
    if (!video_fmt_file_out.empty()) {
        frame_idx--; // frame_idx is now the number of frames, but the last index is one before
        ret_tex->writeFile(std::vformat(video_fmt_file_out,
                                        std::make_format_args(frame_idx)));
    }
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
    debugMarker->setName(m_queues.graphics, "HeadlessRendering.m_queues.graphics");
    m_queues.compute = getDevice().getQueue(getQueueFamilyIndices().compute.value(), 0);
    debugMarker->setName(m_queues.compute, "HeadlessRendering.m_queues.compute");
    m_queues.present = nullptr;     // we do not need a present queue in headless rendering
}

void HeadlessRendering::destroyQueues() {
    m_queues.present = nullptr;
    m_queues.graphics = nullptr;
}

void HeadlessRendering::releaseResources() {
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
