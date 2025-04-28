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

#include "volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp"
#include "volcanite/compression/wavelet_tree/BitVector.hpp"

#include <chrono>
#include <memory>

#include "volcanite/StratifiedPixelSequence.hpp"
#include "vvv/core/Buffer.hpp"
#include "vvv/util/hash_memory.hpp"

#include "glm/gtc/matrix_transform.hpp"
#include "vvvwindow/App.hpp"

#ifndef HEADLESS
#include "portable-file-dialogs.h"
#endif
#ifdef IMGUI
#include "imgui.h"
#endif

using namespace vvv;

namespace volcanite {

RendererOutput CompressedSegmentationVolumeRenderer::renderNextFrame(AwaitableList awaitBeforeExecution, BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) {
    if (!(m_usegmented_volume_info && m_urender_info && m_compressed_segmentation_volume && m_csgv_db))
        throw std::runtime_error("CompressedSegmentationVolumeRenderer data missing!");

    // only start rendering the next frame, if the previous frame finished execution
    if (m_mostRecentFrame.has_value()) {
        awaitBeforeExecution.insert(awaitBeforeExecution.end(), m_mostRecentFrame->renderingComplete.begin(), m_mostRecentFrame->renderingComplete.end());
    }

    if (m_data_changed) {
        // wait until all previous frames are processed
        getCtx()->sync->hostWaitOnDevice(awaitBeforeExecution);

        // create and populate all encoding buffer (blocking)
        initDataSetGPUBuffers();

        const size_t brick_size = m_compressed_segmentation_volume->getBrickSize();
        const size_t output_voxels_per_brick = brick_size * brick_size * brick_size;
        const size_t cache_bricks = static_cast<uint32_t>(m_cache_capacity * m_cache_base_element_uints / output_voxels_per_brick);
        Logger(Debug) << "new data set with " << str(m_compressed_segmentation_volume->getBrickCount())
                      << " bricks added. Cache fits " << cache_bricks << " = "
                      << static_cast<uint32_t>(std::pow(static_cast<double>(cache_bricks), 1. / 3.))
                      << "^3 bricks on finest LoD. Need " << m_cache_palette_idx_bits
                      << " bits per palette index to store " << m_cache_indices_per_uint << " indices per uint.";

        // update invocation sizes to brick dimension
        m_pass->setVolumeInfo(m_compressed_segmentation_volume->getBrickCount(),
                              m_compressed_segmentation_volume->getLodCountPerBrick());

        // trigger accumulation buffer and cache resets
        m_presolve_hash = m_prender_hash = m_pcamera_hash = static_cast<size_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        m_pcache_reset = true;
        m_data_changed = false;
    }

    updateAttributeBuffers();

    // if one of the materials changed, update the whole buffer
    if (std::find(m_gpu_material_changed.begin(), m_gpu_material_changed.end(), true) != m_gpu_material_changed.end()) {
        // wait for previous frame to finish before uploading material buffer and textures
        getCtx()->sync->hostWaitOnDevice(awaitBeforeExecution);
        AwaitableList awaitMaterialUploads = {};
        std::vector<std::shared_ptr<Buffer>> stagingBufferHandles = {}; // used to prevent freeing buffers before finished upload

        std::vector<GPUSegmentedVolumeMaterial> gpu_mat(m_materials.size());
        for (int m = 0; m < SEGMENTED_VOLUME_MATERIAL_COUNT; m++) {
            // Discriminator
            // (we do not need to upload the attribute 0 which is the csgv_id, e.g. the voxel value)
            if (m_materials[m].getSafeDiscrAttribute() <= 0) {
                gpu_mat[m].discrAttributeStart = LABEL_AS_ATTRIBUTE;
            } else if (m_attribute_start_position[m_materials[m].getSafeDiscrAttribute()] < 0) {
                gpu_mat[m].discrAttributeStart = LABEL_AS_ATTRIBUTE;
                // ToDo: this should not throw an exception. We should only allow to select the max. number of attributes in the GUI
                throw std::runtime_error("GPU attribute buffer does not fit all selected attributes!");
            } else {
                gpu_mat[m].discrAttributeStart = static_cast<uint32_t>(m_attribute_start_position[m_materials[m].getSafeDiscrAttribute()]);
                assert(gpu_mat[m].discrAttributeStart < (m_max_attribute_buffer_size / sizeof(float)) &&
                       "invalid start index in GPU attribute buffer");
            }
            gpu_mat[m].discrIntervalMin = m_materials[m].getDiscrInterval().x;
            gpu_mat[m].discrIntervalMax = m_materials[m].getDiscrInterval().y;

            // Visualization attribute
            // (we do not need to upload the attribute 0 which is the csgv_id, e.g. the voxel value)
            if (m_materials[m].tfAttribute <= 0) {
                gpu_mat[m].tfAttributeStart = LABEL_AS_ATTRIBUTE;
            } else if (m_attribute_start_position[m_materials[m].tfAttribute] < 0) {
                gpu_mat[m].tfAttributeStart = LABEL_AS_ATTRIBUTE;
                throw std::runtime_error("GPU attribute buffer does not fit all selected attributes!");
            } else {
                gpu_mat[m].tfAttributeStart = static_cast<uint32_t>(m_attribute_start_position[m_materials[m].tfAttribute]);
                assert(gpu_mat[m].tfAttributeStart < (m_max_attribute_buffer_size / sizeof(float)) &&
                       "invalid start index in GPU attribute buffer");
            }
            gpu_mat[m].tfIntervalMin = m_materials[m].tfMinMax.x;
            gpu_mat[m].tfIntervalMax = m_materials[m].tfMinMax.y;
            gpu_mat[m].opacity = m_materials[m].opacity;
            gpu_mat[m].emission = m_materials[m].emission * m_materials[m].emission; // ^2 for better user control
            gpu_mat[m].wrapping = m_materials[m].wrapping;

            // update transfer function textures
            constexpr int TF_WIDTH = 256;
            // TODO: the color space depends on the colormap type
            m_materialTransferFunctions[m] = m_materials[m].tf->rasterize(getCtx(), TF_WIDTH);
            auto [_tf1dAwait, _tf1dStagingBuf] = m_materialTransferFunctions[m]->upload();
            awaitBeforeExecution.push_back(_tf1dAwait);
            stagingBufferHandles.push_back(_tf1dStagingBuf);
            m_pass->setImageSamplerArray("s_transferFunctions", m, m_materialTransferFunctions[m]->texture(), vk::ImageLayout::eReadOnlyOptimal, false);

            m_gpu_material_changed[m] = false;
        }
        auto [material_upload_finished, _material_upload_staging_buffer] = m_materials_buffer->uploadWithStagingBuffer(
            gpu_mat.data(), sizeof(GPUSegmentedVolumeMaterial) * m_materials.size(), {.queueFamily = m_queue_family_index});
        awaitMaterialUploads.push_back(material_upload_finished);
        getCtx()->sync->hostWaitOnDevice(awaitMaterialUploads); // have to wait here, otherwise the staging buffers are freed immediately

        m_pmaterial_reset = true;
    }

    // wait for the last frame to finish execution (which will also mean that the previous upload of the detail starts
    // finished). Times out after a certain number of seconds and throws an exception.
    // Buffer upload synchronization is handled in PassCompSegVolRender
    getCtx()->sync->hostWaitOnDevice(awaitBeforeExecution, 60 * 1000000000ull);

    // track timing of the last frame
    // TODO: renderFrame and thus the final timing finish will not be called after the last frame in HeadlessRendering
    if (m_enable_frame_time_tracking && m_last_frame_start_time.has_value()) {
        m_last_frame_times.emplace_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                                std::chrono::high_resolution_clock::now() - m_last_frame_start_time.value())
                                                                .count()) /
                                        1000000.);
        m_last_frame_start_time.reset();
    }

    // if a screenshot export was requested, we do this here (not timed)
    if (m_download_frame_to_image_file.has_value() && m_mostRecentFrame.has_value()) {
        Logger(Info) << "exporting screenshot to " << m_download_frame_to_image_file.value();
        try {
            m_mostRecentFrame->texture->writeFile(m_download_frame_to_image_file.value(), m_queue_family_index);
        } catch (const std::runtime_error &e) {
            Logger(Error) << "image export error: " << e.what();
        }
        m_download_frame_to_image_file = {};
    }

    // do not render anything in step mode if no new step is requested
    if (m_accum_step_mode && !m_accum_do_step && m_accumulated_frames > 0u) { // except at later frames: no step
        return m_mostRecentFrame.value();
    }
    m_accum_do_step = false;

    // time tracking start (on host side to consider semaphores, CPU work as well, not just the GPU render times)
    if (m_enable_frame_time_tracking)
        m_last_frame_start_time = std::chrono::high_resolution_clock::now();

    // set render update flags to denote which parameter sets changed, and which render stages have to be executed
    updateRenderUpdateFlags();

    // download GPU statistics information, i.e. the cache occupancy to trigger cache hard resets when it is full
    static constexpr uint32_t gpu_stats_download_interval = 1u;
    static constexpr GPUStats reset_gpu_stats = {
        .min_spp_and_pixel = 0xFFFF0000FFFFFFFF, // 16 MSB: sample count, 32 LSB: pixel coordinate
        .max_spp_and_pixel = 0x00000000FFFFFFFF,
        .limit_area_pixel_spp = 0u,
        .used_cache_base_elements = 0u,
        .bbox_hits = 0u,
        .blocks_decoded_L1_to_7 = {0u, 0u, 0u, 0u, 0u, 0u},
        .blocks_in_cache_L1_to_7 = {0u, 0u, 0u, 0u, 0u, 0u},
        .bricks_requested_L1_to_7 = {0u, 0u, 0u, 0u, 0u, 0u},
        .bricks_on_freestack_L1_to_7 = {0u, 0u, 0u, 0u, 0u, 0u},
    };
    if (m_accumulated_frames == 0u) {
        m_gpu_stats_buffer->upload(&reset_gpu_stats, sizeof(GPUStats));
    } else if ((m_render_update_flags & UPDATE_RENDER_FRAME) && m_accumulated_frames % gpu_stats_download_interval == 0u) {
        // download and reset the buffer
        m_gpu_stats_buffer->download(&m_last_gpu_stats, sizeof(GPUStats));
        m_gpu_stats_buffer->upload(&reset_gpu_stats, sizeof(GPUStats));

        // compute cache fill rate to feed it back to the renderer as uniform value
        const uint32_t cache_elements_per_finest_lod = (m_compressed_segmentation_volume->getBrickSize() / 2u) * (m_compressed_segmentation_volume->getBrickSize() / 2u) * (m_compressed_segmentation_volume->getBrickSize() / 2u);
        const uint32_t global_min_spp = static_cast<uint32_t>(m_last_gpu_stats.min_spp_and_pixel >> 48u);
        m_req_limit.global_min_pixel = glm::ivec2(m_last_gpu_stats.min_spp_and_pixel & 0xFFFF,
                                                  (m_last_gpu_stats.min_spp_and_pixel >> 16u) & 0xFFFF);
        const uint32_t global_max_spp = static_cast<uint32_t>(m_last_gpu_stats.max_spp_and_pixel >> 48u);
        // const glm::ivec2 max_spp_pixel = glm::ivec2(m_last_gpu_stats.max_spp_and_pixel & 0xFFFF,
        //                                             (m_last_gpu_stats.max_spp_and_pixel >> 16u) & 0xFFFF);

        // if the area pixel was selected randomly, its sample count was not known yet (marked by INVALID). set it here.
        if (m_req_limit.area_min_pixel_last_spp == INVALID)
            m_req_limit.area_min_pixel_last_spp = m_last_gpu_stats.limit_area_pixel_spp;

        // TODO: check for cache fragmentation in the cache shaders by comparing the free stacks instead of checking the cache capacity here
        // A fragmented cache can occur if free stacks store unusable levels-of-detail leaving no space for other LoDs.
        // Trigger cache flush on demand, but only if the rendering config changed since the last flush.
        const size_t current_parameter_hash = hashMemory(&m_prender_hash, sizeof(m_prender_hash), m_pcamera_hash);

        // used_cache_base_elements: cache usage as the number of occupied 2x2x2 base elements
        if (m_accumulated_frames > 0u && m_auto_cache_reset && m_last_gpu_stats.used_cache_base_elements >= m_cache_capacity - cache_elements_per_finest_lod // cannot fit a single additional finest LOD brick
            && m_parameter_hash_at_last_reset != current_parameter_hash) {                                                                                   // cache was not yet reset on this position

            // the cache can be fragmented: one of the free stacks did not contain enough base elements to fulfill all
            // requests for its LOD (while the cache is already full), but other free stacks have unused elements
            // that, in combination, span as much storage as is required to decode at least one brick in the finest LOD.
            // uint32_t free_base_elements = 0u;
            // long missing_base_elements = 0u;
            // for (int inv_lod = 0; inv_lod < m_compressed_segmentation_volume->getLodCountPerBrick(); inv_lod++) {
            //     free_base_elements += (1u << (3*inv_lod)) * m_last_gpu_stats.bricks_on_freestack_L1_to_7[inv_lod];
            //     missing_base_elements += glm::max(0l, (1u << (3*inv_lod)) * (static_cast<long>(m_last_gpu_stats.bricks_requested_L1_to_7[inv_lod])
            //                                 - static_cast<long>(m_last_gpu_stats.bricks_on_freestack_L1_to_7[inv_lod])));
            // }
            // if (free_base_elements > cache_elements_per_finest_lod && missing_base_elements > 0l) {
            { // pragmatic approach: if the cache is full, reset it once for this camera perspective
                m_pcache_reset = true;
                m_parameter_hash_at_last_reset = current_parameter_hash;
                disableRequestLimiation();
            }
        }
        if (!m_pcache_reset) {
            // Request limitation limits requesting bricks for the cache to rays from pixels in a certain AABB on screen.
            // The AABB originates at m_req_limit_area_pos with size m_req_limit_area.
            if (m_req_limit.g_enable) {
                updateRequestLimiation(global_min_spp, global_max_spp);
            } else {
                disableRequestLimiation();
            }
        }

        // if requested, more GPU statistics were gathered during rendering and printed here
        if (m_debug_vis_flags & VDEB_STATS_DOWNLOAD_BIT) {
            size_t decoded_bytes_in_frame = 0;
            size_t decoded_bytes_total = 0;
            std::stringstream cache_state = {};
            cache_state << " decoded: ";
            for (int lod = 1; lod < m_compressed_segmentation_volume->getLodCountPerBrick(); lod++) {
                decoded_bytes_in_frame += m_last_gpu_stats.blocks_decoded_L1_to_7[lod - 1] * (1u << (lod * 3)) * 4;
                decoded_bytes_total += m_last_gpu_stats.blocks_decoded_L1_to_7[lod - 1] * (1u << (lod * 3)) * 4;
                cache_state << "inv. LOD" << lod << ": " << m_last_gpu_stats.blocks_decoded_L1_to_7[lod - 1] << ", ";
            }
            cache_state << " in total: " << static_cast<double>(decoded_bytes_total) * BYTE_TO_MB << " MB";
            if (decoded_bytes_in_frame > 0)
                Logger(Info) << cache_state.str();
        }
    }

    // if any previous detail construction is finished, download next brick indices for which the detail level is required
    if (m_compressed_segmentation_volume->isUsingSeparateDetail() && m_detail_stage == DetailReady && m_frame > 0u) {
        assert(m_detail_requests.size() >= m_max_detail_requests_per_frame + 1u && "Detail request buffer is too small.");

        m_detail_requests_buffer->download(m_detail_requests);
        // second to last element stores the number of brick indices for which the detail level is requested
        uint32_t detail_request_count = std::min(m_detail_requests[m_max_detail_requests_per_frame], m_max_detail_requests_per_frame);

        // reset the (atomic) counter at location
        static constexpr uint32_t zero{0u};
        m_detail_requests_buffer->upload(m_max_detail_requests_per_frame * sizeof(uint32_t), &zero, sizeof(uint32_t));

        if (detail_request_count > 0u)
            m_detail_stage = DetailAwaitingCPUConstruction;
    }

    // if no new data has to be rendered, just return the last frame
    if (!(m_render_update_flags & (UPDATE_RENDER_FRAME | UPDATE_PRESOLVE))) {
        return m_mostRecentFrame.value();
    }

    // upload uniforms and set pass properties
    updateUniformDescriptorset();

    m_ucamera_info->upload(m_pass->getActiveIndex());
    m_urender_info->upload(m_pass->getActiveIndex());
    m_uresolve_info->upload(m_pass->getActiveIndex());
    m_usegmented_volume_info->upload(m_pass->getActiveIndex());

    // start asynchronous detail upload if scheduled
    if (m_detail_stage == DetailAwaitingUpload && m_constructed_detail_starts.back() > 0u) {
        m_detail_stage = DetailUploading;
        std::thread detail_upload_thread([this]() {
            // start the buffer copies and uploads. if everything finished, DetailReady is reached and thread terminates
            m_detail_starts_buffer->upload(m_constructed_detail_starts.data(), m_constructed_detail_starts.size() * sizeof(uint32_t));
            m_detail_buffer->upload(m_constructed_detail.data(), m_constructed_detail_starts.back() * sizeof(uint32_t));
            m_detail_stage = DetailReady;
        });
        detail_upload_thread.detach();
    }

    m_pass->setStorageImage("inpaintedOutColor", *m_inpaintedOutColor);
    // feedback texture ping pong for the inpainting shader
    m_pass->setStorageImage("accumulationIn", *m_accumulation_rgba_tex[m_frame % 2u]);
    m_pass->setStorageImage("accumulationOut", *m_accumulation_rgba_tex[1u - (m_frame % 2u)]);
    m_pass->setStorageImage("accuSampleCountIn", *m_accumulation_samples_tex[m_frame % 2u]);
    m_pass->setStorageImage("accuSampleCountOut", *m_accumulation_samples_tex[1u - (m_frame % 2u)]);
    // 24 bit packed gBuffer texture
    m_pass->setStorageImage("gBuffer", *m_g_buffer_tex);

    // ping pong texture for resolve passes
    m_pass->setStorageImageArray("denoisingBuffer", 0, *m_denoise_tex[0], vk::ImageLayout::eGeneral, false);
    m_pass->setStorageImageArray("denoisingBuffer", 1, *m_denoise_tex[1], vk::ImageLayout::eGeneral, false);

    std::vector<std::shared_ptr<Awaitable>> renderAwaitableList = {};
    const auto renderingFinished = m_pass->execute(renderAwaitableList, awaitBinaryAwaitableList, signalBinarySemaphore);

    if (m_detail_stage == DetailAwaitingCPUConstruction) {
        assert(!m_constructed_detail.empty() && "trying to construct detail buffers but detail buffer has no capacity");
        // TODO: m_detail_update_required = check if current and previous detail indices changed
        // TODO: use remaining space in g_detail to store the previously requested bricks. would need one dummy element
        //  in between to make the detail_starts[i+1]-[i] size query possible. Could use a ring buffer.
        // TODO: do *all* the CPU work for detail construction in another thread so nothing blocks the rendering

        uint32_t detail_request_count = std::min(m_detail_requests[m_max_detail_requests_per_frame], m_max_detail_requests_per_frame);
        bool detail_update_required = false;
// check if any requested brick indices are new, otherwise the buffers can stay unchanged
#pragma omp parallel for default(none) shared(detail_request_count, m_detail_requests, m_constructed_detail_starts, detail_update_required)
        for (int i = 0; i < detail_request_count; i++) {
            if (detail_update_required)
                continue;
            if (m_constructed_detail_starts[m_detail_requests[i] + 1u] - m_constructed_detail_starts[m_detail_requests[i]] == 0u) {
                detail_update_required = true;
            }
        }

        if (detail_update_required) {
            m_detail_stage = DetailCPUConstruction;
            // async thread constructs detail buffers on the CPU side and marks m_detail_stage = DetailAwaitingUpload once finished
            std::thread construction_thread(&CompressedSegmentationVolumeRenderer::updateCPUDetailBuffers, this);
            construction_thread.detach();
        } else {
            m_detail_stage = DetailReady;
        }
    }

    // Update GPU memory usage regularly
    if (m_frame % 300 == 0u) {
        updateDeviceMemoryUsage();
    }

    // update tracking variables
    m_frame++;
    if (m_render_update_flags & UPDATE_RENDER_FRAME)
        m_accumulated_frames++;

    m_mostRecentFrame = vvv::RendererOutput{
        .texture = m_inpaintedOutColor->getActive().get(),
        .renderingComplete = {renderingFinished},
        .queueFamilyIndex = m_queue_family_index};
    return m_mostRecentFrame.value();
}

void CompressedSegmentationVolumeRenderer::updateCPUDetailBuffers() {
    if (m_detail_stage != DetailCPUConstruction)
        throw std::runtime_error("Attempting to construct detail buffers on CPU in wrong stage.");

    uint32_t detail_request_count = std::min(m_detail_requests[m_max_detail_requests_per_frame], m_max_detail_requests_per_frame);

    // 1. sort requested brick IDs
    std::sort(m_detail_requests.begin(), m_detail_requests.begin() + detail_request_count);
    // 2. for ALL bricks: compute prefix sum of sizes, assuming an added 0 size if brick is not requested. Store in m_detail_starts
    uint32_t next_requested_id = 0u;
    uint32_t total_detail_size = 0u;
    uint32_t _total_bricks_in_buffer = 0u;
    for (int i = 0; i < m_constructed_detail_starts.size(); i++) {
        m_constructed_detail_starts[i] = total_detail_size;

        // if this id is requested, we reserve some memory for it (as long as there's enough space in the detail array left)
        if (next_requested_id < detail_request_count && i == m_detail_requests[next_requested_id]) {
            uint32_t brick_detail_size = m_compressed_segmentation_volume->getBrickDetailEncodingLength(i);
            if ((total_detail_size + brick_detail_size) <= m_detail_capacity) {
                total_detail_size += brick_detail_size;
                _total_bricks_in_buffer++;
            }

            // even if the previous brick did not fit, we can try the next ones
            // TODO: m_detail_requests may contain duplicates! use a hash set on the GPU instead, remove atomics
            while (next_requested_id < detail_request_count && i == m_detail_requests[next_requested_id])
                next_requested_id++;
        }
    }
// 3. in parallel: copy all detail encodings to the m_detail_encodings
#pragma omp parallel for default(none) shared(detail_request_count, m_detail_requests, m_constructed_detail_starts, m_constructed_detail)
    for (int i = 0; i < detail_request_count; i++) {
        uint32_t brick_idx = m_detail_requests[i];
        uint32_t reserved_size = m_constructed_detail_starts[brick_idx + 1] - m_constructed_detail_starts[brick_idx];
        // if we reserved space for this brick id, copy it
        if (reserved_size > 0) {
            const uint32_t *detail_encoding = m_compressed_segmentation_volume->getBrickDetailEncoding(brick_idx);
            uint32_t detail_length = m_compressed_segmentation_volume->getBrickDetailEncodingLength(brick_idx);
            if (reserved_size != detail_length)
                Logger(Error) << reserved_size << " vs " << detail_length << " for brick " << brick_idx;
            assert(reserved_size == detail_length && "did not reserve fitting detail encoding area for brick.");
            memcpy(m_constructed_detail.data() + m_constructed_detail_starts[brick_idx], detail_encoding, reserved_size * sizeof(uint32_t));
        }
    }

#if 0
    if (m_constructed_detail_starts.back() > 0u) {
        std::stringstream ss;
        ss << "Upload detail (" << total_bricks_in_buffer << " bricks) of size: " << m_constructed_detail_starts.back() << " for bricks ";
        for (int i = 0; i < glm::min(detail_request_count, 8u); i++) {
            ss << m_detail_requests[i] << " ";
        }
        Logger(Info) << ss.str();
    }
#endif

    // GPU upload can only start if all current rendering is finished and is thus dispatched in the render loop
    m_detail_stage = DetailAwaitingUpload;
}

void CompressedSegmentationVolumeRenderer::setCompressedSegmentationVolume(
    std::shared_ptr<CompressedSegmentationVolume> csgv, std::shared_ptr<CSGVDatabase> db) {
    if (!csgv)
        throw std::runtime_error("CompressedSegmentationVolume must not be null");
    if (!db)
        throw std::runtime_error("CompressedSegmentationVolume database must not be null");

    m_compressed_segmentation_volume = std::move(csgv);
    m_data_changed = true;

    // check how many bits are required to store cache indices
    if (m_use_palette_cache) {
        // must be (max_palette_count + 1), need an additional magic number (= 0) for not yet written output voxels
        m_cache_palette_idx_bits = static_cast<uint32_t>(glm::ceil(
            glm::log2(static_cast<double>(m_compressed_segmentation_volume->getMaxBrickPaletteCount()) + 1.0)));
        m_cache_indices_per_uint = 32u / m_cache_palette_idx_bits;
        m_cache_base_element_uints = (8u + m_cache_indices_per_uint - 1u) /
                                     m_cache_indices_per_uint; // = ceil(8 / m_palette_indices_per_uint)
    } else {
        // without paletting, the cache stores explicit 32 bit labels = one label per uint
        m_cache_palette_idx_bits = 32u;
        m_cache_indices_per_uint = 1u;
        m_cache_base_element_uints = 8;
    }

    // when a database is provided, we use it for attribute visualization
    m_csgv_db = std::move(db);
    m_attribute_start_position.resize(m_csgv_db->getAttributeCount(), -1);
    // update transfer function limits
    for (int m = 0; m < SEGMENTED_VOLUME_MATERIAL_COUNT; m++) {
        if (m_materials[m].discrAttribute >= 0) {
            m_materials[m].discrInterval = m_csgv_db->getAttributeMinMax().at(m_materials[m].discrAttribute);
            if (m == 0 && m_materials[m].discrInterval.x == 0u)
                m_materials[m].discrInterval.x = 1u;
        }
        m_materials[m].tfMinMax = m_csgv_db->getAttributeMinMax().at(m_materials[m].tfAttribute);
    }

    // determine the block size of the empty space skipping grid
    if (m_empty_space_block_dim > 0u) {
        glm::uvec3 ess_dim;
        // check if the ESS volume dimension supports the block size (has to be indexed with 32 bits)
        do {
            glm::uvec3 vol_dim = m_compressed_segmentation_volume->getVolumeDim();
            ess_dim = (vol_dim + glm::uvec3(m_empty_space_block_dim - 1u)) / m_empty_space_block_dim;
            if (static_cast<size_t>(ess_dim.x) * ess_dim.y * ess_dim.z <= 0xFFFFFFFFull)
                break;
            m_empty_space_block_dim *= 2ul;
        } while (true);
        if (m_empty_space_block_dim > m_compressed_segmentation_volume->getBrickSize()) {
            throw std::runtime_error("Brick size is too small to create empty space skipping structure.");
        }

        // each cell of the empty space skipping grid requires one bit. the bit vector is stored in BV_WordType elements.
        // ceil(ceil(cell_count / 8) / sizeof(BV_WORD_TYPE)) * sizeof(BV_WORD_TYPE)
        m_empty_space_buffer_size = (ess_dim.x * ess_dim.y * ess_dim.z + sizeof(BV_WordType) * 8u - 1) / 8u;                             // bytes
        m_empty_space_buffer_size = ((m_empty_space_buffer_size + sizeof(BV_WordType) - 1) / sizeof(BV_WordType)) * sizeof(BV_WordType); // bytes for words
    } else {
        m_empty_space_buffer_size = 0u;
    }

    // update GUI parameters
    m_bboxMin = glm::ivec3(0);
    m_bboxMax = glm::ivec3(m_compressed_segmentation_volume->getVolumeDim());
}

void CompressedSegmentationVolumeRenderer::initDataSetGPUBuffers() {
    if (!m_compressed_segmentation_volume || m_compressed_segmentation_volume->getAllEncodings()->empty())
        throw std::runtime_error("CompressedSegmentationVolume not initialized!");
    if (m_compressed_segmentation_volume->isUsingSeparateDetail() && !m_compressed_segmentation_volume->isUsingDetailFreq())
        throw std::runtime_error("Renderer only supports detail separation when rANS is in double table mode.");

    const GpuContext *ctx = getCtx();

    // CREATE GPU BUFFERS ---------------------------------
    const size_t bricks_in_volume = m_compressed_segmentation_volume->getBrickIndexCount();
    const size_t lods_in_volume = m_compressed_segmentation_volume->getLodCountPerBrick();

    // create (base) split encoding buffers
    m_brick_starts_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_brick_start_buffer", .byteSize = (bricks_in_volume + 1u) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    const size_t split_encoding_count = m_compressed_segmentation_volume->getAllEncodings()->size();
    m_split_encoding_buffers.resize(split_encoding_count);
    m_split_encoding_buffer_addresses.resize(split_encoding_count);
    m_split_encoding_buffer_addresses_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_split_encoding_buffer_addresses_buffer", .byteSize = split_encoding_count * 2 * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    for (int i = 0; i < split_encoding_count; i++) {
        const size_t encoding_byte_size =
            m_compressed_segmentation_volume->getAllEncodings()->at(i).size() * sizeof(uint32_t);
        m_split_encoding_buffers[i] = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_encoding_buffer_" + std::to_string(i), .byteSize = encoding_byte_size, .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
        Buffer::deviceAddressUvec2(m_split_encoding_buffers[i]->getDeviceAddress(), &m_split_encoding_buffer_addresses[i].x);
    }

    // create attribute and material buffers
    m_attribute_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_attribute_buffer", .byteSize = m_max_attribute_buffer_size, .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    m_materials_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_materials_buffer", .byteSize = sizeof(GPUSegmentedVolumeMaterial) * SEGMENTED_VOLUME_MATERIAL_COUNT, .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});

    // create detail encoding buffers
    m_detail_capacity = 0u; // measured in number of uints
    if (m_compressed_segmentation_volume->isUsingSeparateDetail()) {
        bool detail_buffer_fits_whole_detail = false;
        size_t complete_detail_size = 0;
        for (const auto &d : *m_compressed_segmentation_volume->getAllDetails())
            complete_detail_size += d.size();
#define ALWAYS_STREAM_DETAIL
#ifdef ALWAYS_STREAM_DETAIL
        if (true) {
#else
        // we can't fit the complete detail buffer onto the GPU
        if (m_max_detail_byte_size / sizeof(uint32_t) < complete_detail_size) {
#endif
            m_detail_capacity = m_max_detail_byte_size / sizeof(uint32_t);
        }
        // we can fit the complete detail buffer onto the GPU
        else {
            m_detail_capacity = complete_detail_size;
            detail_buffer_fits_whole_detail = true;
        }

        m_detail_requests.resize(m_max_detail_requests_per_frame + 1u); // last element stores the atomic counter for the position to insert elements
        m_detail_requests_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_detail_requests_buffer", .byteSize = m_detail_requests.size() * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc, .memoryUsage = vk::MemoryPropertyFlagBits::eHostVisible});
        m_detail_starts_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_detail_starts_buffer", .byteSize = (bricks_in_volume + 1u) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eHostVisible});
        m_detail_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_detail_buffer", .byteSize = m_detail_capacity * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, .memoryUsage = vk::MemoryPropertyFlagBits::eHostVisible});
        Buffer::deviceAddressUvec2(m_detail_buffer->getDeviceAddress(), &m_detail_buffer_address.x);

        m_constructed_detail_starts.resize(bricks_in_volume + 1u, 0u);
        m_constructed_detail.resize(m_detail_capacity, 0u);
        if (detail_buffer_fits_whole_detail) {
            Logger(Warn) << "GPU detail buffer fits the whole detail level. Performing full upload, effectively disabling detail streaming. Consider to not use detail streaming for better performance!";

            size_t offset = 0ul;
            size_t brick_idx = 0ul;
            for (int i = 0; i < m_compressed_segmentation_volume->getAllDetails()->size(); i++) {
                const std::vector<uint32_t> &detail_encoding = m_compressed_segmentation_volume->getAllDetails()->at(i);
                // upload next single detail encoding buffer into offset memory region to form a back-to-back buffer
                m_detail_staging = m_detail_buffer->uploadWithStagingBuffer(detail_encoding.data(),
                                                                            detail_encoding.size() * sizeof(uint32_t),
                                                                            offset * sizeof(uint32_t),
                                                                            {.queueFamily = m_queue_family_index});
                // construct detail starts into continuous detail encoding array
                while (brick_idx < bricks_in_volume && brick_idx / m_compressed_segmentation_volume->getBrickIdxToEncVectorMapping() == i) {
                    m_constructed_detail_starts[brick_idx + 1] = m_constructed_detail_starts[brick_idx] + m_compressed_segmentation_volume->getBrickDetailEncodingLength(brick_idx);
                    brick_idx++;
                }
                offset += detail_encoding.size();
                getCtx()->sync->hostWaitOnDevice({m_detail_staging.first});
            }
        }
        // upload initial detail starts buffer (all zeros if no detail is uploaded initially)
        m_detail_starts_staging = m_detail_starts_buffer->uploadWithStagingBuffer(m_constructed_detail_starts.data(), m_constructed_detail_starts.size() * sizeof(uint32_t), {.queueFamily = m_queue_family_index});
        getCtx()->sync->hostWaitOnDevice({m_detail_starts_staging.first});
        m_detail_staging = {nullptr, nullptr};
        m_detail_starts_staging = {nullptr, nullptr};
    }

    // GPU statistics buffer
    m_gpu_stats_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_gpu_stats_buffer", .byteSize = sizeof(GPUStats), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc, .memoryUsage = vk::MemoryPropertyFlagBits::eHostVisible});

    // empty space skipping buffer TODO: only use m_empty_space_buffer with m_cache_mode == CACHE_VOXELS?
    if (m_empty_space_buffer_size > 0ul) {
        m_empty_space_buffer = std::make_shared<Buffer>(ctx,
                                                        BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_empty_space_buffer", .byteSize = m_empty_space_buffer_size, .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
        Buffer::deviceAddressUvec2(m_empty_space_buffer->getDeviceAddress(), &m_empty_space_buffer_address.x);
    } else {
        m_empty_space_buffer = nullptr;
        m_empty_space_buffer_address = glm::uvec2(0u);
    }

    // cache for decompressed bricks
    m_free_stack_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_free_stack_buffer", .byteSize = (m_free_stack_capacity * (lods_in_volume - 1u) + (lods_in_volume + 1u)) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    m_cache_info_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_cache_info_buffer", .byteSize = bricks_in_volume * sizeof(uint32_t) * 4u, .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    m_assign_info_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_assign_buffer", .byteSize = (1u + (lods_in_volume - 1u) * 3u) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    // cache size is determined depending on the cache mode
    uint32_t cache_uint_size = 0ul;
    if (m_cache_mode == CACHE_NOTHING) {
        cache_uint_size = 1ul; // minimal size as the cache is not used regardless
    } else if (m_cache_mode == CACHE_VOXELS) {
        cache_uint_size = m_target_cache_size_MB * 1024 * 1024 / sizeof(uint32_t);
        Logger(Info) << "Allocating cache with size " << m_target_cache_size_MB << " MB";
    } else if (m_cache_mode == CACHE_BRICKS) {
        // limit cache size to maximum available GPU memory
        auto heap_budget_and_usage = getMemoryHeapBudgetAndUsage(*ctx);
        const size_t free_heap_size_byte = heap_budget_and_usage.first - heap_budget_and_usage.second;
        if (m_target_cache_size_MB * 1024 * 1024 > free_heap_size_byte) {
            updateDeviceMemoryUsage();
            Logger(Warn) << "not enough GPU memory available to provide target cache size of " << m_target_cache_size_MB
                         << " MB. Using smaller cache. " << m_gui_device_mem_text;
            m_target_cache_size_MB = 0;
        }
        // target size of 0 means to allocate as much for the cache as we can (or rather 85% of it to have some leeway)
        if (m_target_cache_size_MB == 0) {
            if (free_heap_size_byte >= 196) {
                constexpr float other_required_device_mb = 128.f;
                m_target_cache_size_MB = static_cast<size_t>(static_cast<float>(free_heap_size_byte) / 1024.f / 1024.f - other_required_device_mb);
            } else {
                m_target_cache_size_MB = static_cast<size_t>(static_cast<float>(free_heap_size_byte) / 1024.f / 1024.f);
            }
        }
        // for small data sets, we can limit the cache so that it fits all LoDs of the data set at once
        size_t maximum_req_cache_size_MB = 4095;
        {
            // number base elements needed to store all LoDs of a brick at once
            size_t brick_cache_size_in_lod = m_cache_base_element_uints; // inv. LoD 0 needs no elements, inv. LoD 1 needs one
            maximum_req_cache_size_MB = brick_cache_size_in_lod;
            for (int l = 2; l < m_compressed_segmentation_volume->getLodCountPerBrick(); l++) {
                // next level needs 8 times the elements
                brick_cache_size_in_lod *= 2 * 2 * 2;
                maximum_req_cache_size_MB += brick_cache_size_in_lod;
            }
            maximum_req_cache_size_MB *= m_compressed_segmentation_volume->getBrickIndexCount();
            // convert from #uints to MB
            maximum_req_cache_size_MB *= sizeof(uint32_t);
            maximum_req_cache_size_MB = static_cast<size_t>(std::ceil(
                static_cast<double>(maximum_req_cache_size_MB) / 1024. / 1024));

            // TODO: include m_cache_base_element_uints and/or m_cache_indices_per_uint when computing required cache size
        }
        if (m_target_cache_size_MB > maximum_req_cache_size_MB) {
            Logger(Debug)
                << "Target cache size is bigger than required to store all LoDs of all bricks. Limitting size.";
            m_target_cache_size_MB = maximum_req_cache_size_MB;
        }
        // TODO: could use the BufferDeviceAddress extension for the cache buffer as well to support caches > 4 GB.
        //  In shaders, cache idx is measured in # base_element where one base_element is ~4 to 16 bytes large.
        //  Would only have to adapt the (cache_idx * g_cache_base_element_uints) in csgv_assign.
        // limit cache size to 4GB if the previous steps increased it by too much
        if (m_target_cache_size_MB * 1024ul * 1024ul > 4294967295ul) {
            m_target_cache_size_MB = 4294967295ul / 1024ul / 1024ul;
        }

        m_cache_capacity = (m_target_cache_size_MB * 1024 * 1024) / (m_cache_base_element_uints * sizeof(uint32_t));
        cache_uint_size = m_cache_capacity * m_cache_base_element_uints;
        Logger(Info) << "Allocating cache with size " << m_target_cache_size_MB << " MB at " << (m_cache_base_element_uints * 32u / 8u) << " bits per label";
    }
    // size_t maxGPUBufferSize = getCtx()->getPhysicalDevice().getProperties().limits.maxStorageBufferRange;
    m_cache_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_cache_buffer", .byteSize = cache_uint_size * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    Buffer::deviceAddressUvec2(m_cache_buffer->getDeviceAddress(), &m_cache_buffer_address.x);

    updateDeviceMemoryUsage();
    Logger(Info) << "Device memory after initialization: " << m_gui_device_mem_text;

    // UPLOAD TO GPU BUFFERS ----------------------------------
    AwaitableList awaitBeforeExecution;
    std::vector<std::pair<AwaitableHandle, std::shared_ptr<vvv::Buffer>>> _encoding_upload;
    for (int i = 0; i < split_encoding_count; i++) {
        _encoding_upload.emplace_back(
            m_split_encoding_buffers[i]->uploadWithStagingBuffer(m_compressed_segmentation_volume->getAllEncodings()->at(i),
                                                                 {.queueFamily = getCtx()->getQueueFamilyIndices().transfer.value()}));
        awaitBeforeExecution.push_back(_encoding_upload[i].first);
    }
    auto [encoding_addresses_upload_finished, _encoding_addresses_staging_buffer] = m_split_encoding_buffer_addresses_buffer->uploadWithStagingBuffer(m_split_encoding_buffer_addresses, {.queueFamily = m_queue_family_index});
    awaitBeforeExecution.push_back(encoding_addresses_upload_finished);
    auto [brickstarts_upload_finished, _brickstarts_staging_buffer] = m_brick_starts_buffer->uploadWithStagingBuffer(*(m_compressed_segmentation_volume->getBrickStarts()), {.queueFamily = m_queue_family_index});
    awaitBeforeExecution.push_back(brickstarts_upload_finished);

    // wait until all uploads finished
    getCtx()->sync->hostWaitOnDevice(awaitBeforeExecution);

    // update all bindings
    m_pass->setStorageBuffer(0, 1, *m_brick_starts_buffer);
    m_pass->setStorageBuffer(0, 2, *m_split_encoding_buffer_addresses_buffer);
    m_pass->setStorageBuffer(0, 3, *m_cache_info_buffer);
    m_pass->setStorageBuffer(0, 4, *m_assign_info_buffer);
    m_pass->setStorageBuffer(0, 5, *m_free_stack_buffer);
    m_pass->setStorageBuffer(0, 6, *m_cache_buffer);
    if (m_compressed_segmentation_volume->isUsingSeparateDetail()) {
        m_pass->setStorageBuffer(0, 7, *m_detail_starts_buffer);
        m_pass->setStorageBuffer(0, 8, *m_detail_buffer);
        m_pass->setStorageBuffer(0, 9, *m_detail_requests_buffer);
    }
    m_pass->setStorageBuffer(0, 16, *m_gpu_stats_buffer);
    m_pass->setStorageBuffer(0, 17, *m_attribute_buffer);
    m_pass->setStorageBuffer(0, 18, *m_materials_buffer);
}

void CompressedSegmentationVolumeRenderer::initResources(GpuContext *ctx) {
    setCtx(ctx);
    updateDeviceMemoryUsage();
    Logger(Info) << "Device memory on startup: " << m_gui_device_mem_text;

    // TODO: all rendering should happen on the compute queue, + queue ownership transfer to present for the Application
    m_queue_family_index = getCtx()->getQueueFamilyIndices().graphics.value();

    // Set camera to a nice start position
    getCamera().reset();

    // all buffers for the encoding etc. are created in initDataSetGPUBuffers() called in the first render loop
    if (m_compressed_segmentation_volume)
        m_data_changed = true; // trigger creation of buffers and re-upload of data
    for (auto &&m : m_gpu_material_changed)
        m = true;
    const int attributeCount = m_csgv_db ? static_cast<int>(m_csgv_db->getAttributeCount()) : 1;
    for (int a = 0; a < attributeCount; a++)
        m_attribute_start_position.at(a) = -1;
}

void CompressedSegmentationVolumeRenderer::releaseResources() {
    m_gpu_stats_buffer = nullptr;
    m_assign_info_buffer = nullptr;
    m_cache_info_buffer = nullptr;
    m_free_stack_buffer = nullptr;
    m_cache_buffer = nullptr;
    m_empty_space_buffer = nullptr;
    m_attribute_buffer = nullptr;
    m_materials_buffer = nullptr;
    for (auto &e : m_split_encoding_buffers)
        e = nullptr;
    m_split_encoding_buffers.clear();
    m_split_encoding_buffer_addresses.clear();
    m_split_encoding_buffer_addresses_buffer = nullptr;
    m_brick_starts_buffer = nullptr;
    m_detail_buffer = nullptr;
    m_detail_starts_buffer = nullptr;
    m_detail_requests_buffer = nullptr;
    m_detail_starts_staging.second = nullptr;
    m_detail_staging.second = nullptr;
    for (auto &tf : m_materialTransferFunctions)
        tf = nullptr;
    setCtx(nullptr);
}

void CompressedSegmentationVolumeRenderer::initShaderResources() {
    assert(getCtx() != nullptr && "renderer needs a valid GPU context");
    assert(m_compressed_segmentation_volume && "can't render without a CompressedSegmentationVolume");

    // the shader code is dependent on data set properties like operation frequency tables
    std::vector<std::string> shader_defines = m_compressed_segmentation_volume->getGLSLDefines();
    if (!m_release_version)
        shader_defines.emplace_back("ENALBE_CSGV_DEBUGGING");
    shader_defines.emplace_back("SEGMENTED_VOLUME_MATERIAL_COUNT=" + std::to_string(SEGMENTED_VOLUME_MATERIAL_COUNT));
    if (m_use_palette_cache)
        shader_defines.emplace_back("PALETTE_CACHE");
    if (m_decode_from_shared_memory)
        shader_defines.emplace_back("DECODE_FROM_SHARED_MEMORY");
    shader_defines.emplace_back("CACHE_MODE=" + std::to_string(m_cache_mode));
    if (m_cache_mode == CACHE_VOXELS) {
        // internally, the uvec2 are interpreted as pack64(.x, .y) uint64 values to support atomic operations
        shader_defines.emplace_back(
            "CACHE_UVEC2_SIZE=" + std::to_string(m_target_cache_size_MB * 1024 * 1024 / sizeof(glm::uvec2)));
    }
    if (m_empty_space_buffer_size > 0) {
        shader_defines.emplace_back(
            "EMPTY_SPACE_UINT_SIZE=" + std::to_string(m_empty_space_buffer_size / sizeof(uint32_t)));
    }
    shader_defines.emplace_back("SUBGROUP_SIZE=" + std::to_string(getCtx()->getPhysicalDeviceSubgroupProperties().subgroupSize));
    // if we're rendering without a GLFW window / WSI, we're disabling MultiBuffering
    if (getCtx()->getWsi())
        m_pass = std::make_unique<PassCompSegVolRender>(getCtx(), getCtx()->getWsi()->stateInFlight(), m_queue_family_index, shader_defines,
                                                        m_compressed_segmentation_volume->isUsingRandomAccess(), m_cache_mode == CACHE_BRICKS);
    else
        m_pass = std::make_unique<PassCompSegVolRender>(getCtx(), NoMultiBuffering, m_queue_family_index, shader_defines,
                                                        m_compressed_segmentation_volume->isUsingRandomAccess(), m_cache_mode == CACHE_BRICKS);
    if (!m_additional_shader_defs.empty())
        shader_defines.emplace_back(m_additional_shader_defs);

    m_pass->allocateResources();
    m_ucamera_info = m_pass->getUniformSet("camera_info");
    m_urender_info = m_pass->getUniformSet("render_info");
    m_uresolve_info = m_pass->getUniformSet("resolve_info");
    m_usegmented_volume_info = m_pass->getUniformSet("segmented_volume_info");

    // reset parameter hashes to trigger re-render
    m_presolve_hash = m_prender_hash = m_pcamera_hash = static_cast<size_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    m_pcache_reset = true;
    m_pmaterial_reset = true;
    m_accumulated_frames = 0;
    m_frame = 0u;

    // update all bindings (if buffers were already created)
    if (m_brick_starts_buffer) {
        // update all bindings
        m_pass->setStorageBuffer(0, 1, *m_brick_starts_buffer);
        m_pass->setStorageBuffer(0, 2, *m_split_encoding_buffer_addresses_buffer);
        m_pass->setStorageBuffer(0, 3, *m_cache_info_buffer);
        m_pass->setStorageBuffer(0, 4, *m_assign_info_buffer);
        m_pass->setStorageBuffer(0, 5, *m_free_stack_buffer);
        m_pass->setStorageBuffer(0, 6, *m_cache_buffer);
        if (m_compressed_segmentation_volume->isUsingSeparateDetail()) {
            m_pass->setStorageBuffer(0, 7, *m_detail_starts_buffer);
            m_pass->setStorageBuffer(0, 8, *m_detail_buffer);
            m_pass->setStorageBuffer(0, 9, *m_detail_requests_buffer);
        }
        m_pass->setStorageBuffer(0, 16, *m_gpu_stats_buffer);
        m_pass->setStorageBuffer(0, 17, *m_attribute_buffer);
        m_pass->setStorageBuffer(0, 18, *m_materials_buffer);
    }
    m_pass->setVolumeInfo(m_compressed_segmentation_volume->getBrickCount(),
                          m_compressed_segmentation_volume->getLodCountPerBrick());
}

void CompressedSegmentationVolumeRenderer::releaseShaderResources() {
    m_usegmented_volume_info = nullptr;
    m_ucamera_info = nullptr;
    m_urender_info = nullptr;
    m_uresolve_info = nullptr;
    if (m_pass)
        m_pass->freeResources();
    m_pass = nullptr;
}

void CompressedSegmentationVolumeRenderer::initSwapchainResources() {
    updateRenderResolutionFromWSI();

    // tell the pass the new invocation size
    m_pass->setImageInfo(m_resolution.width, m_resolution.height);

    // recreate all swapchain image sized textures
    AwaitableList reinitDone;
    // m_accumulation_rgba_tex[0] = m_pass->reflectTexture("accumulationIn", {.width = m_resolution.width, .height = m_resolution.height, .format = vk::Format::eR16G16B16A16Sfloat, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    // m_accumulation_rgba_tex[1] = m_pass->reflectTexture("accumulationOut", {.width = m_resolution.width, .height = m_resolution.height, .format = vk::Format::eR16G16B16A16Sfloat, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    m_accumulation_rgba_tex[0] = m_pass->reflectTexture("accumulationIn", {.width = m_resolution.width, .height = m_resolution.height, .format = vk::Format::eR32G32B32A32Sfloat, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    m_accumulation_rgba_tex[1] = m_pass->reflectTexture("accumulationOut", {.width = m_resolution.width, .height = m_resolution.height, .format = vk::Format::eR32G32B32A32Sfloat, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    for (auto &texture : m_accumulation_rgba_tex) {
        texture->ensureResources();
        const auto layoutTransformDone = texture->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eAllCommands);
        reinitDone.push_back(layoutTransformDone);
    }
    m_accumulation_samples_tex[0] = m_pass->reflectTexture("accuSampleCountIn", {.width = m_resolution.width, .height = m_resolution.height, .format = vk::Format::eR16Uint, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    m_accumulation_samples_tex[1] = m_pass->reflectTexture("accuSampleCountOut", {.width = m_resolution.width, .height = m_resolution.height, .format = vk::Format::eR16Uint, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    for (auto &texture : m_accumulation_samples_tex) {
        texture->ensureResources();
        const auto layoutTransformDone = texture->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eAllCommands);
        reinitDone.push_back(layoutTransformDone);
    }
    // TODO: use 16 bit precision for denoising buffer
    m_denoise_tex = m_pass->reflectTextureArray("denoisingBuffer", {.width = m_resolution.width, .height = m_resolution.height, .format = vk::Format::eR16G16B16A16Sfloat, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    for (auto &texture : m_denoise_tex) {
        texture->ensureResources();
        const auto layoutTransformDone = texture->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eAllCommands);
        reinitDone.push_back(layoutTransformDone);
    }
    m_inpaintedOutColor = m_pass->reflectTextures(
        "inpaintedOutColor", {.width = m_resolution.width, .height = m_resolution.height, .format = vk::Format::eR8G8B8A8Unorm, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    for (auto &texture : *m_inpaintedOutColor) {
        texture->ensureResources();
        const auto layoutTransformDone = texture->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eAllCommands);
        reinitDone.push_back(layoutTransformDone);
    }
    m_g_buffer_tex = m_pass->reflectTexture("gBuffer", {.width = m_resolution.width, .height = m_resolution.height, .format = vk::Format::eR16G16B16A16Uint, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    {
        m_g_buffer_tex->ensureResources();
        const auto layoutTransformDone = m_g_buffer_tex->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eAllCommands);
        reinitDone.push_back(layoutTransformDone);
    }

    // write all transfer function samplers once
    for (int m = 0; m < m_materials.size(); m++) {
        if (m >= m_materialTransferFunctions.size() || !m_materialTransferFunctions[m])
            updateSegmentedVolumeMaterial(m);
        else
            m_pass->setImageSamplerArray("s_transferFunctions", m, m_materialTransferFunctions[m]->texture(), vk::ImageLayout::eReadOnlyOptimal, false);
    }

    m_gui_resolution_text = "[" + std::to_string(m_resolution.width) + " x " + std::to_string(m_resolution.height) + "]";
    getCtx()->sync->hostWaitOnDevice(reinitDone);

    // trigger a temporal accumulation flush and force parameter updates
    m_presolve_hash = m_prender_hash = m_pcamera_hash = static_cast<size_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    m_accumulated_frames = 0;
    m_frame = 0u;
}

void CompressedSegmentationVolumeRenderer::releaseSwapchain() {
    if (m_mostRecentFrame.has_value()) {
        m_mostRecentFrame->texture = nullptr;
        m_mostRecentFrame->renderingComplete = {};
    }
    if (m_accumulation_rgba_tex[0])
        m_accumulation_rgba_tex[0] = nullptr;
    if (m_accumulation_rgba_tex[1])
        m_accumulation_rgba_tex[1] = nullptr;
    if (m_accumulation_samples_tex[0])
        m_accumulation_samples_tex[0] = nullptr;
    if (m_accumulation_samples_tex[1])
        m_accumulation_samples_tex[1] = nullptr;
    if (m_denoise_tex[0])
        m_denoise_tex[0] = nullptr;
    if (m_denoise_tex[1])
        m_denoise_tex[1] = nullptr;
    if (m_inpaintedOutColor)
        m_inpaintedOutColor.reset(); // = nullptr;
    if (m_g_buffer_tex)
        m_g_buffer_tex = nullptr;
}

void CompressedSegmentationVolumeRenderer::resetGPU() {
    releaseGui();
    releaseSwapchain();
    releaseShaderResources();
    releaseResources();
}

void CompressedSegmentationVolumeRenderer::updateRenderUpdateFlags() {
#define HASHP(PARAM) new_hash = hashMemory(&PARAM, sizeof(PARAM), new_hash);

    m_render_update_flags = 0u;
    size_t new_hash;

    // camera parameters
    new_hash = 0ull;
    const glm::mat4 mvp = m_camera->get_world_to_projection_space(m_resolution);
    new_hash = hashMemory(&mvp, sizeof(glm::mat4));
    if (new_hash != m_pcamera_hash) {
        m_render_update_flags |= UPDATE_PCAMERA;
        m_pcamera_hash = new_hash;
    }

    // rendering parameters
    // frame indices and seeds
    HASHP(m_subsampling)
    HASHP(m_target_accum_frames)
    // shading
    HASHP(m_factor_ambient)
    HASHP(m_light_intensity)
    HASHP(m_global_illumination_enabled)
    HASHP(m_shadow_pathtracing_ratio) HASHP(m_light_direction)
        HASHP(m_ambient_occlusion_dist_strength) HASHP(m_envmap_enabled) HASHP(m_max_path_length)
        // volume transformations
        HASHP(m_voxel_size) HASHP(m_bboxMin) HASHP(m_bboxMax) HASHP(m_axis_transpose_mat) HASHP(m_axis_flip[0])
            HASHP(m_axis_flip[1]) HASHP(m_axis_flip[2])
        // HASHP(m_subblock_start) HASHP(m_subblock_size) HASHP(m_subblock_enabled)
        // general rendering config
        HASHP(m_lod_bias) HASHP(m_max_inv_lod) HASHP(m_max_request_path_length_pow2) HASHP(m_blue_noise) HASHP(m_max_steps)
        // request limitation
        // TODO: should not reset frame accumulation. Right now the uniforms are always uploaded always at UPDATE_RENDER_FRAME
        //    HASHP(m_req_limit.area_size)
        //    if (m_req_limit.area_size > 0) {
        //        HASHP(m_req_limit.area_pos) HASHP(m_req_limit.area_min_pixel) HASHP(m_req_limit.spp_delta)
        //    }
        // debug views and flags
        const uint32_t render_debug_bits = m_debug_vis_flags & (VDEB_MODEL_SPACE_BIT | VDEB_LOD_BIT | VDEB_EMPTY_SPACE_BIT | VDEB_CACHE_VOXEL_BIT | VDEB_BRICK_IDX_BIT | VDEB_STATS_DOWNLOAD_BIT);
    HASHP(render_debug_bits)
    if (new_hash != m_prender_hash) {
        m_render_update_flags |= UPDATE_PRENDER;
        m_prender_hash = new_hash;
    }

    // material changes
    if (m_pmaterial_reset) {
        m_render_update_flags |= UPDATE_PMATERIAL;
        m_pmaterial_reset = false;
        m_render_update_flags |= UPDATE_PRENDER; // render uniform stores the max. active material index
    }

    // resolve shader parameters
    new_hash = 0ull;
    HASHP(m_background_color_a)
    HASHP(m_background_color_b)
    HASHP(m_tonemap_enabled)
    HASHP(m_exposure)
    HASHP(m_brightness) HASHP(m_contrast) HASHP(m_gamma)
        HASHP(m_atrous_iterations)
            HASHP(m_denoising_enabled) HASHP(m_atrous_enabled) HASHP(m_depth_sigma) HASHP(m_denoise_fade_sigma)
                HASHP(m_denoise_filter_kernel_size) HASHP(m_denoise_fade_enabled) HASHP(m_mouse_pos)
                    const uint32_t resolve_debug_bits = m_debug_vis_flags & (VDEB_NO_POSTPROCESS_BIT | VDEB_CACHE_ARRAY_BIT | VDEB_EMPTY_SPACE_ARRAY_BIT | VDEB_SPP_BIT | VDEB_G_BUFFER_BIT | VDEB_ENVMAP_BIT | VDEB_REQUEST_LIMIT_BIT | VDEB_BRICK_INFO_BIT);
    HASHP(resolve_debug_bits)
    if (new_hash != m_presolve_hash) {
        m_render_update_flags |= UPDATE_PRESOLVE;
        m_presolve_hash = new_hash;
    }

    // cache reset
    if (m_clear_cache_every_frame || m_pcache_reset) {
        m_render_update_flags |= UPDATE_CLEAR_CACHE;
        m_pcache_reset = false;
        if (m_cache_mode == CACHE_VOXELS) {
            // trigger empty space bit vector update as well
            m_render_update_flags |= UPDATE_PMATERIAL;
        }
    }

    // reset render frame accumulation if any parameters that influence rendering changed
    if (m_clear_accum_every_frame || (m_render_update_flags & (UPDATE_PCAMERA | UPDATE_PRENDER | UPDATE_PMATERIAL))) {
        // TODO: if m_target_accum_frames *increases*, UPDATE_PRENDER is set but accumulation should not reset
        m_render_update_flags |= UPDATE_CLEAR_ACCUM;
        m_accumulated_frames = 0u;
        // reset brick request limitation to off
        disableRequestLimiation();
    }

    // check if a new frame has to be accumulated, target frame count of 0 means: render as long as possible
    if (m_accumulated_frames < m_target_accum_frames || m_target_accum_frames <= 0u) {
        m_render_update_flags |= UPDATE_RENDER_FRAME;
        m_render_update_flags |= UPDATE_PRESOLVE;
    }

    m_pass->setRenderUpdateFlagsForNextCall(m_render_update_flags);
    m_pass->setResolvePasses(m_atrous_iterations);
#undef HASHP
}

void CompressedSegmentationVolumeRenderer::updateUniformDescriptorset() {

    // only upload descriptor sets if render or resolve passes are executed
    if (!(m_render_update_flags & (UPDATE_RENDER_FRAME | UPDATE_PRESOLVE)))
        return;

    const auto camera = getCamera();
    updateRenderResolutionFromWSI();

    glm::vec3 voldim = glm::vec3(m_compressed_segmentation_volume->getVolumeDim());
    glm::vec3 physical_voldim = voldim * m_voxel_size;

    // camera uniform
    if (m_render_update_flags & UPDATE_PCAMERA) {
        const auto world_to_projection_space = camera->get_world_to_projection_space(m_resolution);
        const auto projection_to_world_space = glm::inverse(world_to_projection_space);
        m_ucamera_info->setUniform<glm::mat4x4>("g_world_to_projection_space", world_to_projection_space);
        m_ucamera_info->setUniform<glm::mat4x4>("g_projection_to_world_space", projection_to_world_space);
        m_ucamera_info->setUniform<glm::mat4x4>("g_view_to_projection_space",
                                                camera->get_view_to_projection_space(m_resolution));
        m_ucamera_info->setUniform<glm::mat4x4>("g_projection_to_view_space",
                                                glm::inverse(camera->get_view_to_projection_space(m_resolution)));
        m_ucamera_info->setUniform<glm::mat4x4>("g_world_to_view_space", camera->get_world_to_view_space());
        m_ucamera_info->setUniform<glm::mat4x4>("g_view_to_world_space",
                                                glm::inverse(camera->get_world_to_view_space()));
        glm::mat4 projection_to_world_space_no_translation = projection_to_world_space;
        glm::vec2 viewportScale(2.0f / static_cast<float>(m_resolution.width),
                                2.0f / static_cast<float>(m_resolution.height));
        glm::mat4 pixel_to_ray_direction_projection_space({viewportScale[0], 0.0f, 0.0f, 0.0f},
                                                          {0.0f, viewportScale[1], 0.0f, 0.0f},
                                                          {0.5f * viewportScale[0] - 1.0f,
                                                           0.5f * viewportScale[1] - 1.0f, 1.0f, 1.0f},
                                                          {0.f, 0.f, 0.f, 1.f});
        m_ucamera_info->setUniform<glm::mat3x3>("g_pixel_to_ray_direction_world_space", glm::mat3x3(
                                                                                            projection_to_world_space_no_translation * pixel_to_ray_direction_projection_space));
        m_ucamera_info->setUniform<glm::vec3>("g_camera_position_world_space", camera->position_world_space);
        // the g_voxels_per_pixel_per_dist determines how many voxels an image pixel footprint overlaps for a camera distance
        // TODO: account for anisotropic voxel sizes, currently using average
        float voxels_per_pixel_at_near = (physical_voldim.x + physical_voldim.y + physical_voldim.z) / 3.f / glm::max(m_voxel_size.x, glm::max(m_voxel_size.y, m_voxel_size.z)) / float(m_resolution.height);
        m_ucamera_info->setUniform<float>("g_voxels_per_pixel_per_dist", glm::tan(this->getCamera()->vertical_fov) * voxels_per_pixel_at_near);
    }

    // resolve pass parameter uniform
    if (m_render_update_flags & UPDATE_PRESOLVE) {
        m_uresolve_info->setUniform<glm::vec4>("g_background_color_a", m_background_color_a);
        m_uresolve_info->setUniform<glm::vec4>("g_background_color_b", m_background_color_b);
        m_uresolve_info->setUniform<uint32_t>("g_tonemap_enable", m_tonemap_enabled ? ~0u : 0u);
        m_uresolve_info->setUniform<float>("g_exposure", m_exposure);
        m_uresolve_info->setUniform<float>("g_brightness", m_brightness - 1.f);
        m_uresolve_info->setUniform<float>("g_contrast", m_contrast < 1.f ? m_contrast
                                                                          : glm::pow(m_contrast, 2.f));
        m_uresolve_info->setUniform<float>("g_gamma", m_gamma);
        // denoising
        m_uresolve_info->setUniform<uint32_t>("g_denoising_enabled", m_denoising_enabled ? ~0u : 0u);
        m_uresolve_info->setUniform<uint32_t>("g_atrous_enabled", m_atrous_enabled ? ~0u : 0u);
        m_uresolve_info->setUniform<float>("g_depth_sigma", m_depth_sigma);
        m_uresolve_info->setUniform<float>("g_denoise_fade_sigma", m_denoise_fade_sigma);
        m_uresolve_info->setUniform<uint32_t>("g_denoise_fade_enable", m_denoise_fade_enabled ? ~0u : 0u);
        // automatically limit the kernel size after a certain number of frames
        const uint32_t pixels_per_sample = (1u << m_subsampling) * (1u << m_subsampling);
        m_uresolve_info->setUniform<int>("g_denoise_filter_kernel_size",
                                         glm::min(m_denoise_filter_kernel_size, (m_denoise_filter_kernel_size < 8 * pixels_per_sample) ? 3 : 1));
        m_uresolve_info->setUniform<uint32_t>("g_denoise_fade_enable", m_denoise_fade_enabled ? ~0u : 0u);
        m_uresolve_info->setUniform<glm::ivec2>("g_cursor_pixel_pos", glm::ivec2(m_mouse_pos * glm::vec2(m_resolution.width,
                                                                                                         m_resolution.height)));
        m_uresolve_info->setUniform<uint32_t>("g_debug_vis_flags", m_debug_vis_flags);
    }

    // render info uniform
    if (m_render_update_flags & (UPDATE_RENDER_FRAME | UPDATE_PRENDER)) {

        // MVP matrices, transformations, volume sizes
        float scalingFactor = glm::max(physical_voldim.x, glm::max(physical_voldim.y, physical_voldim.z));
        // size in world space: uniformly scaled so that the largest component is one
        glm::vec3 normalized_volume_size(physical_voldim / scalingFactor);
        m_urender_info->setUniform<glm::vec3>("g_voxel_size", m_voxel_size);
        m_urender_info->setUniform<glm::vec3>("g_physical_vol_dim", physical_voldim);
        m_urender_info->setUniform<glm::vec3>("g_normalized_volume_size", normalized_volume_size);
        // Transformation matrices:
        // In world space, everything should be a cuboid with the largest dimension being one, centered around the origin.
        // In model space, one voxel must be a unit cube. The normalization transform scales this down to world space [-0.5, 0.5]^3
        glm::mat4 world_to_model_space;
        world_to_model_space = glm::translate(glm::scale(glm::mat4(1.f), glm::vec3(voldim) / glm::vec3(normalized_volume_size)), glm::vec3(normalized_volume_size / 2.f)) * m_axis_transpose_mat;
        m_urender_info->setUniform<glm::mat4x4>("g_model_to_world_space", glm::inverse(world_to_model_space));
        m_urender_info->setUniform<glm::mat4x4>("g_world_to_model_space", world_to_model_space);
        m_urender_info->setUniform<glm::mat3x3>("g_model_to_world_space_dir", glm::mat3(glm::inverse(world_to_model_space)));
        m_urender_info->setUniform<glm::mat3x3>("g_world_to_model_space_dir", glm::mat3(world_to_model_space));
        m_urender_info->setUniform<float>("g_world_to_model_space_scaling", scalingFactor);
        // bbox is the volume dimension in voxels centered around the origin (if no bbox reduction is applied)
        m_urender_info->setUniform<glm::ivec4>("g_bboxMin", glm::uvec4(glm::clamp(m_bboxMin, glm::ivec3(0), glm::ivec3(voldim)), 1));
        m_urender_info->setUniform<glm::ivec4>("g_bboxMax", glm::uvec4(glm::clamp(m_bboxMax, glm::ivec3(0), glm::ivec3(voldim)), 1));

        // frame indices and seeds
        m_urender_info->setUniform<uint32_t>("g_frame", m_frame);
        m_urender_info->setUniform<uint32_t>("g_camera_still_frames", m_accumulated_frames);
        m_urender_info->setUniform<uint32_t>("g_target_accum_frames", glm::max(0, m_target_accum_frames));
        m_urender_info->setUniform<float>("g_cache_fill_rate", getCacheFillRate());
        // the sample counts of the min. and max. spp values are in the 16 MSB of the gpu_stats values
        m_urender_info->setUniform<glm::uvec2>("g_min_max_spp", glm::uvec2(m_last_gpu_stats.min_spp_and_pixel >> 48,
                                                                           m_last_gpu_stats.max_spp_and_pixel >> 48));
        m_urender_info->setUniform<glm::ivec2>("g_min_spp_pixel", glm::ivec2(m_last_gpu_stats.min_spp_and_pixel & 0xFFFF,
                                                                             (m_last_gpu_stats.min_spp_and_pixel >> 16) & 0xFFFF));
        m_urender_info->setUniform<int32_t>("g_req_limit_area_size", m_req_limit.area_size);
        m_urender_info->setUniform<glm::ivec2>("g_req_limit_area_pos", m_req_limit.area_pos);
        m_urender_info->setUniform<glm::ivec2>("g_req_limit_area_pixel", m_req_limit.area_min_pixel);
        m_urender_info->setUniform<uint32_t>("g_req_limit_spp_delta", m_req_limit.spp_delta);
        m_urender_info->setUniform<uint32_t>("g_swapchain_index", m_pass->getActiveIndex());
        m_urender_info->setUniform<int32_t>("g_subsampling", (1 << m_subsampling));
        m_urender_info->setUniform<glm::ivec2>("g_subsampling_pixel", PixelSequence::bitReverseMortonNxNVec(m_subsampling)[m_accumulated_frames % ((1 << m_subsampling) * (1 << m_subsampling))]);
        // the random seed should only change every other frame to allow requested bricks to be decoded for path tracing
        const uint32_t pixels_in_subsampling_block = (1 << m_subsampling);
        const uint32_t random_seed = (m_accumulated_frames / 2 / pixels_in_subsampling_block) << 16u ^ (m_accumulated_frames / 2 / pixels_in_subsampling_block);
        m_urender_info->setUniform<uint32_t>("g_random_seed", random_seed);
        // shading
        m_urender_info->setUniform<float>("g_factor_ambient", m_factor_ambient);
        m_urender_info->setUniform<float>("g_light_intensity", m_light_intensity);
        m_urender_info->setUniform<uint32_t>("g_global_illumination_enable", m_global_illumination_enabled ? ~0u : 0u);
        m_urender_info->setUniform<float>("g_shadow_pathtracing_ratio", m_shadow_pathtracing_ratio);
        m_urender_info->setUniform<glm::vec3>("g_light_direction", glm::normalize(glm::mat3(world_to_model_space) * m_light_direction));
        m_urender_info->setUniform<uint32_t>("g_envmap_enable", m_envmap_enabled ? ~0u : 0u);
        m_urender_info->setUniform<int32_t>("g_max_path_length", m_max_path_length);

        // materials
        int max_active_material = -1;
        for (int m = 0; m < m_materials.size(); m++)
            if (m_materials[m].isActive())
                max_active_material = m;
        m_urender_info->setUniform<int32_t>("g_max_active_material", max_active_material);

        // general render config
        m_urender_info->setUniform<uint32_t>("g_detail_buffer_dirty", m_detail_stage == DetailUploading ? 1u : 0u);
        m_urender_info->setUniform<float>("g_lod_bias", m_lod_bias);
        auto lod_count = m_compressed_segmentation_volume->getLodCountPerBrick();
        m_urender_info->setUniform<uint32_t>("g_max_inv_lod", glm::min(static_cast<uint32_t>(m_max_inv_lod), lod_count - 1u));
        m_urender_info->setUniform<int32_t>("g_max_request_path_length", (1 << m_max_request_path_length_pow2) - 1);
        m_urender_info->setUniform<int32_t>("g_maxSteps", m_max_steps);
        m_urender_info->setUniform<uint32_t>("g_blue_noise_enable", m_blue_noise ? ~0u : 0u);
    }

    // static segmentation volume and buffer metadata uniform
    if (m_frame == 0u) {
        m_usegmented_volume_info->setUniform<glm::uvec3>("g_vol_dim", m_compressed_segmentation_volume->getVolumeDim());
        m_usegmented_volume_info->setUniform<glm::uvec3>("g_brick_count", m_compressed_segmentation_volume->getBrickCount());
        m_usegmented_volume_info->setUniform<uint32_t>("g_brick_idx_count", m_compressed_segmentation_volume->getBrickIndexCount());
        // cache management
        m_usegmented_volume_info->setUniform<uint32_t>("g_free_stack_capacity", m_free_stack_capacity);
        m_usegmented_volume_info->setUniform<uint32_t>("g_cache_capacity", m_cache_capacity);
        m_usegmented_volume_info->setUniform<uint32_t>("g_cache_base_element_uints", m_cache_base_element_uints);
        m_usegmented_volume_info->setUniform<uint32_t>("g_cache_indices_per_uint", m_cache_indices_per_uint);
        m_usegmented_volume_info->setUniform<uint32_t>("g_cache_palette_idx_bits", m_cache_palette_idx_bits);
        // encoding and detail encoding buffer management
        m_usegmented_volume_info->setUniform<uint32_t>("g_brick_idx_to_enc_vector", m_compressed_segmentation_volume->getBrickIdxToEncVectorMapping());
        m_usegmented_volume_info->setUniform<glm::uvec2>("g_cache_buffer_address", m_cache_buffer_address);
        m_usegmented_volume_info->setUniform<glm::uvec2>("g_empty_space_bv_address", m_empty_space_buffer_address);
        if (m_empty_space_buffer_size > 0) {
            if (m_empty_space_block_dim == 0u)
                throw std::runtime_error("empty space block size cannot be 0");
            if (m_empty_space_block_dim > m_compressed_segmentation_volume->getBrickSize())
                throw std::runtime_error("empty space block dimension must not be greater than the brick size");

            m_usegmented_volume_info->setUniform<uint32_t>("g_empty_space_block_dim", m_empty_space_block_dim);
            uint32_t empty_space_set_size = m_empty_space_block_dim * m_empty_space_block_dim * m_empty_space_block_dim;
            m_usegmented_volume_info->setUniform<uint32_t>("g_empty_space_set_size", empty_space_set_size);
            // round up to get the number of empty space skipping cells
            const glm::uvec3 empty_space_dim = (m_compressed_segmentation_volume->getVolumeDim() + glm::uvec3(m_empty_space_block_dim - 1u)) / m_empty_space_block_dim;
            const glm::uvec3 empty_space_dot_map = {1, empty_space_dim.x, empty_space_dim.x * empty_space_dim.y};
            assert(static_cast<size_t>(empty_space_dim.x) * empty_space_dim.y * empty_space_dim.z <= 0xFFFFFFFFull && "empty space dim too large to be indexed in 32 bit. decrease empty space block size.");
            m_usegmented_volume_info->setUniform<glm::uvec3>("g_empty_space_dot_map", empty_space_dot_map);
        } else {
            m_usegmented_volume_info->setUniform<uint32_t>("g_empty_space_block_dim", 0u);
            m_usegmented_volume_info->setUniform<uint32_t>("g_empty_space_set_size", 0u);
            m_usegmented_volume_info->setUniform<glm::uvec3>("g_empty_space_dot_map", glm::uvec3{0});
        }
        m_usegmented_volume_info->setUniform<glm::uvec2>("g_detail_buffer_address", m_detail_buffer_address);
        m_usegmented_volume_info->setUniform<uint32_t>("g_request_buffer_capacity", m_max_detail_requests_per_frame);
    }
}

void CompressedSegmentationVolumeRenderer::initGui(vvv::GuiInterface *gui) {
    if (!m_compressed_segmentation_volume)
        throw std::runtime_error("CompressedSegmentationVolume must be set before calling initGui");
    if (!m_csgv_db)
        throw std::runtime_error("CompressedSegmentationVolume database must be set before calling initGui");

    Renderer::initGui(gui);
    // Note: the windows are created in this order in ImGui. the later windows are selected before the earlier ones.
    GuiInterface::GuiElementList *g_gen = gui->get("General");
    GuiInterface::GuiElementList *g_dis = gui->get("Display");
    GuiInterface::GuiElementList *g_render = gui->get("Rendering");
    GuiInterface::GuiElementList *g_dev = gui->get("Development");
    GuiInterface::GuiElementList *g_mat = gui->get("Materials");

    // we create an invisible GUI window to export all parameters but keep them hidden from the user
    gui->getWindow("Development")->setVisible(!m_release_version);
    // specify a docking layout for the windows
    gui->setDockingLayout({
        {"General", "d"},
        {"Rendering", "d"},
        {"Display", "d"},
        {"Materials", "r"},
        {"Development", "Materials"},
    });

#ifdef IMGUI
#define GUI_SAME_LINE(G) G->addCustomCode([]() { ImGui::SameLine(); }, "");
#else
#define GUI_SAME_LINE(G)
#endif

    // General options
    static int gui_preset_selection = 0;
    std::vector<std::string> vcfg_preset_names;
    std::ranges::transform(m_data_vcfg_presets, std::back_inserter(vcfg_preset_names),
                           [](const std::pair<std::string, std::filesystem::path> &c) { return c.first; });
    g_gen->addCombo(&gui_preset_selection, vcfg_preset_names, [this](int i, bool by_user) {
                           // if this combo is updated form a vcfg parameter file import, do not change the parameters
                           if (!by_user)
                               return;

                            // how many frames have to be accumulated so that each pixel received one sample
                            if (i >= 0 && i < m_data_vcfg_presets.size())
                                readParameterFile(m_data_vcfg_presets[i].second.generic_string());

                            // compute min. required number of total accumulation samples per pixel
                            if (m_target_accum_frames > 0) {
                                const int frames_for_one_spp = (1 << m_subsampling) * (1 << m_subsampling);
                                if (m_global_illumination_enabled && m_shadow_pathtracing_ratio > 0.) {
                                    // ambient occlusion / path tracing need some frames to converge
                                    m_target_accum_frames = glm::max(m_target_accum_frames, frames_for_one_spp * 512);
                                } else {
                                    // everything deterministic, nees only AA and iterative brick decoding
                                    m_target_accum_frames = glm::max(m_target_accum_frames, frames_for_one_spp * 64);
                                }
                            } }, "Rendering Preset");
    g_gen->addAction([this]() {
#ifdef HEADLESS
        exportCurrentFrameToImage("./volcanite_output.png");
#else
        if (!pfd::settings::available()) {
            Logger(Warn) << "Can not open file dialog for screenshot export. Using default file ./volcanite_output.png";
            exportCurrentFrameToImage("./volcanite_output.png");
            return;
        }

        // Open a file dialog to choose a file
        auto selected_file = pfd::save_file("Save Screenshot", Paths::getHomeDirectory().string() + "/*",
                                            {"Image File (.png .jpg .jpeg)", "*.png *.jpg *.jpeg", "All Files", "*"});
        if (!selected_file.result().empty()) {
            exportCurrentFrameToImage(selected_file.result());
        }
#endif
    },
                     "Screenshot");
#ifndef HEADLESS
    GUI_SAME_LINE(g_gen)
    g_gen->addAction([this]() {
        std::string file;
        if (!pfd::settings::available()) {
            Logger(Warn) << "Can not open file dialog. Using default file ./parameters.vcfg";
            file = "./parameters.vcfg";
        }

        // Open a file dialog to choose a file
        auto selected_file = pfd::open_file("Import Parameters", Paths::getHomeDirectory().string() + "/*",
                                            {"Parameter Config (.vcfg)", "*.vcfg", "All Files", "*"});
        if (!selected_file.result().empty()) {
            file = selected_file.result().at(0);
            readParameterFile(file, VOLCANITE_VERSION);
        }
    },
                     "Import Parameters");
    GUI_SAME_LINE(g_gen)
    g_gen->addAction([this]() {
        std::string file;
        if (!pfd::settings::available()) {
            Logger(Warn) << "Can not open file dialog. Using default file ./parameters.vcfg";
            file = "./parameters.vcfg";
        }

        // Open a file dialog to choose a file
        auto selected_file = pfd::save_file("Export Parameters", Paths::getHomeDirectory().string() + "/*",
                                            {"Parameter Config (.vcfg)", "*.vcfg", "All Files", "*"});
        if (!selected_file.result().empty())
            file = selected_file.result();

        if (!file.ends_with(".vcfg"))
            file.append(".vcfg");

        if (!writeParameterFile(file, VOLCANITE_VERSION))
            Logger(Warn) << "could not write vcfg file " << file;
    },
                     "Export Parameters");
#endif // not HEADLESS
    GUI_SAME_LINE(g_gen)
    g_gen->addAction([this]() { getCamera()->reset(); }, "Reset Camera");
    GUI_SAME_LINE(g_gen)
    g_gen->addAction([this]() { getCamera()->position_look_at_world_space = {0, 0, 0}; }, "Center Camera");
    g_gen->addSeparator();
    const glm::uvec3 vol_dim = m_compressed_segmentation_volume->getVolumeDim();
    g_gen->addVec3([this](glm::vec3 v) { if (glm::all(glm::greaterThan(v, glm::vec3(0.f)))) m_voxel_size = v; },
                   [this]() { return m_voxel_size; }, "Voxel Size", 3);
    g_gen->addIntRange([this](glm::ivec2 v) {m_bboxMin.x = v.x; m_bboxMax.x = v.y; },
                       [this]() { return glm::ivec2(m_bboxMin.x, m_bboxMax.x); },
                       "Splitting Plane X", glm::ivec2(0), glm::ivec2(vol_dim.x), glm::ivec2(1));
    g_gen->addIntRange([this](glm::ivec2 v) {m_bboxMin.y = v.x; m_bboxMax.y = v.y; },
                       [this]() { return glm::ivec2(m_bboxMin.y, m_bboxMax.y); },
                       "Splitting Plane Y", glm::ivec2(0), glm::ivec2(vol_dim.y), glm::ivec2(1));
    g_gen->addIntRange([this](glm::ivec2 v) {m_bboxMin.z = v.x; m_bboxMax.z = v.y; },
                       [this]() { return glm::ivec2(m_bboxMin.z, m_bboxMax.z); },
                       "Splitting Plane Z", glm::ivec2(0), glm::ivec2(vol_dim.z), glm::ivec2(1));
    static int gui_transpose_selection = 0;
    // TODO: GUI Combo arrays could become static constexpr with lambda init
    static std::vector<glm::ivec3> gui_transpose_order;
    constexpr std::pair<char, glm::vec4> __gui_transpose_xyz[3] = {std::make_pair<char, glm::vec4>('X', {1.f, 0.f, 0.f, 0.f}),
                                                                   std::make_pair<char, glm::vec4>('Y', {0.f, 1.f, 0.f, 0.f}),
                                                                   std::make_pair<char, glm::vec4>('Z', {0.f, 0.f, 1.f, 0.f})};
    std::vector<std::string> gui_transpose_strings;
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) {
            for (int c = 0; c < 3; c++) {
                if (b == a || (c == a || c == b))
                    continue;
                gui_transpose_strings.emplace_back("XYZ");
                gui_transpose_strings.back()[0] = __gui_transpose_xyz[a].first;
                gui_transpose_strings.back()[1] = __gui_transpose_xyz[b].first;
                gui_transpose_strings.back()[2] = __gui_transpose_xyz[c].first;
                gui_transpose_order.emplace_back(glm::ivec3{a, b, c});
            }
        }
    }
    constexpr auto __get_axis_tranpose_mat = [__gui_transpose_xyz](glm::ivec3 order, bool axis_flip[3]) -> glm::mat4 {
        return glm::mat4{__gui_transpose_xyz[order[0]].second * (axis_flip[0] ? -1.f : 1.f),
                         __gui_transpose_xyz[order[1]].second * (axis_flip[1] ? -1.f : 1.f),
                         __gui_transpose_xyz[order[2]].second * (axis_flip[2] ? -1.f : 1.f),
                         glm::vec4{0.f, 0.f, 0.f, 1.f}};
    };
    g_gen->addLabel("Flip");
    GUI_SAME_LINE(g_gen)
    g_gen->addBool([this, __get_axis_tranpose_mat](bool v) {
            m_axis_flip[0] = v;
            m_axis_transpose_mat = __get_axis_tranpose_mat(gui_transpose_order[gui_transpose_selection], m_axis_flip); }, [this]() { return m_axis_flip[0]; }, "X Axis");
    GUI_SAME_LINE(g_gen)
    g_gen->addBool([this, __get_axis_tranpose_mat](bool v) {
            m_axis_flip[1] = v;
            m_axis_transpose_mat = __get_axis_tranpose_mat(gui_transpose_order[gui_transpose_selection], m_axis_flip); }, [this]() { return m_axis_flip[1]; }, "Y Axis");
    GUI_SAME_LINE(g_gen)
    g_gen->addBool([this, __get_axis_tranpose_mat](bool v) {
            m_axis_flip[2] = v;
            m_axis_transpose_mat = __get_axis_tranpose_mat(gui_transpose_order[gui_transpose_selection], m_axis_flip); }, [this]() { return m_axis_flip[2]; }, "Z Axis");
    g_gen->addCombo(&gui_transpose_selection, gui_transpose_strings, [this, __get_axis_tranpose_mat](int i, bool by_user) {
                        assert(gui_transpose_selection < gui_transpose_order.size() && "GUI sets out of bounds transpose matrix.");
                        m_axis_transpose_mat = __get_axis_tranpose_mat(gui_transpose_order[gui_transpose_selection], m_axis_flip); }, "Axis Order");
    g_gen->addSeparator();
    g_gen->addDynamicText(&m_gui_device_mem_text);
    g_gen->addDynamicText(&m_gui_cache_mem_text);

    // Display properties and render resolution
    g_dis->addColor(&m_background_color_a, "Background Color A");
    g_dis->addColor(&m_background_color_b, "Background Color B");
    g_dis->addBool([this](bool b) { if(getCtx()->getWsi()) getCtx()->getWsi()->setWindowResizable(b); }, [this]() { return getCtx()->getWsi() != nullptr && getCtx()->getWsi()->isWindowResizable(); }, "Resizable Window");
    GUI_SAME_LINE(g_dis)
    g_dis->addAction([this]() { getCtx()->getWsi()->setWindowSize(1920, 1080); }, "1920x1080 FullHD");
    GUI_SAME_LINE(g_dis)
    g_dis->addAction([this]() { getCtx()->getWsi()->setWindowSize(3840, 2160); }, "3840x2160 4K");
    GUI_SAME_LINE(g_dis)
    g_dis->addDynamicText(&m_gui_resolution_text);
    //     g_dis->addAction([this]() { getCtx()->getWsi()->setWindowSize(1080, 1920); }, "1080x1920 FullHD");
    //     GUI_SAME_LINE(g_dis)
    //     g_dis->addAction([this]() { getCtx()->getWsi()->setWindowSize( 2160, 3840); }, "2160x3840 4K");
    //     g_dis->addAction([this]() { getCamera()->orbital = !getCamera()->orbital; getCamera()->reset(); }, "Switch Camera Mode");
    g_dis->addSeparator();
    g_dis->addInt(&m_target_accum_frames, "Accumulation Frames");
    g_dis->addProgress([this]() { return m_target_accum_frames > 0u ? static_cast<float>(m_accumulated_frames) / static_cast<float>(m_target_accum_frames)
                                                                    : -static_cast<float>(m_accumulated_frames); }, "Progress");
    g_dis->addLabel("Performance Optimization");
    g_dis->addInt(&m_subsampling, "Resolution Subsampling", 0, 3, 1);
    g_dis->addInt(&m_max_request_path_length_pow2, "Decompression Path Length", 0, 5, 1);
    g_dis->addFloat(&m_lod_bias, "Decompression LOD Bias", -8.f, 8.f, 0.1f, 1.f);

    // Materials
    g_mat->addTFSegmentedVolume(&m_materials, m_csgv_db->getAttributeNames(), m_csgv_db->getAttributeMinMax(), [this](int m) { updateSegmentedVolumeMaterial(m); }, "Materials");

    // Path Tracing / Rendering
    g_render->addFloat(&m_factor_ambient, "Constant Color", 0.0f, 1.f, 0.05f, 2);
    g_render->addSeparator();
    g_render->addFloat(&m_light_intensity, "Light Intensity", 0.f, 4.f, 0.05f, 2);
    g_render->addDirection(&m_light_direction, m_camera.get(), "Light Direction");
    g_render->addSeparator();
    g_render->addBool(&m_global_illumination_enabled, "Global Illumination");
    GUI_SAME_LINE(g_render)
    g_render->addBool(&m_envmap_enabled, "Environment Map");
    g_render->addFloat(&m_shadow_pathtracing_ratio, "Directional <> Ambient", 0.f, 1.f, 0.05f, 2);
    g_render->addInt(&m_max_path_length, "Path Length", 1, 32, 1);
    g_render->addSeparator();
    g_render->addBool(&m_denoising_enabled, "Denoising");
    GUI_SAME_LINE(g_render)
    g_render->addBool(&m_tonemap_enabled, "Tone Mapping");
    g_render->addAction([this]() { m_exposure = 1.f; }, "Reset##Exposure");
    GUI_SAME_LINE(g_render)
    g_render->addFloat(&m_exposure, "Exposure", 0.f, 8.f, 0.01f);
    g_render->addAction([this]() { m_brightness = 1.f; }, "Reset##Brightness");
    GUI_SAME_LINE(g_render)
    g_render->addFloat(&m_brightness, "Brightness", 0.f, 2.f, 0.01f);
    g_render->addAction([this]() { m_contrast = 1.f; }, "Reset##Contrast");
    GUI_SAME_LINE(g_render)
    g_render->addFloat(&m_contrast, "Contrast", 0.f, 2.f, 0.01f);
    g_render->addAction([this]() { m_gamma = 1.f; }, "Reset##Gamma");
    GUI_SAME_LINE(g_render)
    g_render->addFloat(&m_gamma, "Gamma", 0.f, 4.f, 0.01f);

    // Post-Processing
    g_dev->addLabel("Post Processing");
    g_dev->addBool(&m_atrous_enabled, "Enable À-Trous Post-Processing");
    g_dev->addInt(&m_atrous_iterations, "Post-Process Iterations", 1, 4, 1);
    g_dev->addInt(&m_denoise_filter_kernel_size, "Post-Process Kernel Size", 0, 3, 1);
    g_dev->addSeparator();
    g_dev->addLabel("Denoising");
    g_dev->addFloat(&m_depth_sigma, "Denoise Depth Sigma", 0.001f, 100.f, 0.01, 3);
    g_dev->addBool(&m_denoise_fade_enabled, "Denoise Fade Out");
    GUI_SAME_LINE(g_dev)
    g_dev->addFloat(&m_denoise_fade_sigma, "Denoise Fade Sigma", 0.00f, 10.f, 0.01, 2);

    // Development
    g_dev->addLabel("Ray Traversal");
    g_dev->addBool(&m_blue_noise, "Blue Noise Shift");
    g_dev->addInt(&m_max_steps, "Max. DDA Steps", 16, 1 << 16u, 16);
    g_dev->addInt(&m_max_inv_lod, "Max. Decoding LoD", 0, 8, 1);
    g_dev->addSeparator();
    g_dev->addLabel("Debug Views");
    const std::vector<std::string> option_labels = {"Model Space", "Level-of-Detail", "Empty Space", "Brick Index",
                                                    "Label Cache", "Raw Render", "Cache Buffer",
                                                    "Request Limitation", "Brick Info State",
                                                    "Empty Space Buffer", "G-Buffer", "Samples/Pixel",
                                                    "Environment Map", "Print Statistics"};
    const std::vector<uint32_t> option_bits = {VDEB_MODEL_SPACE_BIT, VDEB_LOD_BIT, VDEB_EMPTY_SPACE_BIT, VDEB_BRICK_IDX_BIT,
                                               VDEB_CACHE_VOXEL_BIT, VDEB_NO_POSTPROCESS_BIT, VDEB_CACHE_ARRAY_BIT,
                                               VDEB_REQUEST_LIMIT_BIT, VDEB_BRICK_INFO_BIT,
                                               VDEB_EMPTY_SPACE_ARRAY_BIT, VDEB_G_BUFFER_BIT, VDEB_SPP_BIT,
                                               VDEB_ENVMAP_BIT, VDEB_STATS_DOWNLOAD_BIT};
    g_dev->addBitFlags(&m_debug_vis_flags, option_labels, option_bits, true, "Debug View");
    g_dev->addSeparator();
    g_dev->addLabel("Label Cache Management");
    g_dev->addBool(&m_req_limit.g_enable, "Auto Cache Request Limitation");
    g_dev->addInt(&m_req_limit.spp_delta, "Allowed SPP Difference", 2, 512, 1);
    g_dev->addInt(&m_req_limit.g_area_size_min, "Area Min. Size", 1, 4096, 1);
    g_dev->addIntRange(&m_req_limit.g_area_duration_bounds, "Area Min. Duration", glm::ivec2(2), glm::ivec2(200), glm::ivec2(1));
    g_dev->addAction([this]() {m_req_limit.random_area_pixel = true;
                                                m_req_limit.area_duration = m_req_limit.g_area_duration_bounds.x; },
                     "Trigger Random Requests");
    g_dev->addBool(&m_auto_cache_reset, "Auto Cache Defragmentation");
    g_dev->addBool([this](bool v) { m_pass->setCacheStagesEnabled(!v); },
                   [this]() { return !m_pass->getCacheStagesEnabled(); }, "Freeze Cache");
    g_dev->addBool(&m_clear_cache_every_frame, "Every Frame##cache");
    GUI_SAME_LINE(g_dev)
    g_dev->addAction([this]() { m_pcache_reset = true; }, "Clear Label Cache");
    g_dev->addSeparator();
    g_dev->addLabel("Framebuffer Accumulation");
    g_dev->addBool(&m_clear_accum_every_frame, "Every Frame##framebuffer");
    GUI_SAME_LINE(g_dev)
    g_dev->addAction([this]() { m_presolve_hash = m_prender_hash = m_pcamera_hash = static_cast<size_t>(
                                    std::chrono::high_resolution_clock::now().time_since_epoch().count()); }, "Clear Accumulation");
    g_dev->addBool(&m_accum_step_mode, "Step Accumulation");
    GUI_SAME_LINE(g_dev)
    g_dev->addAction([this]() { m_accum_do_step = m_accum_step_mode; }, "Next Step");
    g_dev->addAction([this]() { m_pcache_reset = true;
        m_presolve_hash = m_prender_hash = m_pcamera_hash = static_cast<size_t>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count()); }, "Clear Cache and Accumulation");
    GUI_SAME_LINE(g_dev)
    g_dev->addAction([this]() { printGPUMemoryUsage(); }, "Print GPU Memory Usage");

    // import initial config (if requested)
    if (m_init_vcfg_file.has_value())
        CompressedSegmentationVolumeRenderer::readParameterFile(m_init_vcfg_file.value().generic_string());
}

void CompressedSegmentationVolumeRenderer::updateDeviceMemoryUsage() {
    auto [avail_heap, used_heap] = getMemoryHeapBudgetAndUsage(*getCtx());
    const size_t total = getMemoryHeapSize(*getCtx());
    std::stringstream ss;
    ss.precision(4);
    ss << "GPU Memory: " << static_cast<float>(used_heap) / 1073741824.f << "/"
       << static_cast<float>(avail_heap) / 1073741824.f << "/"
       << static_cast<float>(total) / 1073741824.f << " GB (used/avail/total)";
    m_gui_device_mem_text = ss.str();
    ss = {};
    const size_t cache_total_bytes = m_cache_buffer ? m_cache_buffer->getByteSize() : 0ull;
    if (m_last_gpu_stats.used_cache_base_elements > 0u) {
        const size_t cache_occupied_bytes = (m_last_gpu_stats.used_cache_base_elements * m_cache_base_element_uints * sizeof(uint32_t));
        ss << "Cache Usage: " << cache_occupied_bytes * BYTE_TO_MB
           << " / " << cache_total_bytes * BYTE_TO_MB << " MB ("
           << 100.f * static_cast<float>(cache_occupied_bytes) / static_cast<float>(cache_total_bytes) << "%) " << getCacheFillRate() * 100.f;
    } else {
        ss << "Cache Usage: ...";
    }
    if (const uint32_t global_max_spp = static_cast<uint32_t>(m_last_gpu_stats.max_spp_and_pixel >> 48u);
        global_max_spp < 0xFFFF) {
        ss << " | SPP min/max: " << static_cast<uint32_t>(m_last_gpu_stats.min_spp_and_pixel >> 48u)
           << " / " << global_max_spp;
    }
    m_gui_cache_mem_text = ss.str();
}

void CompressedSegmentationVolumeRenderer::updateSegmentedVolumeMaterial(int m) {
    if (m_materialTransferFunctions.size() < m_materials.size())
        m_materialTransferFunctions.resize(m_materials.size(), nullptr);

    // mark material dirty
    // TODO: changing colormaps etc. does not change the visibility of a material. track UPDATE_PVISIBILITY separately
    m_pmaterial_reset = true;
    m_gpu_material_changed[m] = true;
}

vvv::AwaitableList CompressedSegmentationVolumeRenderer::updateAttributeBuffers() {
    // TODO: attribute upload could be cleaned up. Encapsulate attribute / material / data buffers in another struct
    //  or class at least. And see the notes regarding the attribute upload below.
    if (!m_csgv_db)
        throw std::runtime_error("Missing csgv database at attribute buffer creation.");

    // check which attributes should be present in GPU memory
    std::vector<bool> attributeNeeded(m_attribute_start_position.size(), false);
    for (int m = 0; m < m_materials.size(); m++) {
        if (m_materials[m].discrAttribute > 0)
            attributeNeeded[m_materials[m].discrAttribute] = true;
        if (m_materials[m].tfAttribute > 0)
            attributeNeeded[m_materials[m].tfAttribute] = true;
    }

    // check at which positions in the attribute buffer an element starts
    bool nothingToDo = true;
    const int numberOfSlots = m_max_attribute_buffer_size / sizeof(float) / m_csgv_db->getLabelCount();
    int requiredSlots = 0;
    std::vector<int> possiblePositions(numberOfSlots, -1);
    for (int a = 0; a < m_csgv_db->getAttributeCount(); a++) {
        if (attributeNeeded[a]) {
            requiredSlots++;

            if (m_attribute_start_position[a] >= 0) {
                assert(possiblePositions.at(m_attribute_start_position[a] / m_csgv_db->getLabelCount()) < 0 && "two attributes were assigned to the same position in the attribute buffer");
                possiblePositions.at(m_attribute_start_position[a] / m_csgv_db->getLabelCount()) = a;
            } else {
                nothingToDo = false;
            }
        } else
            m_attribute_start_position[a] = -1;
    }

    if (nothingToDo)
        return {};

    assert(m_attribute_start_position[0] == -1 && "first attribute (csgv_id) should not be uploaded to the GPU");

    if (requiredSlots > numberOfSlots)
        throw ::std::runtime_error("attribute buffer is not large enough with " + std::to_string(numberOfSlots) + " out of " + std::to_string(requiredSlots) + " required slots");

    // store all attributes back to back (uploads the full buffer every time)
    std::vector<float> attributes(numberOfSlots * m_csgv_db->getLabelCount());

    // put attributes in available slots
    for (int a = 0; a < m_csgv_db->getAttributeCount(); a++) {
        if (attributeNeeded[a] && m_attribute_start_position[a] < 0) {
            for (int p = 0; p < possiblePositions.size(); p++) {
                if (possiblePositions[p] < 0) {
                    m_attribute_start_position[a] = static_cast<int>(p * m_csgv_db->getLabelCount());
                    possiblePositions[p] = a;
                    break;
                }
            }
            if (m_attribute_start_position[a] < 0)
                Logger(Warn) << "could not find an attribute slot for attribute " << m_csgv_db->getAttributeNames()[a] << " in " << numberOfSlots << " slots";
        }
        // copy attribute to buffer
        if (m_attribute_start_position[a] >= 0) {
            m_csgv_db->getAttribute(a, &attributes[m_attribute_start_position[a]], attributes.size() - m_attribute_start_position[a]);
        }
    }

    // trigger parameter update and accumulation buffer reset
    m_pcamera_hash = static_cast<size_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    m_prender_hash = m_pcamera_hash;
    m_presolve_hash = m_pcamera_hash;
    m_pmaterial_reset = true;

    auto [attr_upload_finished, _attr_staging_buffer] = m_attribute_buffer->uploadWithStagingBuffer(attributes.data(), attributes.size() * sizeof(float), {.queueFamily = m_queue_family_index});
    getCtx()->sync->hostWaitOnDevice({attr_upload_finished});
    // can't just return the awaitable (return {attr_upload_finished}) as _attr_staging_buffer can not be freed yet
    return {};
}

void CompressedSegmentationVolumeRenderer::disableRequestLimiation() {
    // turn request limitation off
    m_req_limit.area_size = 0;
    m_req_limit.area_pos = {0, 0};
    m_req_limit.area_duration = m_req_limit.g_area_duration_bounds.x;
    m_req_limit.area_start_frame = m_accumulated_frames;
}

void CompressedSegmentationVolumeRenderer::updateRequestLimiation(const uint32_t global_min_spp,
                                                                  const uint32_t global_max_spp) {
    const uint32_t subsampling_pixels = (1u << m_subsampling) * (1u << m_subsampling);

    // disable request limiation if the pixel with the lowest number af samples did catch up with the max. SPP
    if (global_min_spp + m_req_limit.spp_delta >= global_max_spp) {
        disableRequestLimiation();
    }
    // enable request limitation if:
    // (0) it is not already active
    // (1) some frames were rendered already
    // (2) some pixels did not receive enough samples
    // (3) the cache is almost full
    else if (m_req_limit.area_size == 0 && m_accumulated_frames >= m_req_limit.area_start_frame + subsampling_pixels * m_req_limit.area_duration && (global_min_spp + m_req_limit.spp_delta < global_max_spp) && getCacheFillRate() > 0.90f) {
        m_req_limit.tried_cache_reset = false;
        m_req_limit.random_area_pixel = false,
        m_req_limit.area_size = 1 << glm::findMSB(glm::max(m_resolution.width, m_resolution.height));
        m_req_limit.area_pos = {0, 0};
        m_req_limit.area_start_frame = m_accumulated_frames;
        m_req_limit.area_min_pixel_last_spp = global_min_spp;
        m_req_limit.area_min_pixel = m_req_limit.global_min_pixel;
        m_req_limit.area_duration = m_req_limit.g_area_duration_bounds.x;
    }
    // if a brick request limitation is already set: move the AABB in which pixels can request bricks around
    // when a new area configuration is set, area_duration frames are accumulated to see if it was effective
    else if (m_req_limit.area_size > 0 && m_accumulated_frames >= m_req_limit.area_start_frame + subsampling_pixels * m_req_limit.area_duration) {
        m_req_limit.area_start_frame = m_accumulated_frames;

        // after area_duration frames, this area should have received new samples
        const int new_samples_rendered = static_cast<int>(m_last_gpu_stats.limit_area_pixel_spp) - static_cast<int>(m_req_limit.area_min_pixel_last_spp);
        if (new_samples_rendered > 0) {
            // in area_duration many frames, the renderer could accumulate new_samples_rendered many samples
            // this means that it took (new_samples_rendered - area_duration) many frames to fill the cache:
            // reduce the area_duration accordingly
            m_req_limit.area_duration = glm::max((m_req_limit.area_duration + (1 + new_samples_rendered - m_req_limit.area_duration)) / 2, 4);
            // increase the area size (slower than it is decreased)
            m_req_limit.area_size = ((m_req_limit.area_size * 3 + 7) / 8) * 8; // *3 rounded up to multiple of 8
            // if the area would now cover the whole screen, disable request limitation
            if (m_req_limit.area_size > glm::max(m_resolution.width, m_resolution.height)) {
                disableRequestLimiation();
            }
        }
        // if not: reduce the area size and increase the area duration as long as this is possible
        else {
            // reduce size
            m_req_limit.area_size = m_req_limit.area_size / 4;
            if (m_req_limit.area_size < m_req_limit.g_area_size_min) {
                m_req_limit.area_size = m_req_limit.g_area_size_min;
            }
            // increase duration
            if (m_req_limit.area_size <= m_req_limit.g_area_size_min)
                m_req_limit.area_duration *= 2u;
            else
                m_req_limit.area_duration += 8u;
            if (m_req_limit.area_duration > m_req_limit.g_area_duration_bounds.y) {
                m_req_limit.area_duration = m_req_limit.g_area_duration_bounds.y;

                // if the maximum duration and minimum size are reached: move over to sample random areas instead
                if (m_req_limit.area_size <= m_req_limit.g_area_size_min) {
                    // ... but first try to reset the cache ONCE to see if this solves the problem
                    if (!m_req_limit.tried_cache_reset) {
                        m_pcache_reset = true;
                        m_req_limit.tried_cache_reset = true;
                    } else {
                        if (!m_req_limit.random_area_pixel)
                            Logger(Warn) << "Cache insufficient: unable to render all pixels under current request"
                                            " limitation configuration. Increase cache size with --cache-size [MB]";
                        // m_req_limit.random_area_pixel = true;
                        m_req_limit.area_duration = m_req_limit.g_area_duration_bounds.x;
                        m_accum_step_mode = true;
                    }
                } else {
                    m_req_limit.area_size = m_req_limit.g_area_size_min;
                }
            }
        }

        // the new area of pixels that can request bricks must cover the pixel with the lowest number of samples yet
        // if it is not possible to render any sample for this pixel (even after a long accumulation),
        // random_area_pixel is set to true and the next pixel is always chosen randomly from now on
        if (m_req_limit.area_size > 0) {
            const glm::ivec2 last_pixel = m_req_limit.area_min_pixel;
            if (m_req_limit.random_area_pixel) {
                m_req_limit.area_min_pixel = glm::ivec2(rand() % m_resolution.width, rand() % m_resolution.height);
                m_req_limit.area_min_pixel_last_spp = INVALID;
            } else {
                m_req_limit.area_min_pixel = m_req_limit.global_min_pixel;
                m_req_limit.area_min_pixel_last_spp = global_min_spp;
            }
            m_req_limit.area_pos = (m_req_limit.area_min_pixel / glm::ivec2(m_req_limit.area_size)) * glm::ivec2(m_req_limit.area_size);

            // if we moved over to another location now, we allow resetting the cache once again
            if (last_pixel.x != m_req_limit.area_min_pixel.x && last_pixel.y != m_req_limit.area_min_pixel.y)
                m_req_limit.tried_cache_reset = false;
        }
    }
}

void CompressedSegmentationVolumeRenderer::printGPUMemoryUsage() {
    CSGVRenderEvaluationResults res = getLastEvaluationResults();
    const double available_vram_bytes = static_cast<double>(getMemoryHeapBudgetAndUsage(*getCtx()).first);
    Logger(Info) << "[GPU Memory] " << std::fixed << std::setprecision(3)
                 << "Framebuffers: " << res.mem_framebuffers_bytes * BYTE_TO_GB << " GB | "
                 << "Uniform Buffers: " << res.mem_ubos_bytes * BYTE_TO_GB << " GB | "
                 << "Materials: " << res.mem_materials_bytes * BYTE_TO_GB << " GB | "
                 << "Encoding: " << res.mem_encoding_bytes * BYTE_TO_GB << " GB | "
                 << "Cache: " << res.mem_cache_bytes * BYTE_TO_GB << " GB | "
                 << "Empty Space: " << res.mem_empty_space_bytes * BYTE_TO_GB << " GB | "
                 << "Rest: " << (res.mem_framebuffers_bytes + res.mem_ubos_bytes + res.mem_materials_bytes + res.mem_encoding_bytes + res.mem_cache_bytes + res.mem_empty_space_bytes - res.mem_total_bytes) * BYTE_TO_GB << " GB"
                 << " = " << res.mem_total_bytes * BYTE_TO_GB << " total GB / "
                 << available_vram_bytes * BYTE_TO_GB << " GB";
}

CSGVRenderEvaluationResults CompressedSegmentationVolumeRenderer::getLastEvaluationResults() {
    CSGVRenderEvaluationResults results;

    // obtain GPU memory consumption
    if (m_inpaintedOutColor != nullptr) {
        size_t textures = 0ul;
        if (m_accumulation_rgba_tex[0]) {
            textures += m_accumulation_rgba_tex[0]->memorySize() + m_accumulation_rgba_tex[1]->memorySize();
            textures += m_accumulation_samples_tex[0]->memorySize() + m_accumulation_samples_tex[1]->memorySize();
            for (const auto &t : *m_inpaintedOutColor)
                textures += t->memorySize();
        }
        results.mem_framebuffers_bytes = textures;

        size_t uniform_buffers = 0ul;
        if (m_urender_info) {
            uniform_buffers += m_urender_info->getByteSize() + m_usegmented_volume_info->getByteSize() + m_uresolve_info->getByteSize() + m_ucamera_info->getByteSize();
        }
        results.mem_ubos_bytes = uniform_buffers;

        size_t materials = 0ul;
        if (m_materials_buffer) {
            materials += m_materials_buffer->getByteSize() + m_attribute_buffer->getByteSize();
            for (const auto &t : m_materialTransferFunctions) {
                if (t)
                    materials += t->texture().memorySize();
            }
        }
        results.mem_materials_bytes = materials;

        size_t cache = 0ul;
        if (m_cache_buffer) {
            cache += m_cache_info_buffer->getByteSize() + m_cache_buffer->getByteSize() + m_free_stack_buffer->getByteSize() + m_assign_info_buffer->getByteSize() + (m_detail_requests_buffer ? m_detail_requests_buffer->getByteSize() : 0ul);
        }
        results.mem_cache_bytes = cache;
        results.mem_cache_used_bytes = m_last_gpu_stats.used_cache_base_elements * m_cache_base_element_uints * sizeof(uint32_t);
        results.mem_cache_fill_rate = getCacheFillRate();
        results.min_samples_per_pixel = static_cast<int>(m_last_gpu_stats.min_spp_and_pixel >> 48u);
        results.max_samples_per_pixel = static_cast<int>(m_last_gpu_stats.max_spp_and_pixel >> 48u);

        size_t empty_space = 0ul;
        if (m_empty_space_buffer) {
            empty_space = m_empty_space_buffer->getByteSize();
        }
        results.mem_empty_space_bytes = empty_space;

        size_t encoding = 0ul;
        if (m_brick_starts_buffer) {
            encoding +=
                m_split_encoding_buffer_addresses_buffer->getByteSize() + m_brick_starts_buffer->getByteSize();
            for (const auto &b : m_split_encoding_buffers)
                encoding += b->getByteSize();
            if (m_detail_buffer) {
                encoding += m_detail_buffer->getByteSize() + m_detail_starts_buffer->getByteSize();
            }
        }
        results.mem_encoding_bytes = encoding;
        results.mem_total_bytes = getMemoryHeapBudgetAndUsage(*getCtx()).second;
    }

    // frame times are only available if tracking was enabled via setEvalTracking(true)
    if (m_enable_frame_time_tracking) {
        Logger(Warn) << "Querying rendering results before frame time tracking was stopped";
    }
    if (!m_last_frame_times.empty()) {
        double total = 0.f;
        double min = std::numeric_limits<double>::max();
        double m1 = 0.;
        double m2 = 0.;
        double max = -1.;
        for (int i = 0; i < m_last_frame_times.size(); i++) {
            const auto &ms = m_last_frame_times[i];
            if (i < 16)
                results.frame_ms[i] = ms;
            min = std::min(min, ms);
            max = std::max(max, ms);
            m1 += ms;
            m2 += ms * ms;
            total += ms;
        }
        for (int i = m_last_frame_times.size(); i < 16; i++)
            results.frame_ms[i] = 0.;
        auto frame_time_cpy = m_last_frame_times;
        nth_element(frame_time_cpy.begin(),
                    frame_time_cpy.begin() + (frame_time_cpy.size() / 2),
                    frame_time_cpy.end());
        results.frame_min_ms = min;
        results.frame_avg_ms = m1 / m_last_frame_times.size();
        results.frame_sdv_ms = std::sqrt((m2 / m_last_frame_times.size()) - results.frame_avg_ms * results.frame_avg_ms);
        results.frame_med_ms = *(frame_time_cpy.begin() + (frame_time_cpy.size() / 2));
        results.frame_max_ms = max;
        results.total_ms = total;
        results.accumulated_frames = m_last_frame_times.size();
        Logger(Debug) << "CSGV rendering evaluation: "
                      << results.frame_min_ms << " / " << results.frame_avg_ms << " / " << results.frame_max_ms
                      << " [ms/frame] (min/avg/max) | " << results.accumulated_frames << " total frames";
    }
    return results;
}

} // namespace volcanite
