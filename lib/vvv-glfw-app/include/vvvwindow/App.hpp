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

#include "GuiImgui.hpp"

#include "vvv/core/DefaultGpuContext.hpp"
#include "vvv/core/Renderer.hpp"
#include "vvv/core/Shader.hpp"

#include "vvvwindow/GLFWCameraController.hpp"

#include <filesystem>
#include <optional>
#include <thread>

// forward decl
class GLFWwindow;

namespace vvv {

/// GLFW windowing application displaying renderer parameters using Dear ImGUI.
/// The code here is heavily deprecated and should use modern Vulkan 1.3 VK_KHR_DYNAMIC_RENDERING or even better:
/// modernize and use the GraphicsPass abstraction.
class Application : public DefaultGpuContext, public WindowingSystemIntegration, public std::enable_shared_from_this<Application> {
  private:
    Application(const std::string &appName, const std::shared_ptr<Renderer> &renderer, const std::shared_ptr<DebugUtilities> &debugUtilities)
        : DefaultGpuContext({.debugUtilities = debugUtilities, .appName = appName}), m_renderer(renderer),
          m_camera_controller(), m_gui(std::make_unique<GuiImgui>(this)), m_startup_resolution(1920, 1080), m_fullscreen(false) {
        // choose a camera controller for the renderer
        m_renderer->setCamera(std::make_shared<Camera>(true));
        m_camera_controller.setCamera(&(*m_renderer->getCamera()));

        auto video_directory = Paths::getHomeDirectory().append("volcanite_video");
        if (!std::filesystem::exists(video_directory) && !std::filesystem::create_directory(video_directory)) {
            Logger(Warn) << "Could not create non-existing video export directory " << video_directory;
        } else {
            m_record_file_path = video_directory.generic_string() + "/volcanite_record_file.rec";
            m_video_file_path = video_directory.generic_string() + "/video";
        }
    };

  public:
    [[nodiscard]] static std::shared_ptr<Application> create(std::string appName, std::shared_ptr<Renderer> renderer,
                                                             float guiScaling = 1.f,
                                                             std::shared_ptr<DebugUtilities> debugUtilities = {}) {
        // Not using std::make_shared<Best> because the c'tor is private.
        return std::shared_ptr<Application>(new Application(appName, renderer, debugUtilities));
    }

    const WindowingSystemIntegration *getWsi() const override { return this; }

    /// Acquire all GPU resources including instance, device and swapchain resources.
    /// This method is reintrant.
    void acquireResources();

    /// Release all GPU resources including instance, device and swapchain resources.
    /// This method is reintrant.
    void releaseResources();

    /// Run the renderloop taking ownership of the current thread.
    /// @return status code
    int exec();

    /// Run the renderloop without taking ownership of the current thread.
    /// You MUST NOT call `execAsync` or `exec` to invoke a second instance of the renderloop until the forked renderloop terminates.
    void execAsync();
    std::thread execAsyncAttached();

    void setStartupWindowSize(vk::Extent2D resolution, bool fullscreen = false) {
        m_startup_resolution = resolution;
        m_fullscreen = fullscreen;
    }
    vk::Extent2D getScreenExtent() const override;

    float getScreenContentScale() const override;

    void setWindowSize(int width, int height) const override;
    void setWindowResizable(bool resizable) const override;
    bool isWindowResizable() const override;

    Camera *getCamera() const override { return m_renderer->getCamera().get(); }

    void processHotKeys();

    /// Saves render parameters (camera path) to a temporary file or loads them from this file, depending on rec state.
    void processParameterRecording();

    void processVideoRecording();

    ~Application() { releaseResources(); }

    void setVSync(bool v) {
        if (m_swapchain.vsync != v) {
            m_swapchain.vsync = v;
            m_swapchain.pendingRecreation = true;
        }
    }

    /// sets the renderer parameter file path template, e.g. "~/param_{}.cfg" for quick load store
    void setQuickConfigLocationFmt(std::string config_file_fmt) { m_quick_access_file_fmt = config_file_fmt; }

    /// @return an GuiInterface to which GUI controlled properties can be added in a sequential manner.
    GuiInterface *getGui() const { return m_gui.get(); }

    /// To print out versions of libraries that are available.
    static void logLibraryAvailabilty();

  protected:
    vk::SurfaceKHR createSurface() override;

  private:
    static void errorCallback(int error, const char *description) { std::cerr << "GLFW Error " << error << ": " << description << std::flush; }

    /// @deprecated use MultiBuffering instead
    template <typename T>
    using ForEachSwapchainImage = std::vector<T>;
    /// @deprecated use MultiBuffering instead
    template <typename T>
    using ForEachInFlightFrame = std::vector<T>;

    void createWindow();
    void createQueues();
    void createSwapChain();
    void createBlit();

    void createBlitDescriptorSet();
    void createBlitRenderPass();
    void createBlitFramebuffers();
    void createBlitShaders();
    void createBlitPipeline();

    void destroyWindow();
    void destroyQueues();
    void destroySwapChain();
    void destroyBlit();

    void destroyBlitDescriptorSet();
    void destroyBlitRenderPass();
    void destroyBlitFramebuffers();
    void destroyBlitShaders();
    void destroyBlitPipeline();

    void recreateSwapchain();
    void recreateShaderResources();
    void recreateInnerRenderingEngine();

    void renderFrame();
    void renderFrameRecordCommands(vk::CommandBuffer, RendererOutput const &ldrRendererOutput);
    void updateBlitDescriptorSet(const RendererOutput &output, uint32_t inFlightFrameIdx);

    std::shared_ptr<Renderer> m_renderer;

    vk::Extent2D m_startup_resolution;
    bool m_fullscreen;
    bool m_resources_acquired = false;
    GLFWwindow *m_window = nullptr;
    GLFWCameraController m_camera_controller;
    std::unique_ptr<GuiImgui> m_gui;

    struct {
        vk::Queue graphics = nullptr;
        vk::Queue present = nullptr;
    } m_queues;

    /** state bound to the lifetime of the swapchain */
    struct {
        bool pendingRecreation = false;
        bool vsync = false;

        vk::SwapchainKHR swapchain = nullptr;
        ForEachSwapchainImage<vk::Image> images = {};
        ForEachSwapchainImage<vk::ImageView> views = {};
        vk::Extent2D extent;
        vk::Format colorFormat = vk::Format::eUndefined;
        vk::Format depthFormat = vk::Format::eUndefined;

        ForEachInFlightFrame<vk::Semaphore> presentCompleteSemaphore;
        ForEachInFlightFrame<vk::Semaphore> blitToSwapchainImageComplete;
        ForEachInFlightFrame<vk::Semaphore> renderCompleteSemaphore;
        ForEachInFlightFrame<AwaitableHandle> frameInFlightAwaitable;
        /// points to the Awaitable index in `frameInFlightAwaitable`, which is the frame that uses this image. Can also be IMAGE_NOT_IN_FLIGHT
        ForEachSwapchainImage<size_t> imageInFlightFrame;

        // Note: the number of required buffers is ForEachSwapchainImage if we prerecord everything once.
        // ForEachInFlightFrame is enough, if we rerecord command buffers each frame.
        vk::CommandPool commandPool = nullptr;
        ForEachSwapchainImage<vk::CommandBuffer> commandBuffers;
    } m_swapchain;

    /** renderpass specific resources, note that some state may be rebuilt after a swapchain rebuild
        because the image formats and number of in flight frames might change */
    struct {
        vk::DescriptorPool descPool = nullptr;
        vk::DescriptorSetLayout descSetLayout = nullptr;
        ForEachInFlightFrame<vk::DescriptorSet> descSet;
        ForEachInFlightFrame<std::optional<vk::DescriptorImageInfo>> lastImageDescriptor;

        Shader *shaderVertex = nullptr;
        Shader *shaderFragment = nullptr;

        vk::PipelineCache pipelineCache = nullptr;
        vk::PipelineLayout pipelineLayout = nullptr;
        vk::Pipeline pipeline = nullptr;

        vk::RenderPass renderpass = nullptr;
        ForEachSwapchainImage<vk::Framebuffer> framebuffers = {};
    } m_renderpass;

    static void framebufferResizeCallback(GLFWwindow *window, int _width, int _height);

    // quick store / load of renderer parameter files (CTRL +) [0-9]
    std::string m_quick_access_file_fmt;
    // Recording / Replaying of camera paths
    std::string m_record_file_path;
    std::optional<std::ofstream> m_record_out = {};
    std::optional<std::ifstream> m_record_in = {};
    // for last camera path: video timestamp output
    std::string m_video_file_path;
    std::optional<std::ofstream> m_video_timing = {};
    double m_video_last_timestamp = 0;
    size_t m_video_frame_count = 0;
    std::optional<int> m_video_frame = {};

    double min_ms = 9999999999., avg_ms = 0., var_ms = 0., max_ms = 0.;
    size_t avg_ms_samples = 0;

#ifdef IMGUI
    bool m_display_imgui = true;
    // ------------- imgui
    // move this to the GuiImgui class?
    struct {
        vk::DescriptorPool descPool = nullptr;
        uint32_t minImageCount = 2;
        bool initialized = false;
    } m_imgui;

    void initImGui();
    void recreateSwapchainImGui();
    void shutdownImGui();
    //-------------- end imgui
#endif
};

} // namespace vvv
