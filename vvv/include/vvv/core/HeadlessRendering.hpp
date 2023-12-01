#pragma once

#include "vvv/core/DefaultGpuContext.hpp"
#include "vvv/core/Renderer.hpp"
#include "vvv/core/Shader.hpp"

#include <optional>
#include <memory>
#include <thread>
#include <utility>
#include <utility>

namespace vvv {

class HeadlessRendering : public vvv::DefaultGpuContext, public std::enable_shared_from_this<HeadlessRendering> {
private:
    HeadlessRendering(std::string appName, std::shared_ptr<vvv::Renderer> renderer, std::shared_ptr<vvv::DebugUtilities> debugUtilities)
            : DefaultGpuContext({.debugUtilities = std::move(debugUtilities), .appName = std::move(appName)}),
            m_renderer(std::move(renderer)), m_renderResolution(1920, 1080), m_pendingRecreation(false)
    {
        // choose a camera controller for the renderer
        m_renderer->setCamera(std::make_shared<vvv::Camera>(false));
    };

public:
    [[nodiscard]] static std::shared_ptr<HeadlessRendering> create(std::string appName, std::shared_ptr<vvv::Renderer> renderer, std::shared_ptr<vvv::DebugUtilities> debugUtilities = {}) {
        // Not using std::make_shared<Best> because the constructor is private.
        return std::shared_ptr<HeadlessRendering>(new HeadlessRendering(std::move(appName), std::move(renderer), std::move(debugUtilities)));
    }

    /**
     * Acquire all GPU resources including instance and device resources. This method must be called before any
     * rendering is processed.
     * This method is reintrant.
     */
    void acquireResources();
    /**
     * Release all GPU resources including instance, device and swapchain resources.
     * This method is reintrant.
     */
    void releaseResources();

    /**
     * Run the renderloop for number_of_frames taking ownership of the current thread.
     * If a frame finished callback is passed it is called everytime a frame finished with the current texture output
     * @return the final Texture of the render loop
     */
    std::shared_ptr<Texture> renderFrames(size_t number_of_frames, void (*frameFinishedCallback)(Texture*) = nullptr);

//    /**
//     * Run the renderloop without taking ownership of the current thread.
//     * You MUST NOT call `execAsync` or `exec` to invoke a second instance of the renderloop until the forked renderloop terminates.
//     */
//    void execAsync();
//    std::thread execAsyncAttached();

    vk::Extent2D getRenderResolution() const;
    void setRenderResolution(int width, int height);

    vvv::Camera *getCamera() const { return m_renderer->getCamera().get(); }

    ~HeadlessRendering() { releaseResources(); }


private:

    void createQueues();
    void destroyQueues();

    void recreateSwapchain();
    void recreateShaderResources();
    void recreateInnerRenderingEngine();

    RendererOutput renderFrame(AwaitableList awaitBeforeExecution);

    std::shared_ptr<vvv::Renderer> m_renderer;
    vk::Extent2D m_renderResolution;
    bool m_pendingRecreation;

    struct {
        vk::Queue graphics = nullptr;
        vk::Queue present = nullptr;
    } m_queues;

};

} // namespace vvv
