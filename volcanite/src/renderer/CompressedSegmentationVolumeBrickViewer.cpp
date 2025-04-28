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

#include "volcanite/renderer/CompressedSegmentationVolumeBrickViewer.hpp"

#include "glm/gtc/matrix_transform.hpp"

#include "stb/stb_image.hpp"
#include "vvv/util/Paths.hpp"

using namespace vvv;

namespace volcanite {

RendererOutput CompressedSegmentationVolumeBrickViewer::renderNextFrame(AwaitableList awaitBeforeExecution, BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) {
    assert(m_usegmented_volume_info && m_urender_info && m_compressed_segmentation_volume && "CompressedSegmentationVolumeBrickViewer data missing!");

    // GUI allows setting arbitrary brick indices -> clamp
    m_brick_id = glm::clamp(m_brick_id, {0, 0, 0},
                            glm::ivec3(m_compressed_segmentation_volume->getBrickCount()) - glm::ivec3(1, 1, 1));

    if (m_data_changed) {
        // wait until all previous frames are processed
        getCtx()->getDevice().waitIdle();

        assert(!m_compressed_segmentation_volume->getBrickStarts()->empty() && !m_compressed_segmentation_volume->getAllEncodings()->empty() && "CompressedSegmentationVolume not initialized!");
        if (m_compressed_segmentation_volume->getAllEncodings()->size() != 1)
            throw std::runtime_error("CompressedSegmentationVolume must not contain split encodings for Volume Brick Viewer.");
        m_encoding_buffer->upload(m_compressed_segmentation_volume->getAllEncodings()->at(0));
        m_brick_starts_buffer->upload(*(m_compressed_segmentation_volume->getBrickStarts()));

        // wait until everything is uploaded
        getCtx()->getDevice().waitIdle();
        m_data_changed = false;
    }
    // decompress all LODs of the given brick and add
    if (!m_compressed_segmentation_volume->getAllEncodings()->empty() && glm::any(glm::notEqual(m_current_decoded_brick, m_brick_id))) {
        uint32_t brick_size = m_compressed_segmentation_volume->getBrickSize();
        int lod_count = static_cast<int>(log2(brick_size) + 1);
        std::vector<uint32_t> tmp(2 * lod_count * brick_size * brick_size * brick_size, 0xFFFFFFFF);
        std::vector<glm::uvec4> tmp_palette;
#pragma omp parallel for default(none) shared(lod_count, tmp, brick_size, tmp_palette)
        for (int lod = 0; lod < lod_count; lod++) {
            std::vector<glm::uvec4> *pal_ptr = nullptr;
            if (lod == lod_count - 1)
                pal_ptr = &tmp_palette;
            m_compressed_segmentation_volume->decompressBrickTo(&tmp[lod * (brick_size * brick_size * brick_size)], m_brick_id, lod, &tmp[(lod_count + lod) * (brick_size * brick_size * brick_size)], pal_ptr);
        }
        assert(m_cache_buffer && tmp.size() <= m_cache_buffer->getByteSize() / sizeof(uint32_t));
        assert(m_palette_buffer && tmp_palette.size() <= m_palette_buffer->getByteSize() / sizeof(uint32_t));
        m_cache_buffer->upload(tmp);
        m_palette_buffer->upload(tmp_palette);
        getCtx()->getDevice().waitIdle();
        m_current_decoded_brick = m_brick_id;
    }

    // upload uniforms
    if (m_urender_info && m_usegmented_volume_info) {
        updateUniformDescriptorset();
        m_urender_info->upload(m_pass->getActiveIndex());
        m_usegmented_volume_info->upload(m_pass->getActiveIndex());
    }

    m_pass->setStorageImage("outColor", *m_outColor->getActive());
    const auto renderingFinished = m_pass->execute(awaitBeforeExecution, awaitBinaryAwaitableList);

    return vvv::RendererOutput{
        .texture = m_outColor->getActive().get(),
        .renderingComplete = {renderingFinished},
    };
}

void CompressedSegmentationVolumeBrickViewer::initResources(GpuContext *ctx) {
    setCtx(ctx);
    // allocate GPU buffers for our data
    const constexpr size_t MAX_VOL_SIZE = (1000 * 1024 * 1024); // enough for our biggest data set 1000^3 in compressed form
    m_brick_starts_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeBrickViewer.m_brick_start_buffer", .byteSize = (MAX_VOL_SIZE / 4096) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    m_encoding_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeBrickViewer.m_encoding_buffer", .byteSize = (MAX_VOL_SIZE / 2) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    const constexpr size_t CACHE_SIZE_BYTE = (2 * 7 * 64 * 64 * 64 * sizeof(uint32_t)); // 8 LOD levels for 16*16*16 brick_size * 2 because after the brick values, we also store the encoding
    m_cache_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeBrickViewer.m_cache_buffer", .byteSize = CACHE_SIZE_BYTE, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    m_palette_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeBrickViewer.m_palette_buffer", .byteSize = sizeof(glm::uvec4) * 1024, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    m_enumbrickpos_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeBrickViewer.m_enumbrickpos_buffer", .byteSize = sizeof(glm::uvec4) * 32 * 32 * 32, .usage = vk::BufferUsageFlagBits::eStorageBuffer});
    m_enumbrickpos_buffer->upload(CompressedSegmentationVolume::createBrickPosBuffer(16));
}

void CompressedSegmentationVolumeBrickViewer::releaseResources() {
    m_cache_buffer = nullptr;
    m_palette_buffer = nullptr;
    m_encoding_buffer = nullptr;
    m_brick_starts_buffer = nullptr;
    m_enumbrickpos_buffer = nullptr;
}

void CompressedSegmentationVolumeBrickViewer::initShaderResources() {
    // compute pass for ray marching points
    ShaderCompileErrorCallback compileErrorCallback = [](const ShaderCompileError &err) {
        Logger(Error) << err.errorText;
        return ShaderCompileErrorCallbackAction::USE_PREVIOUS_CODE;
    };
    m_pass = std::make_unique<SinglePassCompute>(SinglePassComputeSettings{.ctx = getCtx(), .label = "CompressedSegmentationVolumeBrickViewer", .multiBuffering = getCtx()->getWsi()->stateInFlight()},
                                                 SimpleGlslShaderRequest{.filename = "volcanite/renderer/csgv_brick_viewer.comp"}, compileErrorCallback);
    m_pass->allocateResources();
    m_urender_info = m_pass->getUniformSet("render_info");
    m_usegmented_volume_info = m_pass->getUniformSet("segmented_volume_info");
    m_pass->setStorageBuffer(0, 3, *m_brick_starts_buffer);
    m_pass->setStorageBuffer(0, 4, *m_encoding_buffer);
    m_pass->setStorageBuffer(0, 5, *m_cache_buffer);
    m_pass->setStorageBuffer(0, 8, *m_palette_buffer);
    m_pass->setStorageBuffer(0, 6, *m_enumbrickpos_buffer);

    // upload encoding icon texture
    int img_width, img_height, img_channels;
    auto img_path = Paths::findDataPath("csgv_codes.png");
    Logger(Info) << img_path.string();
    unsigned char *image = stbi_load(img_path.string().c_str(), &img_width, &img_height, &img_channels, STBI_rgb_alpha);
    m_encoding_tex = m_pass->reflectTexture("SAMPLER_encoding_icons", {.width = static_cast<uint32_t>(img_width), .height = static_cast<uint32_t>(img_height), .format = vk::Format::eR8G8B8A8Unorm});
    auto [tfUploadFinished, _stagingBuffer] = m_encoding_tex->upload(image);
    getCtx()->sync->hostWaitOnDevice({tfUploadFinished});
    stbi_image_free(image);
    m_pass->setImageSampler("SAMPLER_encoding_icons", *m_encoding_tex, vk::ImageLayout::eUndefined, false);
}

void CompressedSegmentationVolumeBrickViewer::releaseShaderResources() {
    m_usegmented_volume_info = nullptr;
    m_urender_info = nullptr;
    if (m_pass)
        m_pass->freeResources();
    m_pass = nullptr;
    m_encoding_tex = nullptr;
}

void CompressedSegmentationVolumeBrickViewer::initSwapchainResources() {
    const auto screen = getCtx()->getWsi()->getScreenExtent();

    m_pass->setGlobalInvocationSize(screen.width, screen.height);
    m_outColor = m_pass->reflectTextures(
        "outColor", {.width = screen.width, .height = screen.height, .format = vk::Format::eR32G32B32A32Sfloat, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    vvv::AwaitableList reinitDone;
    for (auto &texture : *m_outColor) {
        texture->ensureResources();
        const auto layoutTransformDone = texture->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eAllCommands);
        reinitDone.push_back(layoutTransformDone);
    }
    getCtx()->sync->hostWaitOnDevice(reinitDone);
    m_timer.restart();
}

void CompressedSegmentationVolumeBrickViewer::releaseSwapchain() {
    if (m_outColor)
        m_outColor.reset();
}

void CompressedSegmentationVolumeBrickViewer::updateUniformDescriptorset() {
    const auto wsi = getCtx()->getWsi();
    // const auto camera = wsi->getCamera();
    // const auto screenExtent = wsi->getScreenExtent();

    glm::vec4 physical_volume_size(1.f, 1.f, 1.f, 1.f);

    // render info uniform
    {
        m_urender_info->setUniform<glm::vec4>("g_background_color_a", m_background_color_a);
        m_urender_info->setUniform<glm::vec4>("g_background_color_b", m_background_color_b);
        m_urender_info->setUniform<float>("g_transferFunction_limits_min", 0);
        m_urender_info->setUniform<float>("g_transferFunction_limits_max", 1000);
        m_urender_info->setUniform<glm::uvec3>("g_brick", glm::uvec3(m_brick_id));
        m_urender_info->setUniform<int>("g_brick_slice", m_brick_slice);
        m_urender_info->setUniform<int>("g_show_label_bits", m_show_label_bits ? 1 : 0);
        m_urender_info->setUniform<int>("g_show_code_mode", m_show_code_mode);
        m_urender_info->setUniform<int>("g_label_color_mult", m_label_color_mult);
        m_urender_info->setUniform<glm::vec4>("iMouse", glm::vec4{m_mousePos.x, m_mousePos.y, m_mouseHeldDown, m_mouseClicked});
        m_urender_info->setUniform<float>("iTime", static_cast<float>(m_timer.elapsed()));
    }

    // volume / Compressed Segmentation Volume uniform
    {
        uint32_t brick_size = m_compressed_segmentation_volume->getBrickSize();
        m_usegmented_volume_info->setUniform<glm::uvec4>("g_vol_dim", glm::uvec4(m_compressed_segmentation_volume->getVolumeDim(), 0u));
        m_usegmented_volume_info->setUniform<glm::vec4>("g_normalized_volume_size", physical_volume_size);
        m_usegmented_volume_info->setUniform<uint32_t>("g_vol_max_label", 1000000);
        m_usegmented_volume_info->setUniform<uint32_t>("g_brick_size", brick_size);
        m_usegmented_volume_info->setUniform<glm::uvec4>("g_brick_count", glm::uvec4(m_compressed_segmentation_volume->getBrickCount(), 0u));
        m_usegmented_volume_info->setUniform<uint32_t>("g_lod_count", static_cast<uint32_t>(log2(brick_size) + 1));
        const uint32_t cache_element_size = 4 + brick_size * brick_size * brick_size;
        m_usegmented_volume_info->setUniform<uint32_t>("g_brick_cache_count", static_cast<uint32_t>(m_cache_buffer->getByteSize() / 4 / cache_element_size));
        m_usegmented_volume_info->setUniform<uint32_t>("g_cache_element_size", cache_element_size);
    }
}

} // namespace volcanite
