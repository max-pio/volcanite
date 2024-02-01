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

#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/renderer/PassCompSegVolRender.hpp"

namespace vvv {

class CompressedSegmentationVolumeRenderer : public Renderer, public WithGpuContext {

public:
    CompressedSegmentationVolumeRenderer(bool release_version = false) : WithGpuContext(nullptr), m_compressed_segmentation_volume(nullptr), m_data_changed(false),
                                                                         m_camHash(0ul), m_resolution(1920,1080), m_framesSinceCameraMove(0), m_frame(0u),
                                                                         m_release_version(release_version) {}

    ~CompressedSegmentationVolumeRenderer() { resetGPU(); m_compressed_segmentation_volume.reset(); }

    RendererOutput renderNextFrame(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr) override;

    void configureExtensionsAndLayersAndFeatures(GpuContextRwPtr ctx) override {
        ctx->enableDeviceExtension("VK_EXT_memory_budget");
    }

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
     * Releases all GPU states and resources but does not reset the segmentation volume.
     */
    void resetGPU();

    void setRenderResolution(vk::Extent2D resolution) {
        m_resolution = resolution;

        // trigger a "swapchain" recreation
        if(getCtx()) {
            getCtx()->getDevice().waitIdle();
            releaseSwapchain();
            initSwapchainResources();
        }
    }

    vk::Extent2D getRenderResolution() const {
        return m_resolution;
    }

    /**
    * We limit the render resolution to max. 4K (4096x2160) or Full-HD.
    */
    void updateRenderResolutionFromWSI() {
        // ToDo: remove hardcoded render resolution. Move the WSI dependency to Application / HeadlessRendering or the Renderer class?
        const vk::Extent2D max_resolution = {4096u, 2160u};

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
            m_resolution = screen;
        }
    }

    void initGui(vvv::GuiInterface * gui) override;

    void setCompressedSegmentationVolume(std::shared_ptr<CompressedSegmentationVolume> csgv) {
        if(csgv->getBrickCount().x < csgv->getLodCountPerBrick()) {
            Logger(WARN) << "CompressedSegmentationVolume has fewer bricks (" << csgv->getBrickCount().x <<
                         ") in one dimension than there are brick level-of-details (" << csgv->getLodCountPerBrick() <<
                         "). This may break some shaders. Advice: Re-Compress with a smaller brick-size.";
        }
        m_compressed_segmentation_volume = std::move(csgv);
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
    bool m_subblock_enabled = false;
    glm::ivec3 m_subblock_size = glm::ivec3(128, 128, 128);
    glm::ivec3 m_subblock_start = glm::ivec3(0, 0, 0);
    glm::vec3 m_bboxMin = glm::vec3(0.f, 0.f, 0.f);
    glm::vec3 m_bboxMax = glm::vec3(1.f, 1.f, 1.f);
    float m_lod_bias = 0.f;
    bool m_dda_traversal = true;
    bool m_lambert_shading = false;
    float m_factor_ambient = 0.1f;
    float m_ratio_spec_diff = 0.8f;
    bool m_cook_torrance_shading = false;
    bool m_show_normals = false;
    bool m_blue_noise = true;
    bool m_show_model_space = false;
    bool m_show_brick_cache = false;
    bool m_show_lod = false;
    bool m_show_step_count = false;
    bool m_clear_cache_every_frame = false;
    bool m_clear_accum_every_frame = false;
    int m_max_decoding_lod = 6;
    int m_empty_label = 0;
    std::string m_gui_resolution_text;
    std::string m_gui_device_mem_text;
    std::optional<std::string> m_download_frame_to_image_file = {};


    void updateDeviceMemoryUsage();

    void updateUniformDescriptorset();

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

    vk::Extent2D m_resolution;
    size_t m_camHash;       // todo: make multibuffered
    uint32_t m_framesSinceCameraMove;
    uint32_t m_frame;
    std::optional<RendererOutput> m_mostRecentFrame = {};
};

} // namespace vvv