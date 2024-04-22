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
#include "volcanite/compression/CSGVDatabase.hpp"

namespace vvv {

class CompressedSegmentationVolumeRenderer : public Renderer, public WithGpuContext {

public:
    CompressedSegmentationVolumeRenderer(bool release_version = false) : WithGpuContext(nullptr), m_compressed_segmentation_volume(nullptr), m_data_changed(false),
                                                                         m_camHash(0ul), m_resolution(1920,1080), m_framesSinceCameraMove(0), m_frame(0u),
                                                                         m_release_version(release_version) {
        // initialize the shading materials with something reasonable
        for(int m = 0; m < SEGMENTED_VOLUME_MATERIAL_COUNT; m++) {
            auto &mat = m_materials[m];
            mat.discrAttribute = (m == 0) ? 0 : SegmentedVolumeMaterial::DISCR_NONE;
            mat.discrInterval = glm::vec2(1.f, 1000000.f);
            mat.tfAttribute = 0u;
            mat.tfMinMax = glm::vec2(0.f, 1000000.f);
            // we use opaque transfer functions
            mat.tf->m_controlPointsOpacity.resize(4);
            mat.tf->m_controlPointsOpacity[0] = 0.f;
            mat.tf->m_controlPointsOpacity[1] = 1.f;
            mat.tf->m_controlPointsOpacity[2] = 1.f;
            mat.tf->m_controlPointsOpacity[3] = 1.f;
        }
    }

    ~CompressedSegmentationVolumeRenderer() { resetGPU(); m_compressed_segmentation_volume.reset(); }

    RendererOutput renderNextFrame(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr) override;

    void configureExtensionsAndLayersAndFeatures(GpuContextRwPtr ctx) override {
        ctx->enableDeviceExtension("VK_EXT_memory_budget");
        ctx->physicalDeviceFeaturesV12().setBufferDeviceAddress(true);
    }

    /** Initializes Descriptorsets and calls pipeline initialization. */
    void initResources(GpuContext *ctx) override;
    void releaseResources() override;
    /** Initialize everything that depends on shader */
    void initShaderResources() override;
    void releaseShaderResources() override;
    /** Initializes command buffer, renderpass, images and framebuffers */
    void initSwapchainResources() override;
    void releaseSwapchain() override;

    /** Releases all GPU states and resources but does not reset the segmentation volume. */
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

    /** We limit the render resolution to max. 4K (4096x2160) or Full-HD. */
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
    void releaseGui() override {
        if(!m_gui_interface)
            return;

        // save rendering parameters on GUI shutdown if requested
        if(!m_save_config_on_shutdown_path.empty()) {
            std::ofstream out(m_save_config_on_shutdown_path);
            if(out.is_open()) {
                if (!writeParameters(out, VOLCANITE_VERSION))
                    Logger(WARN) << "Could not export parameters to " << m_save_config_on_shutdown_path;
                out.close();
            }
        }

        m_gui_interface = nullptr;
    }

    void setCompressedSegmentationVolume(std::shared_ptr<CompressedSegmentationVolume> csgv, std::shared_ptr<CSGVDatabase> db) {
        if(!csgv)
            throw std::runtime_error("CompressedSegmentationVolume must not be null");
        if(!db)
            throw std::runtime_error("CompressedSegmentationVolume database must not be null");


        if(csgv->getBrickCount().x < csgv->getLodCountPerBrick()) {
            Logger(WARN) << "CompressedSegmentationVolume has fewer bricks (" << csgv->getBrickCount().x <<
                         ") in one dimension than there are brick level-of-details (" << csgv->getLodCountPerBrick() <<
                         "). This may break some shaders. Advice: Re-Compress with a smaller brick-size.";
        }
        m_compressed_segmentation_volume = std::move(csgv);
        m_data_changed = true;

        // when a database is provided, we use it for attribute visualization
        m_csgv_db = std::move(db);
        m_attribute_start_position.resize(m_csgv_db->getAttributeCount(), -1);
        // update transfer function limits
        for(int m = 0; m < SEGMENTED_VOLUME_MATERIAL_COUNT; m++) {
            if(m_materials[m].discrAttribute >= 0) {
                m_materials[m].discrInterval = m_csgv_db->getAttributeMinMax().at(m_materials[m].discrAttribute);
                // most
                if(m_materials[m].discrInterval.y > m_materials[m].discrInterval.x)
                    m_materials[m].discrInterval.x++;
            }
            m_materials[m].tfMinMax = m_csgv_db->getAttributeMinMax().at(m_materials[m].tfAttribute);
        }
    }

    const std::optional<RendererOutput> &mostRecentFrame() { return m_mostRecentFrame; }

    int getTargetAccumulationFrames() { return m_accum_frames; }
    /** Will save the renderer state to the path when the renderer is shut down */
    void saveConfigOnShutdown(std::string path) { m_save_config_on_shutdown_path = path; }
    void setCacheSizeMB(size_t mb) { m_cache_capacity = (mb * 1024 * 1024) / (2*2*2 * sizeof(uint32_t)); }

private:
    // (gui) parameters:
    // materials
    static constexpr uint32_t SEGMENTED_VOLUME_MATERIAL_COUNT = 8;
    std::vector<SegmentedVolumeMaterial> m_materials = std::vector<SegmentedVolumeMaterial>(SEGMENTED_VOLUME_MATERIAL_COUNT);
    float m_factor_ambient = 0.4f;
    float m_ratio_spec_diff = 1.0f;
    bool m_cook_torrance_shading = true;
    // shading and post processing
    glm::vec4 m_background_color_a = glm::vec4(0.9f, 0.9f, 0.95f, 1.f);
    glm::vec4 m_background_color_b = glm::vec4(1.f, 1.f, 1.f, 1.f);
    int m_subsampling = 1;
    bool m_tonemap_enabled = false;
    bool m_global_illumination_enabled = false;
    bool m_envmap_enabled = false;
    float m_shadow_pathtracing_ratio = 1.0f;
    glm::vec2 m_ambient_occlusion_dist_strength = glm::vec2(15.f, 0.5f);
    glm::vec3 m_light_direction = glm::vec3(-1.f, 1.f, 0.1f);
    float m_light_intensity = 1.f;
    // voxel traversal
    int m_max_path_length = 32;
    int m_max_steps = 2048;
    glm::vec3 m_voxel_size = glm::vec3(1.f, 1.f, 1.f);
    bool m_subblock_enabled = false;
    glm::ivec3 m_subblock_size = glm::ivec3(128, 128, 128);
    glm::ivec3 m_subblock_start = glm::ivec3(0, 0, 0);
    glm::vec3 m_bboxMin = glm::vec3(0.f, 0.f, 0.f);
    glm::vec3 m_bboxMax = glm::vec3(1.f, 1.f, 1.f);
    // debugging and dev options
    float m_lod_bias = 0.f;
    bool m_show_envmap = false;
    bool m_show_normals = false;
    bool m_blue_noise = true;
    bool m_show_model_space = false;
    bool m_show_brick_cache = false;
    bool m_show_lod = false;
    bool m_show_step_count = false;
    bool m_clear_cache_every_frame = false;
    bool m_clear_accum_every_frame = false;
    int m_accum_frames = 16;
    int m_max_decoding_lod = 6;
    // utility
    std::string m_gui_resolution_text;
    std::string m_gui_device_mem_text;
    std::optional<std::string> m_download_frame_to_image_file = {};
    std::string m_save_config_on_shutdown_path = {};

    void updateDeviceMemoryUsage();
    void updateSegmentedVolumeMaterial(int m);
    vvv::AwaitableList updateAttributeBuffers();
    void updateUniformDescriptorset();

    std::unique_ptr<PassCompSegVolRender> m_pass = nullptr;
    std::shared_ptr<Texture> m_feedback_tex[2] = {nullptr, nullptr};
    std::shared_ptr<Texture> m_outDepth = nullptr;
    std::shared_ptr<Texture> m_outColor = nullptr;
    std::shared_ptr<vvv::MultiBufferedResource<std::shared_ptr<Texture>>> m_inpaintedOutColor = nullptr; // this is the output texture and thus the only resource that we have to duplicate for each swapchain image
    std::shared_ptr<UniformReflected> m_urender_info = nullptr;
    std::shared_ptr<UniformReflected> m_usegmented_volume_info = nullptr;

    std::shared_ptr<CompressedSegmentationVolume> m_compressed_segmentation_volume = nullptr;
    std::shared_ptr<CSGVDatabase> m_csgv_db = nullptr;
    std::vector<bool> m_gpu_material_changed = std::vector<bool>(SEGMENTED_VOLUME_MATERIAL_COUNT, true);
    std::vector<GPUSegmentedVolumeMaterial> m_gpu_materials{SEGMENTED_VOLUME_MATERIAL_COUNT};

    std::vector<std::shared_ptr<TransferFunction1D>> m_materialTransferFunctions{SEGMENTED_VOLUME_MATERIAL_COUNT, nullptr};
    bool m_data_changed = false;
    std::shared_ptr<Buffer> m_encoding_buffer = nullptr;
    glm::uvec2 m_encoding_buffer_address = {};
    const size_t m_max_attribute_buffer_size = ((64ul << 10) << 10);   // MB to store different floating point attributes back to back
    std::vector<int> m_attribute_start_position = {-1};           // the start index in the attribute_buffer for each attribute
    std::shared_ptr<Buffer> m_attribute_buffer = nullptr;       // stores attributes back to back
    std::shared_ptr<Buffer> m_materials_buffer = nullptr;       // stores the material information
    std::shared_ptr<Buffer> m_brick_starts_buffer = nullptr;
    size_t m_cache_capacity = 33554432ul;    // this many 2x2x2 base elements fit into the cache. Each element is 2x2x2 x sizeof(uint)=32 bytes large, so a capacity of 32000000 equals 1024MB
    const size_t m_free_stack_capacity = 262144ul;  // this many elements (one uint=4byte each) fit into the free stack of EACH LoD > 0. We need max. volume_size/brick_size/lod_width³ elements. a capacity of 262144 equals 1MB * (lod_count-1)
    std::shared_ptr<Buffer> m_cache_info_buffer = nullptr;
    std::shared_ptr<Buffer> m_cache_buffer = nullptr;       // cache_capacity * 2x2x2 uints
    std::shared_ptr<Buffer> m_free_stack_buffer = nullptr;  // (lod_count - 1) * free_stack_capacity uints followed by (lod_count - 1) stack counters [free_stack_top[1], ..., fst[N-1])
    std::shared_ptr<Buffer> m_assign_info_buffer = nullptr; // (lod_count - 1) * 3 * uint assign infos for the LoDs + 1 * uint atomic top-index for the cache buffer

    // detail management
    const uint32_t m_max_detail_requests_per_frame = 512u; // how many brick_ids can be requested for detail upload per frame (affects the request buffer size)
    std::shared_ptr<Buffer> m_detail_requests_buffer = nullptr;
    std::vector<uint32_t> m_last_requested_brick_ids = {};
    bool m_detail_update_required = false;
    std::vector<uint32_t> m_constructed_detail_starts = {};
    std::shared_ptr<Buffer> m_detail_starts_buffer = nullptr;
    std::pair<std::shared_ptr<vvv::Awaitable>, std::shared_ptr<Buffer>> m_detail_starts_staging = {nullptr, nullptr};
    const size_t m_max_detail_byte_size = ((512ul << 10) << 10); // first number = MB
    uint32_t m_detail_capacity = 0u; // how many uints fit into the GPU detail buffer
    std::vector<uint32_t> m_constructed_detail = {};
    std::shared_ptr<Buffer> m_detail_buffer = nullptr;
    glm::uvec2 m_detail_buffer_address = {};
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
