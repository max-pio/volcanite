#pragma once

#include <memory>
#include <optional>
#include <glm/glm.hpp>
#include <utility>

#include "vvv/core/Renderer.hpp"
#include "vvv/core/Shader.hpp"
#include "vvv/util/managed_buffer.hpp"
#include "vvv/reflection/UniformReflection.hpp"
#include "vvv/passes/PassCompute.hpp"

namespace vvv {


class HelloVolumeRenderer : public Renderer, public WithGpuContext {

public:
    HelloVolumeRenderer() : WithGpuContext(nullptr) {}

    RendererOutput renderNextFrame(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr) override;
    /**
     * Initializes Descriptorsets and calls pipeline initialization.
     */
    void initResources(GpuContext *ctx) override;
    void releaseResources() override;
    /**
     * Initialize everything that depends on shader
     */
    void initShaderResources() override;
    void releaseShaderResources() override;
    /**
     * Initializes command buffer, renderpass, images and framebuffers
     */
    void initSwapchainResources() override;
    void releaseSwapchain() override;

    void initGui(vvv::GuiInterface * gui) override {
        auto g = gui->get("Compressed Segmentation Volume Renderer");
        g->addColor(&m_background_color_a, "Background Color A");
        g->addColor(&m_background_color_b, "Background Color B");
        g->addLabel("Debug");
        g->addBool(&m_show_model_space, "Show Model Space");
        g->addAction([this](){getCtx()->getWsi()->getCamera()->reset();}, "Reset Camera");
    };

    const std::optional<RendererOutput> &mostRecentFrame() { return m_mostRecentFrame; }

private:
    // gui parameters
    glm::vec4 m_background_color_a = glm::vec4(0.1f, 0.1f, 0.15f, 1.f);
    glm::vec4 m_background_color_b = glm::vec4(0.2f, 0.2f, 0.3f, 1.f);
    bool m_show_model_space = true;


    void updateUniformDescriptorset();

    std::unique_ptr<SinglePassCompute> m_pass = nullptr;
    std::shared_ptr<MultiBufferedResource<std::shared_ptr<Texture>>> m_outColor = nullptr;
    std::shared_ptr<UniformReflected> m_urender_info = nullptr;

    //std::shared_ptr<Volume<uint32_t>> m_volume;
    bool m_data_changed;

    MultiBufferedResource<size_t> m_camHash;
    std::optional<RendererOutput> m_mostRecentFrame = {};
};

} // namespace vvv