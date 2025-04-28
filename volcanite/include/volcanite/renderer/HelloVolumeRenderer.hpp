//  Copyright (C) 2024, Max Piochowiak, Karlsruhe Institute of Technology
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

#include <glm/glm.hpp>
#include <memory>
#include <optional>

#include "vvv/core/Renderer.hpp"
#include "vvv/passes/PassCompute.hpp"
#include "vvv/reflection/UniformReflection.hpp"

using namespace vvv;

namespace volcanite {

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

    void initGui(vvv::GuiInterface *gui) override {
        auto g = gui->get("Compressed Segmentation Volume Renderer");
        g->addColor(&m_background_color_a, "Background Color A");
        g->addColor(&m_background_color_b, "Background Color B");
        g->addLabel("Debug");
        g->addBool(&m_show_model_space, "Show Model Space");
        g->addAction([this]() { getCtx()->getWsi()->getCamera()->reset(); }, "Reset Camera");
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

    // std::shared_ptr<Volume<uint32_t>> m_volume;
    bool m_data_changed;

    MultiBufferedResource<size_t> m_camHash;
    std::optional<RendererOutput> m_mostRecentFrame = {};
};

} // namespace volcanite