#include <utility>
#include <vvv/core/Renderer.hpp>
#include <vvv/passes/SinglePassGraphics.hpp>
#include <vvv/passes/PassSimpleSsao.hpp>
#include <vvv/util/vertex_primitives.hpp>

#include <vvvwindow/entrypoint.hpp>
#include <vvvwindow/App.hpp>

using namespace vvv;

class PointCloudGraphicsPass : public SinglePassGraphics {
public:
    PointCloudGraphicsPass(GpuContextPtr ctx, const std::shared_ptr<MultiBuffering>& multiBuffering)
        : SinglePassGraphics(ctx, "point_cloud_graphics_pass", {
              .colorAttachmentFormats = {{"outColor", vk::Format::eR8G8B8A8Unorm}, {"outNormal", vk::Format::eR8G8B8A8Unorm}},
              .depthAttachmentFormat = vk::Format::eD32Sfloat,
              .vertexShaderName = "point_cloud.vert",
              .fragmentShaderName = "point_cloud.frag"}, multiBuffering),
          WithGpuContext(ctx), WithMultiBuffering(multiBuffering) {

    }

    void setBuffers(std::shared_ptr<Buffer> positionBuffer, std::shared_ptr<Buffer> sphereBuffer) {
        m_positionBuffer = std::move(positionBuffer);
        m_sphereBuffer = std::move(sphereBuffer);
    }

    void createVertexInputDescriptions(std::vector<vk::VertexInputBindingDescription>& vertexBindingDescriptions, std::vector<vk::VertexInputAttributeDescription>& vertexAttributeDescriptions) override {
        vertexAttributeDescriptions.emplace_back( 0, 0, vk::Format::eR32G32B32Sfloat, 0 );
        vertexAttributeDescriptions.emplace_back( 1, 1, vk::Format::eR32G32B32Sfloat, 0 );
        vertexBindingDescriptions.emplace_back(0, sizeof(glm::vec3), vk::VertexInputRate::eVertex);
        vertexBindingDescriptions.emplace_back(1, sizeof(glm::vec3), vk::VertexInputRate::eInstance);
    }

    void draw(vk::CommandBuffer& commandBuffer) override {
        const vk::DeviceSize offset = 0;
        commandBuffer.bindVertexBuffers(0, m_sphereBuffer->getBuffer(), offset);
        commandBuffer.bindVertexBuffers(1, m_positionBuffer->getBuffer(), offset);

        commandBuffer.draw(m_sphereBuffer->getByteSize() / sizeof(glm::vec3), m_positionBuffer->getByteSize() / sizeof(glm::vec3), 0, 0);
    }

    void freeResources() override {
        SinglePassGraphics::freeResources();

        m_positionBuffer = nullptr;
        m_sphereBuffer = nullptr;
    }

private:
    std::shared_ptr<Buffer> m_positionBuffer;
    std::shared_ptr<Buffer> m_sphereBuffer;
};

class PointCloudRenderer : public Renderer, public WithGpuContext {
public:
    PointCloudRenderer() : WithGpuContext(nullptr) {}

    void initGui(vvv::GuiInterface *gui) override {
        auto s = gui->get("Settings");

        s->addBool(&m_guiRotate, "rotate");


        m_ssaoPass->addToGui(s, [this](int){
            // wait until we can recompile the ssao shader and pipeline
            getCtx()->getDevice().waitIdle();

            m_ssaoPass->releaseSwapchain();
            m_ssaoPass->freeResources();

            m_ssaoPass->allocateResources();
            m_ssaoPass->initSwapchainResources();
        });
    }

    void initResources(vvv::GpuContextRwPtr ctx) override {
        setCtx(ctx);

        std::vector<glm::vec3> pos(1024);
        std::random_device rd;
        std::mt19937 rnd(rd());
        std::uniform_real_distribution<float> dis(-1, 1);
        for (auto& p : pos) {
            p = {dis(rnd), dis(rnd), dis(rnd)};
        }
        std::vector<glm::vec3> sphere = VertexPrimitives::createUVSphereVec3();

        m_positionBuffer = std::make_shared<Buffer>(ctx, BufferSettings{ .label = "position", .byteSize = sizeof(glm::vec3) * pos.size(), .usage = vk::BufferUsageFlagBits::eVertexBuffer });
        m_sphereBuffer   = std::make_shared<Buffer>(ctx, BufferSettings{ .label = "spherePos", .byteSize = sizeof(glm::vec3) * sphere.size(), .usage = vk::BufferUsageFlagBits::eVertexBuffer });

        m_positionBuffer->upload(pos);
        m_sphereBuffer->upload(sphere);

        m_ssaoPass = std::make_unique<PassSimpleApplySsao>(getCtx(), getCtx()->getWsi()->stateInFlight(), vk::ImageUsageFlagBits::eSampled);
    }

    void initShaderResources() override {
        m_graphicsPass = std::make_unique<PointCloudGraphicsPass>(getCtx(), getCtx()->getWsi()->stateInFlight());
        m_graphicsPass->setBuffers(m_positionBuffer, m_sphereBuffer);
        m_graphicsPass->allocateResources();
        m_graphicsUniform = m_graphicsPass->getUniformSet("per_frame_constants");

        m_ssaoPass->allocateResources();
    }

    void initSwapchainResources() override {
        auto screen = getCtx()->getWsi()->getScreenExtent();

        auto makeMultiBuffered = [this](const std::shared_ptr<Texture>& tex) { return std::make_shared<MultiBufferedTexture>(m_graphicsPass->getMultiBuffering(), tex); };
        m_colorTextures = makeMultiBuffered (m_graphicsPass->reflectColorAttachment("outColor", {.width = screen.width, .height = screen.height, .format = vk::Format::eR8G8B8A8Unorm, .usage = vk::ImageUsageFlagBits::eSampled | m_ssaoPass->getInputImageUsageFlags(), .queues = vvv::TextureExclusiveQueueUsage}));
        m_normalTextures = makeMultiBuffered(m_graphicsPass->reflectColorAttachment("outNormal", {.width = screen.width, .height = screen.height, .format = vk::Format::eR8G8B8A8Unorm, .usage = vk::ImageUsageFlagBits::eSampled | m_ssaoPass->getInputImageUsageFlags(), .queues = vvv::TextureExclusiveQueueUsage}));
        m_depthTextures = makeMultiBuffered (m_graphicsPass->createDepthStencilAttachment({.width = screen.width, .height = screen.height, .format = vk::Format::eD32Sfloat, .usage = vk::ImageUsageFlagBits::eSampled | m_ssaoPass->getInputImageUsageFlags(), .queues = vvv::TextureExclusiveQueueUsage}));

        for (auto& multiTex : std::array{m_colorTextures, m_normalTextures, m_depthTextures}) {
            for (auto& tex : *multiTex) {
                tex->initResources();
            }
        }

        m_ssaoPass->initSwapchainResources();
    }

    RendererOutput renderNextFrame(AwaitableList awaitBeforeExecution, BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) override {
        auto& wsi = *getCtx()->getWsi();
        auto& camera = *wsi.getCamera();

        static std::chrono::system_clock::time_point lastTime = {};
        if (m_guiRotate) {
            auto now = std::chrono::system_clock::now();
            float dt = std::chrono::duration<float>(now - lastTime).count();
            const float speed = 4.0f;
            camera.rotation_y += speed * dt;

            const float distance = 4.0f;
            camera.position_world_space = distance * glm::vec3{-std::sin(camera.rotation_y), 0, std::cos(camera.rotation_y)};
        }
        lastTime = std::chrono::system_clock::now();

        glm::mat4 world_to_proj = camera.get_world_to_projection_space(wsi.getScreenExtent());
        glm::vec3 camera_pos = camera.position_world_space;
        m_graphicsUniform->setUniform("world_to_projection_space", world_to_proj);
        m_graphicsUniform->setUniform("camera_pos", camera_pos);
        m_graphicsUniform->upload(m_graphicsPass->getActiveIndex());

        m_graphicsPass->setColorAttachment("outColor",  m_colorTextures->getActive());
        m_graphicsPass->setColorAttachment("outNormal", m_normalTextures->getActive());
        m_graphicsPass->setDepthAttachment(m_depthTextures->getActive());

        auto colorStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        auto depthStage = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
        auto colorAwait  = m_colorTextures ->getActive()->setImageLayout(vk::ImageLayout::eColorAttachmentOptimal,        colorStage, {.await = awaitBeforeExecution});
        auto normalAwait = m_normalTextures->getActive()->setImageLayout(vk::ImageLayout::eColorAttachmentOptimal,        colorStage, {.await = awaitBeforeExecution});
        auto depthAwait  = m_depthTextures ->getActive()->setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal, depthStage, {.await = awaitBeforeExecution});

        auto graphicsAwait = m_graphicsPass->execute({colorAwait, normalAwait, depthAwait}, awaitBinaryAwaitableList);

        m_ssaoPass->setInputTextures(m_depthTextures->getActive().get(), m_normalTextures->getActive().get(), m_colorTextures->getActive().get());
        auto ssaoResult = m_ssaoPass->renderSsao({graphicsAwait});

        return ssaoResult;
    }

    void releaseSwapchain() override {
        m_colorTextures = nullptr;
        m_normalTextures = nullptr;
        m_depthTextures = nullptr;

        m_ssaoPass->releaseSwapchain();
    }

    void releaseShaderResources() override {
        m_graphicsUniform = nullptr;

        m_graphicsPass->freeResources();
        m_graphicsPass = nullptr;

        m_ssaoPass->freeResources();
    }

    void releaseResources() override {
        m_ssaoPass = nullptr;

        m_sphereBuffer = nullptr;
        m_positionBuffer = nullptr;
    }

    void releaseGui() override {

    }

private:
    bool m_guiRotate = false;

    std::unique_ptr<PointCloudGraphicsPass> m_graphicsPass;
    std::shared_ptr<UniformReflected> m_graphicsUniform;
    std::shared_ptr<MultiBufferedTexture> m_colorTextures;
    std::shared_ptr<MultiBufferedTexture> m_normalTextures;
    std::shared_ptr<MultiBufferedTexture> m_depthTextures;
    std::shared_ptr<Buffer> m_positionBuffer;
    std::shared_ptr<Buffer> m_sphereBuffer;

    std::unique_ptr<PassSimpleApplySsao> m_ssaoPass;
};


int example_point_cloud(int argc, char *argv[]) {
    auto renderer = std::make_shared<PointCloudRenderer>();
    auto app = Application::create("VVV Point Cloud Renderer Example", renderer);
    app->setVSync(false);

    const auto returnValue = app->exec();

    return returnValue;
}

ENTRYPOINT(example_point_cloud)
