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
#include <utility>

#include "vvv/core/Renderer.hpp"
#include "vvv/core/Shader.hpp"
#include "vvv/passes/PassCompute.hpp"

using namespace vvv;

namespace volcanite {

struct PassCompSegVolRenderCfg {
    std::vector<std::string> shader_defines = {};
    bool parallel_decode = false;
    bool enable_cache_stages = true;
    bool try_enable_gpu_timing = true;                  ///< enables GPU timing query support if possible. Check success with isGPUTimingAvailable().
    vk::ImageUsageFlags output_image_usage = {};
    const std::string &label = "PassCompSegVolRender";
};

class PassCompSegVolRender : public PassCompute {

  public:
    enum CSGVRenderStage {
        CACHECLEAR = 0,
        REQUEST = 1,
        PROVISION = 2,
        ASSIGN = 3,
        DECOMPRESS = 4,
        RENDERING = 5,
        RESOLVE = 6,
        RENDERING_DUMMY = 7
    };

    enum TimingStageMS {
        CACHE_MANAGE_MS = 0,
        DECOMPRESS_MS = 1,
        RENDERING_MS = 2,
        POSTPROCESS_MS = 3,
    };

    PassCompSegVolRender(GpuContextPtr ctx, const std::shared_ptr<MultiBuffering> &multiBuffering, uint32_t queueFamilyIndex,
                         const PassCompSegVolRenderCfg &cfg)
        : WithMultiBuffering(multiBuffering),
          WithGpuContext(ctx), PassCompute(ctx, cfg.label, multiBuffering, queueFamilyIndex), m_shader_defines(cfg.shader_defines),
          m_parallel_decode(cfg.parallel_decode), m_enable_cache_stages(cfg.enable_cache_stages), m_enable_gpu_timing(cfg.try_enable_gpu_timing) {

        if (m_enable_gpu_timing)
            initializeGPUTimingQueries();
    }

    ~PassCompSegVolRender() override {
        if (m_timestamp_query_pool)
            VK_DEVICE_DESTROY(getCtx()->getDevice(), m_timestamp_query_pool);
    }

    AwaitableHandle execute(AwaitableList awaitBeforeExecution = {},
                            BinaryAwaitableList awaitBinaryAwaitableList = {},
                            vk::Semaphore *signalBinarySemaphore = nullptr) override;

    void setVolumeInfo(glm::uvec3 brick_count, uint32_t lod_count) {
        setGlobalInvocationSize(CACHECLEAR, brick_count.x, brick_count.y, brick_count.z);
        setGlobalInvocationSize(REQUEST, brick_count.x, brick_count.y, brick_count.z);
        setGlobalInvocationSize(PROVISION, lod_count - 1u, 1u, 1u);
        setGlobalInvocationSize(ASSIGN, brick_count.x, brick_count.y, brick_count.z);
        if (m_parallel_decode) {
            const uint32_t subgroup_size = getCtx()->getPhysicalDeviceSubgroupProperties().subgroupSize;
            setGlobalInvocationSize(DECOMPRESS, brick_count.x * brick_count.y * brick_count.z * subgroup_size, 1u, 1u);
        } else {
            setGlobalInvocationSize(DECOMPRESS, brick_count.x, brick_count.y, brick_count.z);
        }
    }
    void setImageInfo(const uint32_t width, const uint32_t height) {
        setGlobalInvocationSize(RENDERING, width, height, 1u);
        setGlobalInvocationSize(RESOLVE, width, height, 1u);
        setGlobalInvocationSize(RENDERING_DUMMY, width, height, 1u);
    }

    void setRenderUpdateFlagsForNextCall(uint32_t param_update_flags) { m_render_update_flags = param_update_flags; }
    [[nodiscard]] uint32_t getRenderUpdateFlagsForNextCall() const { return m_render_update_flags; }
    void setResolvePasses(int passes) { m_atrous_iterations = static_cast<uint32_t>(passes); }

    void setCacheStagesEnabled(bool enable) { m_enable_cache_stages = enable; }
    [[nodiscard]] bool getCacheStagesEnabled() const { return m_enable_cache_stages; }

    // GPU stage timing queries --------------
    [[nodiscard]] bool isFrameTimeTrackingAvailable() const { return m_enable_gpu_timing; }

    /// Resets the GPU frame time tracking results and starts timing the following frames until stopFrameTimeTracking() is called.
    void startFrameTimeTracking();

    /// Returns either a vector to the last tracking results or an empty vector if no tracking was previously carried out.
    /// Starting a new frame time tracking invalidates the reference.
    /// Will throw an exception if frame time tracking is currently active.
    [[nodiscard]] const std::vector<glm::vec4> &getLastFrameTimeTrackingResults() const;

    /// Waits for awaitLastFrameFinished and measures one last set of GPU stage timings.
    /// @returns the GPU stage (TimingStageMS) timings in [ms] for each frame between startFrameTimeTracking() and the last frame (execute) submitted for rendering.
    const std::vector<glm::vec4> &stopFrameTimeTracking(const std::optional<AwaitableList> &awaitLastFrameFinished);

  protected:
    struct PushConstants {
        uint32_t denoising_iteration; // denoising iteration variable for ping pong svgf-buffer
        uint32_t last_denoising_iteration;
    };

    std::vector<std::shared_ptr<Shader>> createShaders() override;
    std::vector<vk::PushConstantRange> definePushConstantRanges() override;

    void setGlobalInvocationSize(CSGVRenderStage shader_index, uint32_t width, uint32_t height, uint32_t depth) {
        assert(shader_index < m_shaders.size());
        m_work_group_sizes[shader_index] = getDispatchSize(width, height, depth, m_shaders[shader_index]->reflectWorkgroupSize());
    }
    void executeCommands(vk::CommandBuffer commandBuffer, CSGVRenderStage stage);

    // GPU stage timing queries --------------
    /// Tries to setup GPU timing queries. If this fails, m_enable_gpu_timing is set to false.
    /// @returns if the timing query setup was successful
    bool initializeGPUTimingQueries();

    void startTimingQuery(const vk::CommandBuffer &command_buffer, TimingStageMS stage);
    void stopTimingQuery(const vk::CommandBuffer &command_buffer, TimingStageMS stage) const;

    /// Queries the GPU stage timings of the last executed frame and appends them to the m_frame_gpu_timings vector.
    /// Must be called after the last submitted frame finished all GPU work but before submitting the next frame.
    void readNextGPUTimings();

    /// work group sizes per stage
    vk::Extent3D m_work_group_sizes[8] = {{0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}};
    uint32_t m_render_update_flags = 0u;                ///< among others: if the GPU cache reset should be triggered on the next call
    uint32_t m_atrous_iterations = 1u;
    const std::vector<std::string> m_shader_defines;    ///< defines that are passed on to shader compilation
    bool m_parallel_decode = false;                     ///< if decompression is parallelized within one brick
    bool m_enable_cache_stages = true;                  ///< if the cache provision, assign, and decompress stages are executed. only required when caching full bricks.

    // GPU timing measurements
    bool m_enable_gpu_timing = false;
    bool m_timing_active = false;
    uint32_t m_timing_active_stage_bits = 0u;           ///< bit (1 << TimingStageMS) == 1 <=> a time stamp query for this stage was submitted in the previous frame
    uint32_t m_timing_frame = 0u;
    static constexpr uint32_t GPU_TIMINGS_COUNT = 8u;   ///< twice the number of durations computed per frame (one start and one end point).
    std::vector<glm::vec4> m_frame_gpu_timings = {};    ///< per frame GPU durations [ms] of TimingStageMS stages: cache management, brick decoding, rendering, post-processing
    vk::QueryPool m_timestamp_query_pool = nullptr;
    float m_timestamp_period = 0.f;
};

} // namespace volcanite

