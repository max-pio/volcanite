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

/// @brief This render pass implements Gaussian blur and is executed on the graphics queue.
///
/// It takes a input image (linked with setInputTexture()) and blurs it.
/// Optionally, bilateral filtering is supported. Use setInputTexturesBilateral() to specify either depth, normal or both.
/// The input image should have the usage flags returned from getInputImageUsageFlags().
/// The result is returned by renderBlur() in a RendererOutput struct.
class PassBlur : public PassCompute {
  public:
    enum BilateralMode : uint32_t {
        Disabled = 0,
        DepthOnly,
        NormalOnly,
        DepthNormal
    };

    PassBlur(GpuContextPtr ctx, const std::shared_ptr<MultiBuffering> &multiBuffering, int radius, BilateralMode bilateralMode = Disabled,
             vk::ImageUsageFlags outputImageUsage = {}, const std::string &label = "PassBlur");

    void allocateResources() override;
    void initSwapchainResources();
    void releaseSwapchain();
    void freeResources() override;

    static vk::ImageUsageFlags getInputImageUsageFlags() { return vk::ImageUsageFlagBits::eSampled; }
    void setInputTexture(Texture *inputTexture);
    void setInputTexturesBilateral(Texture *depth, Texture *normal); //< pass depth, normal or both as selected with BilateralMode in constructor

    RendererOutput renderBlur(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr);

    /// Does nothing, use renderBlur() instead
    [[nodiscard]] AwaitableHandle execute(AwaitableList, BinaryAwaitableList, vk::Semaphore *) override { return nullptr; }

    void setKernelRadius(int radius) {
        m_kernelRadius = radius;
        m_kernelDirty = true;
    }
    [[nodiscard]] int getKernelRadius() const { return m_kernelRadius; }

  protected:
    std::vector<std::shared_ptr<Shader>> createShaders() override;

  private:
    std::shared_ptr<Buffer> uploadKernelTexture(VkCommandBuffer commandBuffer);

    vk::ImageUsageFlags m_outputImageUsage;
    std::shared_ptr<UniformReflected> m_uniform = nullptr;

    int m_kernelRadius = 50;
    bool m_kernelDirty = true;
    std::shared_ptr<Texture> m_kernelTexture = nullptr;

    BilateralMode m_bilateralMode;
    Texture *m_bilateralDepthTexture = nullptr;
    Texture *m_bilateralNormalTexture = nullptr;

    Texture *m_inputTexture = nullptr;
    std::shared_ptr<MultiBufferedTexture> m_internalTextures;
    std::shared_ptr<MultiBufferedTexture> m_outputTextures;

    std::shared_ptr<Shader> m_shader1_h = nullptr;
    std::shared_ptr<Shader> m_shader2_v = nullptr;
};

} // namespace vvv