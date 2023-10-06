#pragma once

#include <vvv/passes/PassSsao.hpp>
#include <vvv/passes/PassBlur.hpp>

#include <vvv/core/MultiBuffering.hpp>
#include <vvv/core/WithGpuContext.hpp>

namespace vvv {

/**
 * This is a convenience wrapper for PassSsao and PassBlur.
 * It performs Screen Space Ambient Occlusion based on depth and world space normals, which is then smoothed using PassBlur in bilateral mode.
 * Specity input images using setInputTextures() each frame and call renderSsao().
 */
class PassSimpleSsao : public virtual WithMultiBuffering, public virtual WithGpuContext {
public:
    using Algorithm = PassSsao::Algorithm;

    PassSimpleSsao(GpuContextPtr ctx, const std::shared_ptr<MultiBuffering>& multiBuffering,
             vk::ImageUsageFlags outputImageUsage = {}, const std::string& label = "PassSimpleSsao", Algorithm algorithm = Algorithm::Starcraft);
    virtual ~PassSimpleSsao() = default;


    virtual void allocateResources();
    virtual void initSwapchainResources();
    virtual void releaseSwapchain();
    virtual void freeResources();


    static vk::ImageUsageFlags getInputImageUsageFlags() { return vk::ImageUsageFlagBits::eSampled; }
    void setInputTextures(Texture *depthTexture, Texture *normalTexture);

    virtual RendererOutput renderSsao(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr);

    /**
     * Add SSAO settings to the Gui.
     * @param shaderRecompileCallback if a callback is provided, a selection box for the algorithm is added to the gui.
     *        When it is used in the gui, the callback needs to call releaseSwapchain(), freeResources(), allocateResources(), initSwapchainResources().
     */
    virtual void addToGui(GuiInterface::GuiElementList* gui, std::optional<std::function<void(int)>> shaderRecompileCallback = {});

    // GUI parameters
    glm::float32& g_ssaoRadius = m_ssaoPass.g_ssaoRadius;         ///< radius in world space
    glm::int32&   g_ssaoNumSamples = m_ssaoPass.g_ssaoNumSamples; ///< number of individual texture samples
    Algorithm&    g_ssaoAlgorithm = m_ssaoPass.g_ssaoAlgorithm;   ///< algorithm used. Changing this requires calling freeResources() and allocateResources()
    glm::float32& g_ssaoBias = m_ssaoPass.g_ssaoBias;             ///< bias distance in world space. [only Starcraft, HBAO]
    glm::float32& g_ssaoFalloff = m_ssaoPass.g_ssaoFalloff;       ///< falloff power factor [only Starcraft]
    glm::int32&   g_ssaoNumSteps = m_ssaoPass.g_ssaoNumSteps;     ///< samples for each horizon [only HBAO]

    void setBlurKernelRadius(int radius) { m_blurPass.setKernelRadius(radius); }
    [[nodiscard]] int getBlurKernelRadius() const { return m_blurPass.getKernelRadius(); }

private:
    PassSsao m_ssaoPass;
    PassBlur m_blurPass;
};

/**
 * Calculate Ssao, smooth using bilateral filter and multiply AO with a color image.
 * Specity input images using setInputTextures() each frame and call renderSsao().
 */
class PassSimpleApplySsao : public PassSimpleSsao {
public:
    PassSimpleApplySsao(GpuContextPtr ctx, const std::shared_ptr<MultiBuffering>& multiBuffering,
                   vk::ImageUsageFlags outputImageUsage = {}, const std::string& label = "PassSimpleApplySsao", Algorithm algorithm = Algorithm::Starcraft);

    void allocateResources() override;
    void initSwapchainResources() override;
    void releaseSwapchain() override;
    void freeResources() override;

    void setInputTextures(Texture *depthTexture, Texture *normalTexture, Texture *colorTexture);
    virtual RendererOutput renderSsao(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr) override;

    glm::float32 g_ssaoIntensity = 1.0f; ///< contrast slider. white image at zero
    glm::float32 g_ssaoGamma = 1.0f;     ///< make mids brighter or darker using gamma curve

    virtual void addToGui(GuiInterface::GuiElementList* gui, std::optional<std::function<void(int)>> shaderRecompileCallback = {}) override;

private:
    SinglePassCompute m_applyPass;
    std::shared_ptr<UniformReflected> m_uniform;

    Texture *m_inputColorTexture = nullptr;

    vk::ImageUsageFlags m_outputImageUsage;
    std::shared_ptr<MultiBufferedTexture> m_outputTextures;
};

}