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

    m_renderer->releaseSwapchain();
    m_renderer->initSwapchainResources();

    m_pendingRecreation = false;
}

RendererOutput HeadlessRendering::renderFrame(AwaitableList awaitBeforeExecution) {
    // the "swapchain" does not exist in headless.
    // But the renderer has to create frame buffer size dependent resources.
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
    // if (cfg.video_frames > 0 && !cfg.record_file_in.empty())
    //     throw std::runtime_error("Cannot specify both video_frames and record_file_in.");

    // TODO: decouple HeadlessRendering::exec in an initialization method and multiple render calls, respect m_pendingRecreation
    // e.g.: hr.init(); hr.setRenderResolution(400, 400); hr.renderToFile(120); hr.setRenderParametersFromFile(path); auto output = hr.render(60);

    // TODO: add behaviour for frame_time_file_out export if it is given. Obtain frame time from renderer, write to array. export frame times + video output file names to *_timings.txt in the end.

    // pre-recorded camera path playback
    std::optional<std::ifstream> record_in = {};
    if (!cfg.record_file_in.empty()) {
        record_in = std::ifstream(cfg.record_file_in, std::ios::in | std::ios::binary);
        if (!record_in->is_open()) {
            throw std::runtime_error("could not open recording input file " + cfg.record_file_in);
        }
    }

    if (!cfg.record_file_in.empty())
        Logger(Info) << "rendering camera poses from " + cfg.record_file_in << " with " + std::to_string(cfg.accumulation_samples) << " render sample(s) each";
    else if (cfg.duration > 0)
        Logger(Info) << "rendering " << cfg.duration << " frame(s) animation with " + std::to_string(cfg.accumulation_samples) << " render sample(s) each";
    else if (cfg.duration < 0)
        Logger(Info) << "rendering " << -cfg.duration << " second(s) animation with " + std::to_string(cfg.accumulation_samples) << " render sample(s) each";

    // TODO: replace headless camera animation with real parameter animation class that operates with the GUIInterface
    // interpolation start and end values (rotation around Y axis and zoom)
    struct VideoKeyFrames {
        float roty_0 = 0.f; ///< camera rotation start
        float roty_1 = 0.f; ///< camera rotation end
        float dist_0 = 0.f; ///< camera distance start
        float dist_1 = 0.f; ///< camera distance end
    } anim;
    {
        auto camera = getCamera();
        anim.roty_0 = anim.roty_1 = camera->rotation_y;
        anim.dist_0 = anim.dist_1 = camera->orbital_radius;
        if (!record_in.has_value()) {
            anim.roty_0 = camera->rotation_y + (cfg.cam_rot_start * glm::pi<float>() / 180.f);
            anim.roty_1 = camera->rotation_y + (cfg.cam_rot_end * glm::pi<float>() / 180.f);
            anim.dist_0 = glm::max(0.001f, camera->orbital_radius + cfg.cam_zoom_start);
            anim.dist_1 = glm::max(0.001f, camera->orbital_radius + cfg.cam_zoom_end);
        }
    }

    if (cfg.duration < 0 && cfg.accumulation_samples > 1) {
        Logger(Warn) << "Rendering real-time video output must not use cfg.accumulation_samples != 1";
    }

    RendererOutput rendererOutput = {nullptr, {}};
    size_t frame_idx = 0u;
    MiniTimer timer;
    double elapsed_s = 0.;
    // if record_in is given (pre-recorded): render all camera poses from the record_in file
    // else, render a camera path animation:
    // if video_frames > 0 (target frame count): render video_frames images
    // if video_frames < 0 (target real-time duration): render until abs(video_frames) seconds passed
    //
    // Each frame is rendered with accumulation_samples each.
    for (frame_idx = 0u; (record_in.has_value() && !record_in->eof()) || (cfg.duration > 0 && frame_idx < cfg.duration) || (cfg.duration < 0 && elapsed_s < static_cast<double>(-cfg.duration)); frame_idx++) {
        if (record_in.has_value()) {
            getCamera()->readFrom(record_in.value());
            if (record_in->eof()) {
                record_in->close();
                record_in = {};
                break;
            }
            if (record_in->fail()) {
                throw std::runtime_error("Error reading camera pose from " + cfg.record_file_in);
            }
        } else {
            // if an automated video is rendered, animate the parameters based on the config
            Camera *const camera = getCamera();

            float v = 0.f;
            if (cfg.duration > 0) // equidistant interpolation based on frame index
                v = static_cast<float>(frame_idx) / static_cast<float>(cfg.duration);
            else if (cfg.duration < 0) { // "real-time" interpolation based on passed duration
                if (frame_idx == 0u) {
                    elapsed_s = 0.f;
                } else {
                    // either use pre-recorded timings or measure the timings as given
                    if (cfg.video_frame_times) {
                        if (frame_idx >= cfg.video_frame_times->size())
                            break;
                        elapsed_s += cfg.video_frame_times->at(frame_idx - 1u) / 1000.;
                    } else {
                        elapsed_s = timer.elapsed();
                    }
                    if (elapsed_s > static_cast<double>(-cfg.duration))
                        break;
                    v = static_cast<float>(elapsed_s);
                    Logger(Info) << v << "," << timer.elapsed() << " / " << static_cast<double>(-cfg.duration) << " " << (cfg.video_frame_times ? cfg.video_frame_times->size() : 0);
                }
            }
            // add edges for interpolation
            v = glm::clamp((v - cfg.edge_start) / (cfg.edge_end - cfg.edge_start), 0.f, 1.f);

            switch (cfg.interpolation) {
            case HeadlessRenderingConfig::Interpolant::Smooth:
                v = glm::smoothstep(0.f, 1.f, v);
                break;
            case HeadlessRenderingConfig::Interpolant::Smoother:
                v = v * v * v * (v * (6.f * v - 15.f) + 10.f);
                break;
            default:;
            }

            if (anim.roty_0 != anim.roty_1)
                camera->rotation_y = glm::fract(glm::mix(anim.roty_0, anim.roty_1, v) / (2.f * glm::pi<float>())) * (2.f * glm::pi<float>());
            if (anim.dist_0 != anim.dist_1)
                camera->orbital_radius = glm::mix(anim.dist_0, anim.dist_1, v);
            camera->position_world_space = camera->position_look_at_world_space + glm::vec3(
                                                                                      camera->orbital_radius * glm::cos(camera->rotation_y) * glm::cos(camera->rotation_x),
                                                                                      camera->orbital_radius * glm::sin(camera->rotation_x),
                                                                                      camera->orbital_radius * glm::sin(camera->rotation_y) * glm::cos(camera->rotation_x));

            camera->onCameraUpdate();
        }

        // render one frame after the other = wait for the last renderingComplete to finish
        for (size_t accumulation_idx = 0; accumulation_idx < cfg.accumulation_samples; accumulation_idx++) {
            rendererOutput = renderFrame(rendererOutput.renderingComplete);
        }

        if (!cfg.video_fmt_file_out.empty()) {
            m_renderer->exportCurrentFrameToImage(fmt::vformat(cfg.video_fmt_file_out, fmt::make_format_args(frame_idx)));
        }

        if (cfg.frameFinishedCallback) {
            cfg.frameFinishedCallback(&rendererOutput);
        }
    }

    // stop the frame time tracking here with an awaitable (to get the last timing)
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
        frame_idx++;
        Logger(Info, true) << "exporting screenshot to " << last_output_image_path;
        ret_tex->writeFile(last_output_image_path);

        // prevent the renderer from screenshotting the frame again, if more frames are rendered
        m_renderer->exportCurrentFrameToImage("");
    }

    Logger(Info) << "rendering " << (frame_idx * cfg.accumulation_samples)
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

    if (const auto device = getDevice()) {
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
