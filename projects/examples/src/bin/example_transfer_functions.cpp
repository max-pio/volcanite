#include <vvv/passes/PassCompute.hpp>
#include <vvv/volren/tf/VectorTransferFunction.hpp>
#include <vvv/volren/tf/TransferFunction1D.hpp>
#include <vvv/volren/tf/TransferFunction2D.hpp>
#include <vvv/volren/tf/builtin.hpp>
#include <vvv/util/Paths.hpp>

#include <vvvwindow/entrypoint.hpp>
#include <vvvwindow/App.hpp>

#include <cstdlib>

using namespace vvv;

class TFRenderer : public Renderer, public WithGpuContext {
public:
    explicit TFRenderer() : WithGpuContext(nullptr) {}

    void initGui(vvv::GuiInterface *gui) override {
        histogram1D = {0, 0.3, 0.4, 1.0, 0.1, 1.5, 0.2};

        auto settings = gui->get("Transfer Function Test Settings");
        settings->addCombo(&m_renderMode, m_renderModes);
        settings->addSeparator();
        settings->addTF1D(m_vectorTF.get(), &histogram1D, &m_histogramMin.x, &m_histogramMax.x, [this]() {
            getCtx()->sync->hostWaitOnDevice({m_lastFrameAwaitable});
            m_tf1D = m_vectorTF->rasterize(getCtx(), m_resolution);
            auto [tf1dAwait, tf1dStagingBuf] = m_tf1D->upload();
            getCtx()->sync->hostWaitOnDevice({tf1dAwait});
            m_computePass->setImageSampler("SAMPLER_TF1D", m_tf1D->texture(), vk::ImageLayout::eReadOnlyOptimal, false);
        });
        settings->addTF2D(m_tf2D.get(), histogram2DTexture.get(), nullptr, [this]() {
            auto [await, stagingBuf] = m_tf2D->upload();
            getCtx()->sync->hostWaitOnDevice({await});
        }, &m_histogramMin, &m_histogramMax);
        settings->addVec2(&m_histogramMin, "minimum", glm::vec2{0}, glm::vec2{1}, glm::vec2{0.01});
        settings->addVec2(&m_histogramMax, "maximum", glm::vec2{0}, glm::vec2{1}, glm::vec2{0.01});
        settings->addSeparator();
        settings->addInt([this](int a) {
            m_resolution = a;
            if (m_resolution > 1) {
                getCtx()->sync->hostWaitOnDevice({m_lastFrameAwaitable});
                m_tf1D = m_vectorTF->rasterize(getCtx(), m_resolution);
                auto [tf1dAwait, tf1dStagingBuf] = m_tf1D->upload();
                getCtx()->sync->hostWaitOnDevice({tf1dAwait});
                m_computePass->setImageSampler("SAMPLER_TF1D", m_tf1D->texture(), vk::ImageLayout::eReadOnlyOptimal, false);
            }
        }, [this](){ return m_resolution; }, "resolution");
    }

    void initResources(vvv::GpuContextRwPtr ctx) override {
        setCtx(ctx);
        AwaitableList awaitList;

        m_vectorTF = std::make_shared<VectorTransferFunction>(colormaps::jet);
        m_tf1D = m_vectorTF->rasterize(ctx, m_resolution);
        m_tf2D = std::make_shared<vvv::TransferFunction2D>(ctx, ctx->getWsi()->stateInFlight(), m_resolution);
        m_tf2D->setPolygons({{{.5,.5},{.5, .9},{.75,.75},{.75,.5}}, {{.25,.25},{.25, .5},{.5,.25}}});

        auto [tf1dAwait, tf1dStagingBuf] = m_tf1D->upload();
        awaitList.push_back(tf1dAwait);
        auto [tf2dAwait, tf2dStagingBuf] = m_tf2D->upload();
        awaitList.push_back(tf2dAwait);

        const size_t histSize = 16;
        using HistType = uint8_t;
        std::array<HistType, histSize * histSize> histData{};
        for(auto& v : histData) v = 0;
        for(int i = 0; i < 100; i++) {
            float angle = static_cast<float>(rand() % 10000) / 10000.0f * 2 * M_PI;
            float r = static_cast<float>(rand() % 10000) / 10000.0f;
            r = 0.5f * glm::sqrt(r) + 0.5f * r;
            auto p = r * glm::vec2{ glm::sin(angle), glm::cos(angle) } * static_cast<float>(0.5f * histSize) + glm::vec2{0.5f * histSize};
            p = glm::clamp(p, 0.f, histSize - 0.5f);
            histData[static_cast<size_t>(p.y) * histSize + static_cast<size_t>(p.x)]++;
        }
        auto maxVal = *std::max_element(histData.begin(), histData.end());
        for(auto& v : histData) {
            v *= static_cast<HistType>(static_cast<float>(std::numeric_limits<HistType>::max()) / maxVal);
        }
        histogram2DTexture = std::make_shared<Texture>(getCtx(), vk::Format::eR8Unorm, histSize, histSize, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
        histogram2DTexture->initResources();
        histogram2DTexture->setName("histogram_2d");
        auto [hist2DUploadAwait, hist2DStaging] = histogram2DTexture->upload(histData.data());
        awaitList.push_back(hist2DUploadAwait);

        getCtx()->sync->hostWaitOnDevice(awaitList);
    };

    void initShaderResources() override {
        m_computePass = std::make_unique<SinglePassCompute>(SinglePassComputeSettings{.ctx = getCtx(), .label = "tf_test", .multiBuffering = getCtx()->getWsi()->stateInFlight()},
                                                            SimpleGlslShaderRequest{.filename = "tf_test.comp", .label = "tf_test"});
        m_computePass->allocateResources();
        m_computePass->setImageSampler("SAMPLER_TF1D", m_tf1D->texture(), vk::ImageLayout::eUndefined, false);
        m_computePass->setImageSampler("SAMPLER_TF2D", m_tf2D->texture(), vk::ImageLayout::eUndefined, false);

        m_optionsUniform = m_computePass->getUniformSet("options");
    }

    void initSwapchainResources() override {
        assert(m_computePass);

        const auto screen = getCtx()->getWsi()->getScreenExtent();
        uint32_t width = screen.width, height = screen.height;

        m_computePass->setGlobalInvocationSize(width, height);

        auto reflectOpts = TextureReflectionOptions{
            .width = width, .height = height, .format = vk::Format::eR8G8B8A8Unorm, .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled};
        m_outputTextures = m_computePass->reflectTextures("IMAGE_out", reflectOpts);

        for (auto& texture : *m_outputTextures) {
            texture->initResources();
        }
    }
    
    RendererOutput renderNextFrame(AwaitableList awaitBeforeExecution, BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) override {
        assert(m_computePass);
        assert(m_outputTextures);

        m_optionsUniform->setUniform("mode", m_renderMode);
        m_optionsUniform->upload(m_outputTextures->getActiveIndex());

        auto await = m_outputTextures->getActive()->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eComputeShader, {.await = {awaitBeforeExecution}});
        m_computePass->setStorageImage("IMAGE_out", *m_outputTextures->getActive(), vk::ImageLayout::eGeneral);

        m_lastFrameAwaitable = m_computePass->execute({await});
        return {.texture = m_outputTextures->getActive().get(), .renderingComplete = {m_lastFrameAwaitable}};
    }

    void releaseGui() override {

    };

    void releaseSwapchain() override {
        m_outputTextures = nullptr;
        m_lastFrameAwaitable = nullptr;
    }

    void releaseShaderResources() override {
        m_optionsUniform = nullptr;

        m_computePass->freeResources();
        m_computePass = nullptr;
    }

    void releaseResources() override {
        histogram2DTexture = nullptr;
        m_vectorTF = nullptr;
        m_tf1D = nullptr;
        m_tf2D = nullptr;
    }

private:
    std::unique_ptr<SinglePassCompute> m_computePass;
    std::shared_ptr<UniformReflected> m_optionsUniform;
    std::shared_ptr<MultiBufferedTexture> m_outputTextures;

    std::shared_ptr<VectorTransferFunction> m_vectorTF;
    std::shared_ptr<TransferFunction1D> m_tf1D;
    std::shared_ptr<TransferFunction2D> m_tf2D;

    std::vector<float> histogram1D;
    std::shared_ptr<Texture> histogram2DTexture;
    glm::vec2 m_histogramMin = glm::vec2{0};
    glm::vec2 m_histogramMax = glm::vec2{1};

    int m_resolution = 1024;

    const std::vector<std::string> m_renderModes = {
        "Output 1D Transfer Function (sampler1D)",
        "Output 2D Transfer Function (sampler2D)"
    };
    int m_renderMode = 0;

    std::shared_ptr<Awaitable> m_lastFrameAwaitable = nullptr;
};

int transfer_functions(int argc, char *argv[]) {
    auto renderer = std::make_shared<TFRenderer>();
    auto app = Application::create("Transfer Function Test / Example", renderer);
    app->setVSync(true);

    const auto statusCode = app->exec();

    return statusCode;
}

ENTRYPOINT(transfer_functions)