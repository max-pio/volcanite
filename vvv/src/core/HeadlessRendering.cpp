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
}

RendererOutput HeadlessRendering::renderFrame(AwaitableList awaitBeforeExecution) {
    if(m_pendingRecreation)
        recreateSwapchain();
    return m_renderer->renderNextFrame(awaitBeforeExecution, {});
}

std::thread HeadlessRendering::execAsyncAttached() {
    std::thread guiThread(&HeadlessRendering::exec, this);
    return guiThread;
}

void HeadlessRendering::execAsync() { execAsyncAttached().detach(); }

int HeadlessRendering::exec() {
    acquireResources();

    // ToDo: decouple HeadlessRendering::exec in an initialization method and multiple render calls, respect m_pendingRecreation
    // e.g.: hr.init(); hr.setRenderResolution(400, 400); hr.renderToFile(120); hr.setRenderParametersFromFile(path); auto output = hr.render(60);

    // how many frames to render
    const size_t m_render_frames = 60;

    RendererOutput rendererOutput = {nullptr, {}};
    MiniTimer timer;
    for (size_t frame_idx = 0; frame_idx < m_render_frames; frame_idx++) {
        // render one frame after the other = wait for the last renderingComplete to finish
        // ToDo: should we use MultiBuffering in headless mode as well?
        rendererOutput = renderFrame({rendererOutput.renderingComplete});
    }
    double endTime = timer.elapsed();

    if (getDevice()) {
        getDevice().waitIdle();
    }

    double frame_time = endTime / static_cast<double>(m_render_frames);
    Logger(INFO) << "rendering of " << m_render_frames << " frames finished with " << 1. / frame_time << " fps (" << 1000.f * frame_time << "ms/frame)";

    // ToDo: download the result to return?
    // std::vector<uint8_t> renderedImage = rendererOutput.texture->download();
    // dummy export of the output
    rendererOutput.texture->writePng("./volcanite_output.png");
    return 0;
}

void HeadlessRendering::acquireResources() {
    m_renderer->configureExtensionsAndLayersAndFeatures(this);

    createGpuContext();
    createQueues();

    m_renderer->initResources(this);
    m_renderer->initShaderResources();
    m_renderer->initSwapchainResources();
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

vk::Extent2D HeadlessRendering::getRenderResolution() const {
    return m_renderResolution;
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



void HeadlessRendering::setRenderResolution(int width, int height) {
    m_renderResolution.width = width;
    m_renderResolution.height = height;
    m_pendingRecreation = true;
}

} // namespace vvv
