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

#include "vvv/core/DefaultGpuContext.hpp"
#include "vvv/core/Renderer.hpp"

#include <memory>
#include <thread>
#include <utility>

namespace vvv {

class DummyGuiInterface : public GuiInterface {
  public:
    explicit DummyGuiInterface() = default;
    virtual ~DummyGuiInterface() = default;
    void updateGui() override {}
};

struct HeadlessRenderingConfig {
    const std::string &record_file_in = "";                    ///< if set, replays pre-recorded camera positions from this file
    const std::string &video_fmt_file_out = "";                ///< if set, outputs video frames to file path with an integer fmt placeholder , e.g.
    size_t accumulation_samples = 1;                           ///< number of frames after which a new camera position is read and a video frame is exported
    void (*frameFinishedCallback)(RendererOutput *) = nullptr; ///< will be called each time a frame finished rendering after accumulation_samples
};

class HeadlessRendering : public DefaultGpuContext, public std::enable_shared_from_this<HeadlessRendering> {
  private:
    HeadlessRendering(std::string appName, std::shared_ptr<Renderer> renderer, std::shared_ptr<DebugUtilities> debugUtilities)
        : DefaultGpuContext({.debugUtilities = std::move(debugUtilities), .appName = std::move(appName)}),
          m_renderer(std::move(renderer)), m_pendingRecreation(false), m_gui(std::make_unique<DummyGuiInterface>()) {
          };

  public:
    [[nodiscard]] static std::shared_ptr<HeadlessRendering> create(std::string appName, std::shared_ptr<Renderer> renderer, std::shared_ptr<DebugUtilities> debugUtilities = {}) {
        // Not using std::make_shared<Best> because the constructor is private.
        return std::shared_ptr<HeadlessRendering>(new HeadlessRendering(std::move(appName), std::move(renderer), std::move(debugUtilities)));
    }

    /// Acquire all GPU resources including instance and device resources. This method must be called before any
    /// rendering is processed.
    /// This method is reintrant.
    void acquireResources();
    /// Release all GPU resources including instance, device and swapchain resources.
    /// This method is reintrant.
    void releaseResources();

    /// Run the renderloop for number_of_frames taking ownership of the current thread.
    /// @param cfg the rendeirng configuration
    /// number_of_frames number of frames to render. can be zero if record_file_in is given to use record length.
    /// record_file_in a previously recorded camera path that is played when rendering the frames. "" for none.
    /// video_fmt_file_out image file path string that contains a single replacement field {*} for
    /// <a href="https://fmt.dev/latest/syntax/">fmt::format</a> for the integer frame index.
    /// Example: "./out{:3}.png"
    /// frameFinishedCallback is called everytime a frame finished with the current texture output.
    /// @return the final Texture of the render loop.
    std::shared_ptr<Texture> renderFrames(const HeadlessRenderingConfig &cfg);

    //    /// Run the renderloop without taking ownership of the current thread.
    //    /// You MUST NOT call `execAsync` or `exec` to invoke a second instance of the renderloop until the forked renderloop terminates.
    //    void execAsync();
    //    std::thread execAsyncAttached();

    Camera *getCamera() const { return m_renderer->getCamera().get(); }

    ~HeadlessRendering() override {
        releaseResources();
        m_gui = nullptr;
    }

    /// @return an GuiInterface to which GUI controlled properties can be added in a sequential manner.
    GuiInterface *getGui() const { return m_gui.get(); }

  private:
    void createQueues();
    void destroyQueues();

    void recreateSwapchain();
    void recreateShaderResources();
    void recreateInnerRenderingEngine();

    RendererOutput renderFrame(AwaitableList awaitBeforeExecution);

    std::shared_ptr<Renderer> m_renderer;
    bool m_pendingRecreation;

    std::unique_ptr<DummyGuiInterface> m_gui = nullptr;

    struct {
        vk::Queue graphics = nullptr;
        vk::Queue compute = nullptr;
        vk::Queue present = nullptr;
    } m_queues;
};

} // namespace vvv
