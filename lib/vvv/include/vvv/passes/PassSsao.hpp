//  Copyright (C) 2024, Patrick Jaberg, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
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

#include <vvv/core/Renderer.hpp>
#include <vvv/passes/PassCompute.hpp>

namespace vvv {

/// This render pass implements SSAO and is executed on the graphics queue.
/// It takes depth and world space normals as input images (linked with setInputTextures())
/// and applies ambient occlusion to it.
/// Input images should have the usage flags returned from getInputImageUsageFlags().
/// The result is returned by renderSsao() in a RendererOutput struct.
class PassSsao : public SinglePassCompute {
  public:
    enum Algorithm : int { Crytek,
                           Starcraft,
                           Hbao };

    PassSsao(GpuContextPtr ctx, const std::shared_ptr<MultiBuffering> &multiBuffering,
             vk::ImageUsageFlags outputImageUsage = {}, const std::string &label = "PassSsao", Algorithm algorithm = Algorithm::Starcraft);

    void allocateResources() override;
    void freeResources() override;

    // GUI parameters
    glm::float32 g_ssaoRadius = 0.1f;                 ///< radius in world space
    glm::int32 g_ssaoNumSamples = 32;                 ///< number of individual texture samples
    Algorithm g_ssaoAlgorithm = Algorithm::Starcraft; ///< algorithm used. Changing this requires calling freeResources() and allocateResources()
    glm::float32 g_ssaoBias = 0.01;                   ///< bias distance in world space. [only Starcraft, HBAO]
    glm::float32 g_ssaoFalloff = 100.0;               ///< falloff power factor [only Starcraft]
    glm::int32 g_ssaoNumSteps = 16;                   ///< samples for each horizon [only HBAO]

    /// Add SSAO settings to the Gui.
    /// @param shaderRecompileCallback if a callback is provided, a selection box for the algorithm is added to the gui.
    ///        When it is used in the gui, the callback needs to call releaseSwapchain(), freeResources(), allocateResources(), initSwapchainResources().
    void addToGui(GuiInterface::GuiElementList *gui, std::optional<std::function<void(int, bool)>> shaderRecompileCallback = {});

    static vk::ImageUsageFlags getInputImageUsageFlags() { return vk::ImageUsageFlagBits::eSampled; }

    void setInputTextures(Texture *depthTexture, Texture *normalTexture);

    void releaseSwapchain();
    void initSwapchainResources();

    RendererOutput renderSsao(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr);

  private:
    static const std::map<Algorithm, const char *> algorithmToDefine;
    static const std::map<Algorithm, const char *> algorithmToGuiNames;

    void updateUniforms(uint32_t index);

    vk::ImageUsageFlags m_outputImageUsage;
    std::shared_ptr<UniformReflected> m_perFrameConstantsUniform = nullptr;

    std::shared_ptr<MultiBufferedTexture> m_outputTextures;

    vk::ImageLayout m_inputDepthLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
    Texture *m_inputDepthTexture = nullptr;
    Texture *m_inputNormalTexture = nullptr;
};
} // namespace vvv