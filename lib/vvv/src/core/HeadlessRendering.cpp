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

#include <vvv/util/Logger.hpp>
#include <vvv/util/util.hpp>

#include "glm/ext/scalar_constants.hpp"
#include <fmt/core.h>

namespace vvv {

static void check_vk_result(VkResult err) {
    if (err != 0) {
        std::cerr << "Vulkan error " << vk::to_string(static_cast<vk::Result>(err));
        if (err < 0) {
            abort();
        }
    }
}

[[maybe_unused]]
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
    if (m_pendingRecreation)
        recreateSwapchain();
    return m_renderer->renderNextFrame(awaitBeforeExecution, {});
}

// std::thread HeadlessRendering::execAsyncAttached() {
//     std::thread renderThread(&HeadlessRendering::exec, this);
//     return renderThread;
// }
//
// void HeadlessRendering::execAsync() { execAsyncAttached().detach(); }

std::shared_ptr<Texture> HeadlessRendering::renderFrames(const HeadlessRenderingConfig &cfg) {
    if (!isGpuContextCreated())
        throw std::runtime_error("GPU context not available. You must call acquireResources() before rendering.");
    if (cfg.accumulation_samples == 0)
        throw std::runtime_error("Accumulation frames must be greater then zero.");
    // if (cfg.camera_auto_rotate_frames > 0 && !cfg.record_file_in.empty())
    //     throw std::runtime_error("Cannot specify both camera_auto_rotate_frames and record_file_in.");

    // TODO: decouple HeadlessRendering::exec in an initialization method and multiple render calls, respect m_pendingRecreation
    // e.g.: hr.init(); hr.setRenderResolution(400, 400); hr.renderToFile(120); hr.setRenderParametersFromFile(path); auto output = hr.render(60);

    constexpr int MAX_CAMERA_AUTO_FRAMES = 256; // 1800 frames would be 1 min at 30 fps
    int camera_auto_rotate_frames;
    // pre-recorded camera path playback
    std::optional<std::ifstream> m_record_in = {};
    if (!cfg.record_file_in.empty()) {
        m_record_in = std::ifstream(cfg.record_file_in, std::ios::in | std::ios::binary);
        if (!m_record_in->is_open()) {
            throw std::runtime_error("could not open recording input file " + cfg.record_file_in);
        }
        camera_auto_rotate_frames = 0; // exit when the camera file reached its end
    }
    // rendering video images but no camera playback is specified: rotate camera around
    else if (!cfg.video_fmt_file_out.empty()) {
        camera_auto_rotate_frames = MAX_CAMERA_AUTO_FRAMES;
    } else {
        camera_auto_rotate_frames = 1;
    }

    Logger(Info) << "rendering " << (camera_auto_rotate_frames == 0u ? (" camera poses from " + cfg.record_file_in) : (std::to_string(camera_auto_rotate_frames) + " camera pose(s)"))
                 << " with " + std::to_string(cfg.accumulation_samples) << " frame(s) each";

    // interpolation start and end values (rotation around Y axis and zoom)
    float roty_0, roty_1;
    float rad_0, rad_1;
    {
        auto camera = getCamera();
        roty_0 = roty_1 = camera->rotation_y;
        rad_0 = rad_1 = camera->orbital_radius;
        if (camera_auto_rotate_frames > 0) {
            roty_1 = roty_0 + (2.f * glm::pi<float>());
            rad_1 = 1.f;
        }
    }

    RendererOutput rendererOutput = {nullptr, {}};
    size_t frame_idx = 0u;
    MiniTimer timer;
    // either render all camera poses from the record_in file or render camera_auto_rotate_frames images with
    // accumulation_samples each. he camera is rotated around the Y axis after each camera_auto_rotate_frame
    for (frame_idx = 0u; (m_record_in.has_value() && !m_record_in->eof()) || frame_idx < camera_auto_rotate_frames; frame_idx++) {
        if (m_record_in.has_value()) {
            getCamera()->readFrom(m_record_in.value());
            if (m_record_in->eof()) {
                m_record_in->close();
                m_record_in = {};
                break;
            }
            if (m_record_in->fail()) {
                throw std::runtime_error("Error reading camera pose from " + cfg.record_file_in);
            }
        } else if (camera_auto_rotate_frames > 0) {
            auto camera = getCamera();

            // constexpr float smootherstep(float s_min, float s_max, float x) {
            //     x = glm::clamp((x - s_min) / (s_max - s_min), 0.f, 1.f);
            //     return x * x * x * (x * (6.f * x - 15.f) + 10.f);
            // }

            // TODO: camera auto rotation in headless rendering could become a (configurable) camera controller
            const float v = glm::smoothstep(0.01f, 0.99f, static_cast<float>(frame_idx) / static_cast<float>(camera_auto_rotate_frames));
            camera->rotation_y = glm::mix(roty_0, roty_1, v);
            camera->orbital_radius = glm::mix(rad_0, rad_1, v);
            camera->position_world_space = camera->position_look_at_world_space + glm::vec3(
                                                                                      camera->orbital_radius * glm::cos(camera->rotation_y) * glm::cos(camera->rotation_x),
                                                                                      camera->orbital_radius * glm::sin(camera->rotation_x),
                                                                                      camera->orbital_radius * glm::sin(camera->rotation_y) * glm::cos(camera->rotation_x));

            camera->onCameraUpdate();
        }

        // render one frame after the other = wait for the last renderingComplete to finish
        for (size_t accumulation_idx = 0; accumulation_idx < cfg.accumulation_samples; accumulation_idx++) {
            rendererOutput = renderFrame({rendererOutput.renderingComplete});
        }

        if (!cfg.video_fmt_file_out.empty()) {
            m_renderer->exportCurrentFrameToImage(fmt::vformat(cfg.video_fmt_file_out, fmt::make_format_args(frame_idx)));
        }

        if (cfg.frameFinishedCallback) {
            cfg.frameFinishedCallback(&rendererOutput);
        }
    }

    m_renderer->stopFrameTimeTracking(rendererOutput.renderingComplete);
    const double endTime = timer.elapsed();
    const double frame_time = endTime / static_cast<double>((frame_idx * cfg.accumulation_samples));

    // copy the last output texture to a new texture that we can return.
    // this way the original rendering texture could be overwritten or destroyed without affecting the return texture.
    auto ret_tex = std::make_shared<Texture>(this, rendererOutput.texture->format, rendererOutput.texture->width, rendererOutput.texture->height,
                                             vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage, std::set<uint32_t>{rendererOutput.queueFamilyIndex});
    ret_tex->ensureResources();
    const auto layoutTransformDone = ret_tex->setImageLayout(vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits::eAllCommands, {.queueFamily = rendererOutput.queueFamilyIndex});
    rendererOutput.renderingComplete.push_back(layoutTransformDone);
    sync->hostWaitOnDevice({this->executeCommands([rendererOutput, ret_tex](const vk::CommandBuffer cmd) {
        auto width = rendererOutput.texture->width;
        auto height = rendererOutput.texture->height;
        const auto originalLayout = rendererOutput.texture->descriptor.imageLayout;
        rendererOutput.texture->setImageLayout(cmd, vk::ImageLayout::eTransferSrcOptimal);
        vk::ImageCopy copyRegion(vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), vk::Offset3D(0, 0, 0),
                                 vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), vk::Offset3D(0, 0, 0), vk::Extent3D(width, height, 1));
        cmd.copyImage(rendererOutput.texture->image, vk::ImageLayout::eTransferSrcOptimal, ret_tex->image, vk::ImageLayout::eTransferDstOptimal, {copyRegion});
        rendererOutput.texture->setImageLayout(cmd, originalLayout);
    },
                                                  {.queueFamily = rendererOutput.queueFamilyIndex, .await = rendererOutput.renderingComplete})});

    // export the final frame to the video path
    if (!cfg.video_fmt_file_out.empty()) {
        frame_idx--; // frame_idx is now the number of frames, but the last index is one before
        std::string last_output_image_path = fmt::vformat(cfg.video_fmt_file_out, fmt::make_format_args(frame_idx));
        Logger(Info) << "exporting screenshot to " << last_output_image_path;
        ret_tex->writeFile(last_output_image_path);
    }

    Logger(Info) << "rendering of " << (frame_idx * cfg.accumulation_samples)
                 << " frames finished with " << 1. / frame_time << " fps (" << 1000.f * frame_time << "ms/frame)";
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
    m_queues.present = nullptr; // we do not need a present queue in headless rendering
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
