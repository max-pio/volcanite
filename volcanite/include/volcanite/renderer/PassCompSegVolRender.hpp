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

    PassCompSegVolRender(GpuContextPtr ctx, const std::shared_ptr<MultiBuffering> &multiBuffering, uint32_t queueFamilyIndex,
                         std::vector<std::string> shaderDefines = {}, bool parallel_decode = false, bool enable_cache_stages = true,
                         vk::ImageUsageFlags outputImageUsage = {}, const std::string &label = "PassCompSegVolRender")
        : PassCompute(ctx, label, multiBuffering, queueFamilyIndex),
          WithMultiBuffering(multiBuffering), WithGpuContext(ctx), m_shader_defines(std::move(shaderDefines)),
          m_parallel_decode(parallel_decode), m_enable_cache_stages(enable_cache_stages) {}

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

    /// work group sizes per stage
    vk::Extent3D m_work_group_sizes[8] = {{0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}};
    uint32_t m_render_update_flags = 0u; /// among others: if the GPU cache reset should be triggered on the next call
    uint32_t m_atrous_iterations = 1u;
    const std::vector<std::string> m_shader_defines; /// defines that are passed on to shader compilation
    bool m_parallel_decode = false;                  /// if decompression is parallelized within one brick
    bool m_enable_cache_stages = true;               /// if the cache provision, assign, and decompress stages are executed. only required when caching full bricks.
};

} // namespace volcanite
