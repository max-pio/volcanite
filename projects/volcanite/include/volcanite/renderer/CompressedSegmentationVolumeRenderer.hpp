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
#include "volcanite/renderer/PassCompSegVolRender.hpp"

namespace vvv {

class CompressedSegmentationVolumeRenderer : public Renderer, public WithGpuContext {

public:
    CompressedSegmentationVolumeRenderer(bool release_version = false, bool headless = false) : WithGpuContext(nullptr), m_compressed_segmentation_volume(nullptr), m_data_changed(false), m_camHash(0ul),
                                                                         m_framesSinceCameraMove(0), m_frame(0u), m_release_version(release_version), m_headless(headless) {}

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

    /**
     * We limit the render resolution to max. 4K (4096x2160) or Full-HD.
     */
    vk::Extent2D getRenderResolution() const {
        // ToDo: remove hardcoded render resolution. Move the WSI dependency to Application / HeadlessRendering or the Renderer class?
        const vk::Extent2D max_resolution = {1920u, 1080u};
        //const vk::Extent2D max_resolution = {4096u, 2160u};

        auto wsi = getCtx()->getWsi();
        // context is associated with a window
        if (wsi) {
            auto screen = wsi->getScreenExtent();

            float oversizeFactor = static_cast<float>(screen.width) / static_cast<float>(max_resolution.width);
            if (static_cast<float>(screen.height) / static_cast<float>(max_resolution.height) > oversizeFactor)
                oversizeFactor = static_cast<float>(screen.height) / static_cast<float>(max_resolution.height);
            if (oversizeFactor > 1.f) {
                screen.width = static_cast<uint32_t>(static_cast<float>(screen.width) / oversizeFactor);
                screen.height = static_cast<uint32_t>(static_cast<float>(screen.height) / oversizeFactor);
            }
            return screen;
        }
        // headless rendering
        else {
            return max_resolution;
        }
    }

    void initGui(vvv::GuiInterface * gui) override {
        auto g = gui->get("Compressed Segmentation Volume Renderer");

        if(m_release_version) {
            // for released versions, we show a simplified variant of the gui
            g->addColor(&m_background_color_a, "Background Color Bottom Left");
            g->addColor(&m_background_color_b, "Background Color Top Right");
            g->addFloat(&m_step_size, "Step Size", 0.0005f, 0.01f, 0.0005f, 4);
            g->addCustomCode(
                [this]() {
                    ImGui::DragFloatRange2("Splitting Plane X", &m_bboxMin.x, &m_bboxMax.x, 0.01f, 0.0f, 1.f, "Min: %.2f %%", "Max: %.2f %%");
                    ImGui::DragFloatRange2("Splitting Plane Y", &m_bboxMin.y, &m_bboxMax.y, 0.01f, 0.0f, 1.f, "Min: %.2f %%", "Max: %.2f %%");
                    ImGui::DragFloatRange2("Splitting Plane Z", &m_bboxMin.z, &m_bboxMax.z, 0.01f, 0.0f, 1.f, "Min: %.2f %%", "Max: %.2f %%");
                },
                "Splitting Planes");
            g->addCustomCode(
                    [this]() {
                        auto old_voxel_size = m_voxel_size;
                        ImGui::InputFloat3("Voxel Size", &m_voxel_size.x);
                        if(glm::any(glm::lessThanEqual(m_voxel_size, glm::vec3(0.f)))) {
                            Logger(WARN) << "voxel size must be > 0 in all dimensions! Resetting..";
                            m_voxel_size = old_voxel_size;
                        }
                    },
                    "Voxel Size");
            g->addInt(&m_empty_label, "Empty Label");
            g->addInt(&m_label_minmax.x, "Cell ID Min. 2^n", 0, 32, 1);
            g->addInt(&m_label_minmax.y, "Cell ID Max. 2^n", 0, 32, 1);
            g->addBool(&m_shadow_ray_enabled, "Shadow Ray Enabled");
            g->addDirection(&m_light_direction, "Light Direction");
            g->addDynamicText(&m_gui_resolution_text);
        }
        else {
            g->addColor(&m_background_color_a, "Background Color A");
            g->addColor(&m_background_color_b, "Background Color B");
            g->addInt(&m_subsampling, "Subsampling Factor (2^n)", 0, 2, 1);
            g->addFloat(&m_step_size, "Step Size", 0.0005f, 0.01f, 0.0005f, 4);
            g->addInt(&m_max_steps, "Max Steps", 1, 2048, 1);
            g->addCustomCode(
                [this]() {
                    ImGui::DragFloatRange2("Splitting Plane X", &m_bboxMin.x, &m_bboxMax.x, 0.01f, 0.0f, 1.f, "Min: %.2f %%", "Max: %.2f %%");
                    ImGui::DragFloatRange2("Splitting Plane Y", &m_bboxMin.y, &m_bboxMax.y, 0.01f, 0.0f, 1.f, "Min: %.2f %%", "Max: %.2f %%");
                    ImGui::DragFloatRange2("Splitting Plane Z", &m_bboxMin.z, &m_bboxMax.z, 0.01f, 0.0f, 1.f, "Min: %.2f %%", "Max: %.2f %%");
                },
                "Splitting Planes");
            g->addCustomCode(
                    [this]() {
                        auto old_voxel_size = m_voxel_size;
                        ImGui::InputFloat3("Voxel Size", &m_voxel_size.x);
                        if(glm::any(glm::lessThanEqual(m_voxel_size, glm::vec3(0.f)))) {
                            Logger(WARN) << "voxel size must be > 0 in all dimensions! Resetting..";
                            m_voxel_size = old_voxel_size;
                        }
                    },
                    "Voxel Size");
            g->addInt(&m_empty_label, "Empty Label");
            g->addInt(&m_label_minmax.x, "Label Min. 2^", 0, 32, 1);
            g->addInt(&m_label_minmax.y, "Label Max. 2^", 0, 32, 1);
            g->addFloat(&m_lod_bias, "LOD bias", -4.f, 4.f, 0.1f, 1.f);
            g->addBool(&m_blue_noise, "Blue Noise Shift");
            g->addBool(&m_dda_traversal, "DDA Traversal");
            g->addBool(&m_tonemap_enabled, "Tone Mapping");
            g->addBool(&m_shadow_ray_enabled, "Shadow Ray Enabled");
            g->addFloat(&m_shadow_ao_ray_distr, "Shadow / AO Ray Ratio", 0.f, 1.f, 0.1f, 1);
            g->addDirection(&m_light_direction, "Light Direction");
            g->addFloat(&m_light_intensity, "Light Intensity", 0.f, 10.f, 0.02f, 2);
            g->addFloat(&m_ambient_occlusion_dist_strength.x, "Ambient Occlusion Distance", 1.f, 32.f, 1.f);
            g->addFloat(&m_ambient_occlusion_dist_strength.y, "Ambient Occlusion Strength", 0.f, 1.f, 0.1f);
            g->addSeparator();
            g->addLabel("Debug");
            g->addInt(&m_max_decoding_lod, "Max. LOD", 0, 5, 1);
            g->addBool(&m_show_model_space, "Show Model Space");
            g->addBool(&m_show_brick_cache, "Show Brick Cache");
            g->addBool(&m_show_lod, "Show LOD Levels");
            g->addBool(&m_show_step_count, "Show Ray Step Count");
            g->addAction([this]() { getCamera()->reset(); }, "Reset Camera");
            g->addAction(
                [this]() {
                    if (m_pass)
                        m_pass->resetCacheOnNextCall();
                },
                "Hard Reset Brick Cache");
            g->addBool(&m_clear_cache_every_frame, "Clear Cache Every Frame");
            g->addBool(&m_clear_accum_every_frame, "Clear Accumulation Every Frame");
            g->addSeparator();
            g->addDynamicText(&m_gui_resolution_text);
            g->addAction([this]() { getCtx()->getWsi()->setWindowSize(1920, 1080); }, "1920x1080 FullHD");
            g->addAction([this]() { getCtx()->getWsi()->setWindowSize(3840, 2160); }, "3840x2160 4K");
        }
    }

    void setCompressedSegmentationVolume(std::shared_ptr<CompressedSegmentationVolume> tree) {
        m_compressed_segmentation_volume = std::move(tree);
        m_data_changed = true;
    }

    const std::optional<RendererOutput> &mostRecentFrame() { return m_mostRecentFrame; }

private:
    // (gui) parameters
    glm::vec4 m_background_color_a = glm::vec4(0.9f, 0.9f, 0.95f, 1.f);
    glm::vec4 m_background_color_b = glm::vec4(1.f, 1.f, 1.f, 1.f);
    glm::ivec2 m_label_minmax = glm::ivec2(0, 32);
    bool m_tonemap_enabled = false;
    bool m_shadow_ray_enabled = true;
    float m_shadow_ao_ray_distr = 0.f;
    glm::vec2 m_ambient_occlusion_dist_strength = glm::vec2(15.f, 0.5f);
    glm::vec3 m_light_direction = glm::vec3(-1.f, 1.f, 0.1f);
    float m_light_intensity = 1.f;
    float m_step_size = 0.002f;
    int m_max_steps = 2048;
    int m_subsampling = 0;
    glm::vec3 m_voxel_size = glm::vec3(1.f, 1.f, 1.f);
    glm::vec3 m_bboxMin = glm::vec3(0.f, 0.f, 0.f);
    glm::vec3 m_bboxMax = glm::vec3(1.f, 1.f, 1.f);
    float m_lod_bias = -2.5f;
    bool m_dda_traversal = false;
    bool m_blue_noise = true;
    bool m_show_model_space = false;
    bool m_show_brick_cache = false;
    bool m_show_lod = false;
    bool m_show_step_count = false;
    bool m_clear_cache_every_frame = false;
    bool m_clear_accum_every_frame = false;
    int m_max_decoding_lod = 5;
    int m_empty_label = 0;
    std::string m_gui_resolution_text;


    void updateUniformDescriptorset();

    bool m_headless = false;
    std::unique_ptr<PassCompSegVolRender> m_pass = nullptr;
    std::shared_ptr<Texture> m_feedback_tex[2] = {nullptr, nullptr};
    std::shared_ptr<Texture> m_outDepth = nullptr;
    std::shared_ptr<Texture> m_outColor = nullptr;
    std::shared_ptr<vvv::MultiBufferedResource<std::shared_ptr<Texture>>> m_inpaintedOutColor = nullptr; // this is the output texture and thus the only resource that we have to duplicate for each swapchain image
    std::shared_ptr<UniformReflected> m_urender_info = nullptr;
    std::shared_ptr<UniformReflected> m_usegmented_volume_info = nullptr;

    std::shared_ptr<CompressedSegmentationVolume> m_compressed_segmentation_volume;
    bool m_data_changed;
    std::shared_ptr<Buffer> m_encoding_buffer = nullptr;
    std::shared_ptr<Buffer> m_brick_starts_buffer = nullptr;
    const size_t m_cache_capacity = 96000000ul;    // this many 2x2x2 base elements fit into the cache. Each element is 2x2x2 x sizeof(uint)=32 bytes large, so a capacity of 32000000 equals 1024MB
    const size_t m_free_stack_capacity = 262144ul;  // this many elements (one uint=4byte each) fit into the free stack of EACH LoD > 0. We need max. volume_size/brick_size/lod_width³ elements. a capacity of 262144 equals 1MB * (lod_count-1)
    std::shared_ptr<Buffer> m_cache_info_buffer = nullptr;
    std::shared_ptr<Buffer> m_cache_buffer = nullptr;       // cache_capacity * 2x2x2 uints
    std::shared_ptr<Buffer> m_free_stack_buffer = nullptr;  // (lod_count - 1) * free_stack_capacity uints followed by (lod_count - 1) stack counters [free_stack_top[1], ..., fst[N-1])
    std::shared_ptr<Buffer> m_assign_info_buffer = nullptr; // (lod_count - 1) * 3 * uint assign infos for the LoDs + 1 * uint atomic top-index for the cache buffer

    // detail management
    const uint32_t m_max_detail_requests_per_frame = 64u; // how many brick_ids can be requested for detail upload per frame (affects the request buffer size)
    std::shared_ptr<Buffer> m_detail_requests_buffer = nullptr;
    std::vector<uint32_t> m_last_requested_brick_ids = {};
    bool m_detail_update_required = false;
    std::vector<uint32_t> m_constructed_detail_starts = {};
    std::shared_ptr<Buffer> m_detail_starts_buffer = nullptr;
    std::pair<std::shared_ptr<vvv::Awaitable>, std::shared_ptr<Buffer>> m_detail_starts_staging = {nullptr, nullptr};
    const size_t m_max_detail_byte_size = ((8ul << 10) << 10); // first number = MB
    uint32_t m_detail_capacity = 0u; // how many uints fit into the GPU detail buffer
    std::vector<uint32_t> m_constructed_detail = {};
    std::shared_ptr<Buffer> m_detail_buffer = nullptr;
    std::pair<std::shared_ptr<vvv::Awaitable>, std::shared_ptr<Buffer>> m_detail_staging = {nullptr, nullptr};
    size_t m_camHash_at_last_cache_reset = 0u;

    // debugging
    struct GPUStats {
        uint32_t gpu_blocks_decoded[6];
        uint32_t gpu_blocks_in_cache[6];
        uint32_t gpu_cache_size;
        uint32_t gpu_raymarch_samples;
        uint32_t gpu_bbox_hits;
    } m_last_gpu_stats = {};
    std::shared_ptr<Buffer> m_gpu_stats_buffer = nullptr;

    bool m_release_version = false;     // set to true if this is used in a renderer to release. Some parameters are hidden / set to default values in that case.

    size_t m_camHash;       // todo: make multibuffered
    uint32_t m_framesSinceCameraMove;
    uint32_t m_frame;
    std::optional<RendererOutput> m_mostRecentFrame = {};
};

} // namespace vvv