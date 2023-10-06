#include <memory>
#include <string>


#include "vvv/util/Logger.hpp"
#include "vvvwindow/entrypoint.hpp"

#include "vvv/util/detect_debugger.hpp"
#include "vvvwindow/App.hpp"

#include "vvv/passes/SinglePassGraphics.hpp"
#include "vvv/core/Renderer.hpp"
#include <utility>

namespace vvv {

class HelloTriangleRenderer : public Renderer, public WithGpuContext {

public:
    HelloTriangleRenderer() : WithGpuContext(nullptr) {}

    RendererOutput renderNextFrame(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr) override {
        m_uniformConstants->upload(m_pass->getActiveIndex());
        const auto renderingFinished = m_pass->execute(awaitBeforeExecution, awaitBinaryAwaitableList);

        return vvv::RendererOutput{
            .texture = m_colorTexture.get(),
            .renderingComplete = {renderingFinished},
        };
    };

    void initResources(vvv::GpuContextRwPtr ctx) override {
        setCtx(ctx);
        m_pass = std::make_unique<SinglePassFullscreenGraphics>(ctx, ctx->getWsi()->stateInFlight(), "background_gradient.frag");
        m_pass->allocateResources();
    }

    void releaseResources() override {
        m_uniformConstants = nullptr;
        m_pass->freeResources();
    }

    void initSwapchainResources() override {
        const auto screen = getCtx()->getWsi()->getScreenExtent();

        // create color and depth attachment textures
        m_colorTexture = m_pass->reflectColorAttachment("outColor", {.width = screen.width, .height = screen.height, .usage = vk::ImageUsageFlagBits::eSampled, .queues = vvv::TextureExclusiveQueueUsage});
        m_colorTexture->ensureResources();
        m_colorTexture->setName("HelloTriangleRenderer.m_colorAttachmentTexture");
        const auto layoutTransformDone = m_colorTexture->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eAllGraphics);

        getCtx()->sync->hostWaitOnDevice({layoutTransformDone});
        m_pass->setColorAttachment("outColor", m_colorTexture);

        m_uniformConstants = m_pass->getUniformSet("gradient");
        // set default values for background color
        m_uniformConstants->setUniform("colorBottomRight", glm::vec4(1.0f, 1.0f, 0.0f, 0.f));
        m_uniformConstants->setUniform("colorTopLeft", glm::vec4(0.6f, 0.0f, 0.6f, 0.f));
    }

    void releaseSwapchain() override {
        m_colorTexture = nullptr;
    }

private:
    std::unique_ptr<SinglePassFullscreenGraphics> m_pass = nullptr;
    std::shared_ptr<Texture> m_colorTexture = nullptr;
    std::shared_ptr<UniformReflected> m_uniformConstants = nullptr;
};

} // namespace vvv

int graphics_pass_test(int argc, char *argv[]) {
    vvv::Logger(vvv::INFO) << "running Cell Growth build";
    std::string appName = "Graphics Pass Test";

    auto renderer = std::make_shared<vvv::HelloTriangleRenderer>();

    // the NastjaRenderer currently only renders point clouds
    auto app = Application::create(appName, renderer);

    // execute app
    app->setVSync(true);
    auto returnValue = app->exec();

    return returnValue;
}

ENTRYPOINT(graphics_pass_test)
