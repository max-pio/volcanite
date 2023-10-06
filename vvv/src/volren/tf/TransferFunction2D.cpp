#include <vvv/volren/tf/TransferFunction2D.hpp>

#include "vvv/volren/tf/VectorTransferFunction.hpp"
#include <vvv/volren/tf/builtin.hpp>
#include <vvv/passes/PassCompute.hpp>
#include <vvv/util/Logger.hpp>

namespace vvv {


TransferFunction2D::TransferFunction2D(GpuContextPtr ctx, const std::shared_ptr<MultiBuffering> &multiBuffering, uint32_t resolution, uint32_t queue)
    : TransferFunction(ctx), m_resolution(resolution) {
    AwaitableList awaitList;

    m_computePass = std::make_unique<SinglePassCompute>(SinglePassComputeSettings{.ctx = ctx, .label = "tf2d", .multiBuffering = multiBuffering, .queueFamilyIndex = queue, .workgroupCount = {resolution, resolution, 1}},
                                                        SimpleGlslShaderRequest{.filename = "create_transfer_function_2d.comp", .label = "tf2d.shader"});
    m_computePass->allocateResources();
    m_optionsUniform = m_computePass->getUniformSet("options");

    // prepare color map
    VectorTransferFunction jet(colormaps::jet, {0, 1, 1, 1});
    m_colormapTF = jet.rasterize(ctx, resolution);
    auto [colormapAwait, colormapStaging] = m_colormapTF->upload();
    awaitList.push_back(colormapAwait);

    // prepare output texture
    TextureReflectionOptions opts = {.width = m_resolution, .height = m_resolution,
                                     .format = vk::Format::eR16G16B16A16Unorm,
                                     .usage = vk::ImageUsageFlagBits::eSampled};

    m_texture = m_computePass->reflectTexture("IMAGE_transferFunction2D", opts);
    m_texture->setName("tf2d.2d_texture");
    m_texture->initResources();
    m_computePass->setStorageImage("IMAGE_transferFunction2D", *m_texture, vk::ImageLayout::eGeneral, false);

    auto texSetImgLayoutAwait = m_texture->setImageLayout(vk::ImageLayout::eGeneral);
    awaitList.push_back(texSetImgLayoutAwait);

    // prepare storage buffers
    m_polygonStorageBuffer = std::make_unique<Buffer>(getCtx(), BufferSettings{ .label = "tf2d.polygon_ssbo", .byteSize = polygonStorageBufferCapacity * sizeof(glm::vec2), .usage = vk::BufferUsageFlagBits::eStorageBuffer });
    m_computePass->setStorageBuffer(0, 3, *m_polygonStorageBuffer);
    m_additionalDataStorageBuffer = std::make_unique<Buffer>(getCtx(), BufferSettings{ .label = "tf2d.additional_data_ssbo", .byteSize = additionalDataStorageBufferCapacity * sizeof(glm::vec4), .usage = vk::BufferUsageFlagBits::eStorageBuffer });
    m_computePass->setStorageBuffer(0, 4, *m_additionalDataStorageBuffer);

    getCtx()->sync->hostWaitOnDevice(awaitList);
}

TransferFunction2D::~TransferFunction2D() {
    m_computePass->freeResources();
    m_computePass = nullptr;
}

std::vector<glm::vec2> TransferFunction2D::preparePolygonData() const {
    auto polygons = m_polygons;

    // if no polygons specified, fill whole texture with opacity = 1
    if (polygons.empty())
        polygons.push_back({{0,0}, {0,1}, {1,1}, {1,0}});

    // count data requirements: 1 for end, 1 end for each polygon, one for each point
    auto polygonsDataCount = 1 + polygons.size();
    for (auto& p : polygons) polygonsDataCount += p.size();

    // trim polygons if it does not fit in storageBuffer
    if (polygonsDataCount > polygonStorageBufferCapacity)
        Logger(WARN) << "Transfer Function 2D: polygon data does not fit in storage buffer, trimming polygons.";
    while(polygonsDataCount > polygonStorageBufferCapacity) {
        polygons.back().pop_back();
        polygonsDataCount--;
        if (polygons.back().size() < 3) {
            polygonsDataCount -= polygons.back().size() + 1;
            polygons.pop_back();
        }
    }

    std::vector<glm::vec2> polygonData;
    polygonData.reserve(polygonStorageBufferCapacity);
    for (auto& polygon : polygons) {
        for (glm::vec2& v : polygon)
            polygonData.push_back(v);
        polygonData.emplace_back(-1.0f, 0.0f);
    }
    polygonData.emplace_back(-2.0f, 0.0f);

    polygonData.resize(polygonStorageBufferCapacity, {0,0});

    return polygonData;
}

std::pair<vvv::AwaitableHandle, std::shared_ptr<vvv::Buffer>> TransferFunction2D::upload() {
    m_optionsUniform->setUniform("g_backgroundOpacity", 0.0f);
    m_optionsUniform->setUniform("g_foregroundOpacity", 1.0f);
    m_optionsUniform->setUniform("g_feathering", m_feathering);
    m_optionsUniform->setUniform("g_direction", static_cast<uint32_t>(m_direction));
    m_optionsUniform->upload(m_computePass->getActiveIndex());

    m_computePass->setImageSampler("SAMPLER_transferFunction1D", m_colormapTF->texture(), vk::ImageLayout::eShaderReadOnlyOptimal);

    m_polygonStorageBuffer->upload(preparePolygonData());

    std::vector<glm::vec4> additionalData(additionalDataStorageBufferCapacity, glm::vec4{0});
    for(int i = 0; i < m_polygons.size(); i++)
        additionalData[i] = m_polygonHasCustomColor[i] ? glm::vec4{m_polygonCustomColor[i], m_polygonOpacity[i]} : glm::vec4{-1,-1,-1,m_polygonOpacity[i]};
    m_additionalDataStorageBuffer->upload(additionalData);

    auto computeAwait = m_computePass->execute();
    return {computeAwait, {}};
}

}
