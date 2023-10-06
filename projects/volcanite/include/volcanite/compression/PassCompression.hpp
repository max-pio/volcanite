#pragma once

#include <memory>
#include <string>
#include <utility>

#include "vvv/passes/PassCompute.hpp"
#include "vvv/util/Logger.hpp"

// =============================================
// Starting point of a GPU based implementation
// for the RLE compression algorithm from
// Henriette Färber and Max. Currently, I'm not
// working on this but on other compression
// methods instead. (see VolumeCompressionBase)
// =============================================



namespace vvv {

typedef glm::ivec4 CompressionVerifyErrors;

class PassCompression : public PassCompute {

public:
    PassCompression(GpuContextPtr ctx, std::string label, const std::shared_ptr<MultiBuffering>& multiBuffering = NoMultiBuffering, uint32_t queueFamilyIndex = 0)
        :   WithGpuContext(ctx), WithMultiBuffering(multiBuffering), PassCompute(ctx, std::move(label), multiBuffering, queueFamilyIndex), m_path(""), m_brick_size(8),
          m_volume_frames(0), m_volume_dim(0, 0, 0) { }

    void init(std::string path, int brick_size = 8);

    AwaitableHandle execute(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr) override;
    AwaitableHandle verify(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr);

    void freeResources() override {
        m_gpu.index_buffer = nullptr;
        m_gpu.sequence_buffer = nullptr;
        m_gpu.vol_img = nullptr;
        m_gpu.parameters = nullptr;
        m_gpu.order_buffer = nullptr;
        m_gpu.verify_buffer = nullptr;
        PassCompute::freeResources(); // contains shader cleanup
    }

protected:
    // shader stages / pipeline indices of all compression steps
    static constexpr uint32_t SEQUENCE_GEN = 0, MAX_POSTFIX = 1, REPRESENTATIVE = 2, POSTFIX_ELIMINATION = 3, VERIFY = 4;

    /**
     * fills the commandbuffer with the dispatch for the given compression pipeline stage
     * @param pipeline_index step in the pipeline
     * @param domain dispatch workgroup count
     */
    void executeCommands(vk::CommandBuffer commandBuffer, uint32_t pipeline_index, vk::Extent3D domain) {
        commandBuffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_pipelines.at(pipeline_index)); // each compute shader has one pipeline
        if (hasDescriptors()) {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_pipelineLayout, 0, m_descriptorSets->at(0), nullptr);
        }
        vk::Extent3D workgroupCount = getDispatchSize(domain.width, domain.height, domain.depth, m_shaders.at(pipeline_index)->reflectWorkgroupSize());
        commandBuffer.dispatch(workgroupCount.width, workgroupCount.height, workgroupCount.depth);
        commandBuffer.end();
    }

    std::vector<std::shared_ptr<Shader>> createShaders() override {
        std::shared_ptr<Shader> sequence_gen = std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "volcanite/compression/sequence_generation.comp", .label = "compr_sequence_generation"});
        std::shared_ptr<Shader> max_postfix = std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "volcanite/compression/max_postfix.comp", .label = "compr_max_postfix"});
        std::shared_ptr<Shader> representative = std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "volcanite/compression/representative.comp", .label = "compr_representative"});
        std::shared_ptr<Shader> postfix_elimination = std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "volcanite/compression/postfix_elimination.comp", .label = "compr_postfix_elemination"});
        std::shared_ptr<Shader> verify = std::make_shared<Shader>(SimpleGlslShaderRequest{.filename = "volcanite/compression/verify_compressed.comp", .label = "verify_compressed"});

        return {sequence_gen, max_postfix, representative, postfix_elimination, verify};
    }

private:
    /**
     * Generates a buffer that local voxel coordinates within a brick in access order
     */
    void generate_brick_order();
    std::vector<glm::ivec4> m_brick_order;

    void squeeze_sequence_buffer(size_t& top_of_sequence_buffer, const int frame, const size_t bricks_per_frame);

    // data set information
    int m_volume_frames;
    glm::uvec3 m_volume_dim;
    std::string m_path;

    // compression results
    int m_brick_size;
    glm::uvec3 m_brick_dim;                     ///< bricks in each dimension for one frame / volume
    std::vector<int32_t> m_sequence_buffer;     ///< complete sequence buffer
    std::vector<glm::ivec2> m_index_buffer;     ///< complete index buffer
    struct {
        size_t sequence_buffer_size = 0;                          ///< gpu size of the sequence buffer, should fit one frame
        std::shared_ptr<Buffer> sequence_buffer = nullptr;        ///< gpu sequence buffer (only a subset of the total buffer)
        size_t index_buffer_size = 0;                             ///< gpu size of the index buffer, should fit all indices into gpu memory
        std::shared_ptr<Buffer> index_buffer = nullptr;           ///< gpu index buffer (complete)
        std::shared_ptr<UniformReflected> parameters = nullptr;   ///< uniform parameters (same for all shaders, may contain irrelevant parameters for some)
        std::shared_ptr<Texture> vol_img;                         ///< volume image
        std::shared_ptr<Buffer> order_buffer = nullptr;           ///< contains order in which brick voxels are accessed
        std::shared_ptr<Buffer> verify_buffer = nullptr;           ///< contains the results of the verification shader
    } m_gpu;
};

} // namespace vvv
