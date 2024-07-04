#pragma once

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

namespace vvv {


class PassCompSegVolRender : public PassCompute {

public:
    enum CSGVRenderStage {
        CACHECLEAR = 0,
        REQUEST = 1,
        PROVISION = 2,
        ASSIGN = 3,
        RENDERING = 4,
        INPAINTING = 5
    };

    PassCompSegVolRender(GpuContextPtr ctx, const std::shared_ptr<MultiBuffering>& multiBuffering, std::vector<std::string> shaderDefines = {}, vk::ImageUsageFlags outputImageUsage = {},
                            const std::string& label = "PassCompSegVolRender")
        : PassCompute(ctx, label, multiBuffering, ctx->getQueueFamilyIndices().graphics.value()), WithMultiBuffering(multiBuffering), WithGpuContext(ctx), m_shader_defines(shaderDefines) {}

    AwaitableHandle execute(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr) override;


    void setVolumeInfo(glm::uvec3 brick_count, uint32_t lod_count) {
        setGlobalInvocationSize(CACHECLEAR, brick_count.x, brick_count.y, brick_count.z);
        setGlobalInvocationSize(REQUEST, brick_count.x, brick_count.y, brick_count.z);
        setGlobalInvocationSize(PROVISION, lod_count-1u, 1u, 1u);
        setGlobalInvocationSize(ASSIGN, brick_count.x, brick_count.y, brick_count.z);
    }
    void setImageInfo(uint32_t width, uint32_t height) {
        setGlobalInvocationSize(RENDERING, width, height, 1u);
        setGlobalInvocationSize(INPAINTING, width, height, 1u);
    }

    void resetCacheOnNextCall() { m_reset_cache = true; }
    bool willCacheBeResetOnNextCall() { return m_reset_cache; }

protected:
    struct PushConstants {
        uint32_t denoising_iteration;
    };

    std::vector<std::shared_ptr<Shader>> createShaders() override;
    std::vector<vk::PushConstantRange> definePushConstantRanges() override;

    void setGlobalInvocationSize(CSGVRenderStage shader_index, uint32_t width, uint32_t height, uint32_t depth) {
        assert(shader_index < m_shaders.size());
        m_workgroupCount[shader_index] = getDispatchSize(width, height, depth, m_shaders[shader_index]->reflectWorkgroupSize());
    }
    void executeCommands(vk::CommandBuffer commandBuffer, CSGVRenderStage stage);

    vk::Extent3D m_workgroupCount[6] = {{0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}}; // work group sizes per shader index
    bool m_reset_cache = false;
    const std::vector<std::string> m_shader_defines;
};

} // namespace vvv