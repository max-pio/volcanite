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

#include "imgui.h"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"

namespace vvv {


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

    void initGui(vvv::GuiInterface * gui) override {
        auto g = gui->get("Compressed Segmentation Volume Brick Visualizer");
        g->addColor(&m_background_color_a, "Background Color A");
        g->addColor(&m_background_color_b, "Background Color B");
        g->addInt(&m_brick_id.x, "Brick X");
        g->addInt(&m_brick_id.y, "Brick Y");
        g->addInt(&m_brick_id.z, "Brick Z");
        g->addInt(&m_brick_slice, "Brick Slice", 0, 15, 1);
        g->addInt(&m_label_color_mult, "Label Color Cycle", 1, 100000, 5);
        g->addBool(&m_show_label_bits, "Show Label Bits");
        g->addCombo(&m_show_code_mode, {"All", "New Palette", "Flat"}, [this](int v) { m_show_code_mode = v; });
        g->addCustomCode([this](){
            auto mousePos = ImGui::GetMousePos();
            m_mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            m_mouseHeldDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            if (m_mouseHeldDown) {
                m_mousePos = mousePos;
            }
        }, "Mouse");
    };

    void setCompressedSegmentationVolume(std::shared_ptr<CompressedSegmentationVolume> tree) {
        m_compressed_segmentation_volume = std::move(tree);
        m_data_changed = true;
    }

    const std::optional<RendererOutput> &mostRecentFrame() { return m_mostRecentFrame; }

private:
    // gui parameters
    glm::vec4 m_background_color_a = glm::vec4(1.f, 1.f, 1.f, 1.f);
    glm::vec4 m_background_color_b = glm::vec4(0.9f, 0.95f, 1.f, 1.f);
    glm::ivec3 m_brick_id = glm::ivec3(0);
    int m_brick_slice = 0;
    glm::ivec3 m_current_decoded_brick = glm::ivec3(0);
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

} // namespace vvv