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
#include <utility>

#include "vvv/core/Renderer.hpp"
#include "vvv/passes/PassCompute.hpp"
#include "vvv/reflection/UniformReflection.hpp"

#ifdef IMGUI
#include "imgui.h"
#endif
#include "volcanite/compression/CompressedSegmentationVolume.hpp"

using namespace vvv;

namespace volcanite {

class CompressedSegmentationVolumeBrickViewer : public Renderer, public WithGpuContext {

  public:
    CompressedSegmentationVolumeBrickViewer() : WithGpuContext(nullptr), m_compressed_segmentation_volume(nullptr), m_data_changed(false) {}

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
        assert(m_compressed_segmentation_volume && "must set CSGV data set before starting csgv brick viewer");
        const glm::ivec3 brick_count = {m_compressed_segmentation_volume->getBrickCount()};
        const int brick_size = static_cast<int>(m_compressed_segmentation_volume->getBrickSize());

        m_csgv_infos.emplace_back("Volume", m_compressed_segmentation_volume->getLabel());
        m_csgv_infos.emplace_back("Encoding Mode", EncodingMode_STR(m_compressed_segmentation_volume->getEncodingMode()));
        uint32_t op_mask = m_compressed_segmentation_volume->getOperationMask();
        m_csgv_infos.emplace_back("Operation Mask", OperationMask_STR(op_mask));
        m_csgv_infos.emplace_back("Max. Palette Size", std::to_string(m_compressed_segmentation_volume->getMaxBrickPaletteCount()));
        m_csgv_infos.emplace_back("Unique Labels", std::to_string(m_compressed_segmentation_volume->getNumberOfUniqueLabelsInVolume()));
        m_csgv_infos.emplace_back("Brick Size", std::to_string(m_compressed_segmentation_volume->getBrickSize()));
        m_csgv_infos.emplace_back("LOD Count", std::to_string(m_compressed_segmentation_volume->getLodCountPerBrick()));
        m_csgv_infos.emplace_back("Brick Count", str(m_compressed_segmentation_volume->getBrickCount()));
        m_csgv_infos.emplace_back("Volume Size", str(m_compressed_segmentation_volume->getVolumeDim()));
        m_csgv_infos.emplace_back("Compression Ratio", std::to_string(m_compressed_segmentation_volume->getCompressionRatio()) + "%");

        auto g = gui->get("Compressed Segmentation Volume Brick Visualizer");
        g->addInt(&m_brick_id.x, "Brick X", 0, brick_count.x - 1, 1);
        g->addInt(&m_brick_id.y, "Brick Y", 0, brick_count.y - 1, 1);
        g->addInt(&m_brick_id.z, "Brick Z", 0, brick_count.z - 1, 1);
        g->addInt(&m_brick_slice, "Brick Slice", 0, brick_size - 1, 1);
        g->addSeparator();
        g->addInt(&m_label_color_mult, "Label Color Cycle", 1, 100000, 5);
        g->addCombo(&m_show_code_mode, {"All", "New Palette", "Flat"}, [this](int v, bool by_user) { m_show_code_mode = v; });
        g->addBool(&m_show_label_bits, "Show Label Bits");
        g->addColor(&m_background_color_a, "Background Color A");
        g->addColor(&m_background_color_b, "Background Color B");
        g->addSeparator();
        for (auto &m_csgv_info : m_csgv_infos) {
            g->addDynamicText(&m_csgv_info.second, m_csgv_info.first);
        }
#ifdef IMGUI
        g->addCustomCode([this]() {
            auto mousePos = ImGui::GetMousePos();
            m_mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            m_mouseHeldDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            if (m_mouseHeldDown) {
                m_mousePos = mousePos;
            }
        },
                         "Mouse");
#endif
    };

    void setCompressedSegmentationVolume(std::shared_ptr<CompressedSegmentationVolume> tree) {
        m_compressed_segmentation_volume = std::move(tree);
        m_data_changed = true;

        m_brick_id = m_compressed_segmentation_volume->getBrickCount() / 2u;
        m_brick_slice = static_cast<int>(m_compressed_segmentation_volume->getBrickSize() / 2u);
    }

    const std::optional<RendererOutput> &mostRecentFrame() { return m_mostRecentFrame; }

  private:
    // gui parameters
    std::vector<std::pair<std::string, std::string>> m_csgv_infos;
    glm::vec4 m_background_color_a = glm::vec4(1.f, 1.f, 1.f, 1.f);
    glm::vec4 m_background_color_b = glm::vec4(1.f, 1.f, 1.f, 1.f);
    glm::ivec3 m_brick_id = glm::ivec3(0);
    int m_brick_slice = 0;
    glm::ivec3 m_current_decoded_brick = glm::ivec3(-1);
    bool m_show_label_bits = false;
    int m_show_code_mode = 0;
    int m_label_color_mult = 1;

    glm::vec2 m_mousePos = glm::vec2(0.f);
    bool m_mouseClicked = false;
    bool m_mouseHeldDown = false;
    MiniTimer m_timer;

    void updateUniformDescriptorset();

    std::unique_ptr<SinglePassCompute> m_pass = nullptr;
    std::shared_ptr<MultiBufferedResource<std::shared_ptr<Texture>>> m_outColor = nullptr;
    std::shared_ptr<UniformReflected> m_urender_info = nullptr;
    std::shared_ptr<UniformReflected> m_usegmented_volume_info = nullptr;

    std::shared_ptr<CompressedSegmentationVolume> m_compressed_segmentation_volume;
    bool m_data_changed;
    std::shared_ptr<Buffer> m_encoding_buffer = nullptr;
    std::shared_ptr<Buffer> m_brick_starts_buffer = nullptr;
    std::shared_ptr<Buffer> m_cache_buffer = nullptr;
    std::shared_ptr<Buffer> m_palette_buffer = nullptr;
    std::shared_ptr<Buffer> m_enumbrickpos_buffer = nullptr;
    std::shared_ptr<Texture> m_encoding_tex = nullptr;

    std::optional<RendererOutput> m_mostRecentFrame = {};
};

} // namespace volcanite
