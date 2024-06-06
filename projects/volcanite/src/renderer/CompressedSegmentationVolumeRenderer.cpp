#include "volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp"

#include <vvv/core/Buffer.hpp>
#include <volcanite/StratifiedPixelSequence.hpp>
#include <chrono>
#include <random>
#include <memory>

#include "glm/gtc/matrix_transform.hpp"

#include "portable-file-dialogs.h"
#ifdef IMGUI
#include "imgui.h"
#endif

namespace vvv {


RendererOutput CompressedSegmentationVolumeRenderer::renderNextFrame(AwaitableList awaitBeforeExecution, BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) {
    if(!(m_usegmented_volume_info && m_urender_info && m_compressed_segmentation_volume && m_csgv_db))
        throw std::runtime_error("CompressedSegmentationVolumeRenderer data missing!");

    // we only want to render the next frame, if the previous frame finished execution
    if(m_mostRecentFrame.has_value()) {
        awaitBeforeExecution.insert(awaitBeforeExecution.end(), m_mostRecentFrame->renderingComplete.begin(), m_mostRecentFrame->renderingComplete.end());
    }

    if(m_data_changed) {
        // wait until all previous frames are processed
        getCtx()->getDevice().waitIdle();

        assert(!m_compressed_segmentation_volume->getBrickStarts()->empty() && !m_compressed_segmentation_volume->getEncoding()->empty() && "CompressedSegmentationVolume not initialized!");
        auto [encoding_upload_finished, _encoding_staging_buffer] = m_encoding_buffer->uploadWithStagingBuffer(*(m_compressed_segmentation_volume->getEncoding()), {.queueFamily = getCtx()->getQueueFamilyIndices().transfer.value()});
        auto [brickstarts_upload_finished, _brickstarts_staging_buffer] = m_brick_starts_buffer->uploadWithStagingBuffer(*(m_compressed_segmentation_volume->getBrickStarts()),  {.queueFamily = getCtx()->getQueueFamilyIndices().transfer.value()});

        awaitBeforeExecution.push_back(encoding_upload_finished);
        awaitBeforeExecution.push_back(brickstarts_upload_finished);

        // reset cache
        m_pass->resetCacheOnNextCall();

        // update invocation sizes to brick dimension
        m_pass->setVolumeInfo(m_compressed_segmentation_volume->getBrickCount(), m_compressed_segmentation_volume->getLodCountPerBrick());
        {
            const size_t brick_size = m_compressed_segmentation_volume->getBrickSize();
            const size_t cache_element_size = brick_size * brick_size * brick_size;
            const size_t cache_bricks = static_cast<uint32_t>(m_cache_buffer->getByteSize() / 4l / cache_element_size);
            Logger(DEBUG) << "new data set with " << str(m_compressed_segmentation_volume->getBrickCount()) << " bricks added. Cache fits " <<  cache_bricks << " = " << static_cast<uint32_t>(std::pow(static_cast<double>(cache_bricks), 1.f/3.f)) << "^3 bricks on finest LoD.";
        }

        // reset all accumulation buffers
        m_camHash = static_cast<size_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());

        // ToDo? is this required? wait until everything is uploaded
        getCtx()->getDevice().waitIdle();
        m_data_changed = false;
    }

    updateAttributeBuffers();
    // if one of our materials changed, we update the whole buffer
    if(std::find(m_gpu_material_changed.begin(), m_gpu_material_changed.end(), true) != m_gpu_material_changed.end()) {
        std::vector<GPUSegmentedVolumeMaterial> gpu_mat(m_materials.size());
        for (int m = 0; m < SEGMENTED_VOLUME_MATERIAL_COUNT; m++) {
            // Discriminator
            // (we do not need to upload the attribute 0 which is the csgv_id, e.g. the voxel value)
            if (m_materials[m].getSafeDiscrAttribute() <= 0) {
                gpu_mat[m].discrAttributeStart = LABEL_AS_ATTRIBUTE;
            }
            else if (m_attribute_start_position[m_materials[m].getSafeDiscrAttribute()] < 0) {
                gpu_mat[m].discrAttributeStart = LABEL_AS_ATTRIBUTE;
                // ToDo: this should not throw an exception. We should only allow to select the max. number of attributes in the GUI
                throw std::runtime_error("GPU attribute buffer does not fit all selected attributes!");
            } else {
                gpu_mat[m].discrAttributeStart = static_cast<uint32_t>(m_attribute_start_position[m_materials[m].getSafeDiscrAttribute()]);
                assert(gpu_mat[m].discrAttributeStart >= 0 &&
                       gpu_mat[m].discrAttributeStart < (m_max_attribute_buffer_size / sizeof(float)) &&
                       "invalid start index in GPU attribute buffer");
            }
            gpu_mat[m].discrIntervalMin = m_materials[m].getDiscrInterval().x;
            gpu_mat[m].discrIntervalMax = m_materials[m].getDiscrInterval().y;

            // Visualization attribute
            // (we do not need to upload the attribute 0 which is the csgv_id, e.g. the voxel value)
            if (m_materials[m].tfAttribute <= 0) {
                gpu_mat[m].tfAttributeStart = LABEL_AS_ATTRIBUTE;
            }
            else if (m_attribute_start_position[m_materials[m].tfAttribute] < 0) {
                gpu_mat[m].tfAttributeStart = LABEL_AS_ATTRIBUTE;
                throw std::runtime_error("GPU attribute buffer does not fit all selected attributes!");
            } else {
                gpu_mat[m].tfAttributeStart = static_cast<uint32_t>(m_attribute_start_position[m_materials[m].tfAttribute]);
                assert(gpu_mat[m].tfAttributeStart >= 0 &&
                       gpu_mat[m].tfAttributeStart < (m_max_attribute_buffer_size / sizeof(float)) &&
                       "invalid start index in GPU attribute buffer");
            }
            gpu_mat[m].tfIntervalMin = m_materials[m].tfMinMax.x;
            gpu_mat[m].tfIntervalMax = m_materials[m].tfMinMax.y;
            gpu_mat[m].opacity = m_materials[m].opacity;
            gpu_mat[m].emission = m_materials[m].emission;

            m_gpu_material_changed[m] = false;
        }
        // upload material buffer
        auto [material_upload_finished, _material_upload_staging_buffer] = m_materials_buffer->uploadWithStagingBuffer(
                gpu_mat.data(), sizeof(GPUSegmentedVolumeMaterial) * m_materials.size(), {.queueFamily = getCtx()->getQueueFamilyIndices().transfer.value()});
        getCtx()->sync->hostWaitOnDevice({material_upload_finished}); // we have to wait here, otherwise the upload_staging buffer is freed immediately
    }

    // wait for the last frame to finish execution (which will also mean that the previous upload of the detail starts finished)
    getCtx()->sync->hostWaitOnDevice(awaitBeforeExecution);

    // if a screenshot export was requested, we do this here
    if(m_download_frame_to_image_file.has_value() && m_mostRecentFrame.has_value()) {
        Logger(INFO) << "exporting screenshot to " << m_download_frame_to_image_file.value();
        try {
            m_mostRecentFrame->texture->writeFile(m_download_frame_to_image_file.value());
        }
        catch(const std::runtime_error& e) {
            Logger(ERROR) << "image export error: " << e.what();
        }
        m_download_frame_to_image_file = {};
    }

    // we have to know if the detail buffer is still in an uploading state. If yes, we don't do anything else with the detail buffer
    // if streaming the detail buffer is disabled, we set the flag to true to avoid any usage of the detail resources.
    bool detail_buffer_dirty = !m_compressed_segmentation_volume->isUsingSeparateDetail() ||
                                (  (m_detail_starts_staging.first != nullptr && !getCtx()->sync->isAwaitableResolved(m_detail_starts_staging.first))
                                || (m_detail_staging.first != nullptr && !getCtx()->sync->isAwaitableResolved(m_detail_staging.first)) );
    // download the next request buffer
    std::vector<uint32_t> requested_ids(m_max_detail_requests_per_frame + 2u, INVALID);
    uint32_t requested_id_count = 0u;
    uint32_t cache_usage = 0u;
    if(m_compressed_segmentation_volume->isUsingSeparateDetail()) {
        if (!detail_buffer_dirty && m_frame > 0u) {
            m_detail_starts_staging = {nullptr, nullptr}; // we can now free the staging buffers for the detail upload because they are no longer uploading / in a dirty state
            m_detail_staging = {nullptr, nullptr};

            m_detail_requests_buffer->download(requested_ids);
            requested_id_count = requested_ids[m_max_detail_requests_per_frame] > m_max_detail_requests_per_frame ? m_max_detail_requests_per_frame : requested_ids[m_max_detail_requests_per_frame];
        }
        // reset the atomic counter
        static const uint32_t zeroes[2] = {0u, 0u};
        m_detail_requests_buffer->upload(m_max_detail_requests_per_frame * sizeof(uint32_t), &zeroes, 2 * sizeof(uint32_t));

        // one element after, we store the current cache usage as number of used 2x2x2 elements
        cache_usage = requested_ids[m_max_detail_requests_per_frame + 1u];
    }
    else {
        // ToDo: download the cache_usage also if we don't use detail separation, e.g. m_last_gpu_stats.gpu_cache_size which is currently only set when m_show_step_count
    }

    // trigger garbage collection on demand, but only if we have a different camHash since the last garbage collection
    const uint32_t cache_elements_per_finest_lod = m_compressed_segmentation_volume->getBrickSize() / 2u;
    if(m_frame % 4 == 0u && cache_usage >= m_cache_capacity - (cache_elements_per_finest_lod * cache_elements_per_finest_lod * cache_elements_per_finest_lod) && m_camHash_at_last_cache_reset != m_camHash) {
        m_pass->resetCacheOnNextCall();
        m_camHash_at_last_cache_reset = m_camHash;
    }


    if(m_clear_cache_every_frame)
        m_pass->resetCacheOnNextCall();

    // upload uniforms
    if (m_urender_info && m_usegmented_volume_info) {
        updateUniformDescriptorset();

        // inform the shader if the detail buffer upload is ready (= the staging buffer upload finished
        m_usegmented_volume_info->setUniform<uint32_t>("g_detail_buffer_dirty", detail_buffer_dirty ? 1u : 0u);

        m_urender_info->upload(m_pass->getActiveIndex());
        m_usegmented_volume_info->upload(m_pass->getActiveIndex());
    }

    // if we only accumulate a certain number of frames, we just return the last result
    if (m_accum_frames > 0 && m_framesSinceCameraMove >= m_accum_frames) {
        m_framesSinceCameraMove = m_accum_frames;
        return m_mostRecentFrame.value();
    }

    if (m_compressed_segmentation_volume->isUsingSeparateDetail()) {
        if(!detail_buffer_dirty && m_detail_update_required && m_constructed_detail_starts.back() > 0u) {
            m_detail_starts_staging = m_detail_starts_buffer->uploadWithStagingBuffer(m_constructed_detail_starts.data(), m_constructed_detail_starts.size() * sizeof(uint32_t), {.queueFamily = getCtx()->getQueueFamilyIndices().transfer.value()});
            m_detail_staging = m_detail_buffer->uploadWithStagingBuffer(m_constructed_detail.data(), m_constructed_detail_starts.back() * sizeof(uint32_t), {.queueFamily = getCtx()->getQueueFamilyIndices().transfer.value()});
        }
    }

    // GPU debug / stats
    if(m_show_step_count) {
        m_gpu_stats_buffer->download(&m_last_gpu_stats, sizeof(m_last_gpu_stats));
        size_t decoded_bytes_in_frame = 0;
        size_t decoded_bytes_total = 0;
        std::stringstream cache_state = {};
        for (int i = 0; i < m_compressed_segmentation_volume->getLodCountPerBrick() - 1u; i++) {
            decoded_bytes_in_frame += m_last_gpu_stats.gpu_blocks_decoded[i] * (2u << i) * (2u << i) * (2u << i) * 4; // i = 0 means inv_lod 1 so 2^3 voxels
            decoded_bytes_total += m_last_gpu_stats.gpu_blocks_in_cache[i] * (2u << i) * (2u << i) * (2u << i) * 4; // i = 0 means inv_lod 1 so 2^3 voxels
            cache_state << "inv. LOD" << (i+1) << ": " << m_last_gpu_stats.gpu_blocks_in_cache[i] << ", ";
        }
        cache_state << static_cast<double>(decoded_bytes_total) / 1000. / 1000.f << " MB total";
        Logger(INFO) << cache_state.str() << "   |   " << m_last_gpu_stats.gpu_raymarch_samples << " ray samples (" << static_cast<double>(m_last_gpu_stats.gpu_raymarch_samples) / static_cast<double>(m_last_gpu_stats.gpu_bbox_hits) << " per ray).";
        if (decoded_bytes_in_frame > 0)
            Logger(INFO) << "decoded " << std::fixed << std::setprecision(3) << static_cast<double>(decoded_bytes_in_frame) / 1000. / 1000. << " MB";
    }

    m_pass->setStorageImage("inpaintedOutColor", *m_inpaintedOutColor);
    // feedback texture ping pong for the inpainting shader
    m_pass->setStorageImage("feedbackIn", *m_feedback_tex[m_frame % 2u]);
    m_pass->setStorageImage("feedbackOut", *m_feedback_tex[1u - (m_frame % 2u)]);
    // 16 bit packed gBuffer texture storing
    m_pass->setStorageImage("gBuffer", *m_gBuffer_tex);


    std::vector<std::shared_ptr<Awaitable>> renderAwaitableList = {};
    // we just check the awaitables in the shader now!
//    if(m_detail_starts_staging.first)
//        renderAwaitableList.push_back(m_detail_starts_staging.first);
//    if(m_detail_staging.first)
//        renderAwaitableList.push_back(m_detail_staging.first);

    const auto renderingFinished = m_pass->execute(renderAwaitableList, awaitBinaryAwaitableList, signalBinarySemaphore);
    
    if(m_compressed_segmentation_volume->isUsingSeparateDetail() && !detail_buffer_dirty) {
        assert(!m_constructed_detail.empty() && "creating detail buffers but detail buffer has no capacity");
        // ToDo: m_detail_update_required = check if current and previous detail indices changed
        // ToDo: use remaining space in g_detail to store the perviously requested bricks (move to right)? would need one dummy element in between to make the detail_starts[i+1]-[i] size query possible
        //      can we use a ring buffer for that?

        m_detail_update_required = false;
        // check if any id is new
        #pragma omp parallel for default(none) shared(requested_id_count, requested_ids, m_constructed_detail_starts, m_detail_update_required)
        for(int i = 0; i < requested_id_count; i++) {
            if(m_detail_update_required)
                continue;
            if(m_constructed_detail_starts[requested_ids[i] + 1u] - m_constructed_detail_starts[requested_ids[i]] == 0u) {
                m_detail_update_required = true;
            }
        }


        if(m_detail_update_required) {
            // 1. sort requested brick IDs
            std::sort(requested_ids.begin(), requested_ids.begin() + requested_id_count);
            // 2. for ALL bricks: compute prefix sum of sizes, assuming an added 0 size if brick is not requested. Store in m_detail_starts
            uint32_t next_requested_id = 0u;
            uint32_t total_detail_size = 0u;
            const std::vector<uint32_t> *detail_starts = m_compressed_segmentation_volume->getDetailStarts();
            for (int i = 0; i < m_constructed_detail_starts.size(); i++) {
                m_constructed_detail_starts[i] = total_detail_size;

                // if this id is requested, we reserve some memory for it (as long as there's enough space in the detail array left)
                if (next_requested_id < requested_id_count && i == requested_ids[next_requested_id]) {
                    uint32_t brick_detail_size = (*detail_starts)[i + 1] - (*detail_starts)[i];
                    if ((total_detail_size + brick_detail_size) <= m_detail_capacity) {
                        total_detail_size += brick_detail_size;
                        next_requested_id++;
                    }
                }
            }
            // 3. in parallel: copy all detail encodings to the m_detail_encoding
            const std::vector<uint32_t> *detail = m_compressed_segmentation_volume->getDetail();
            #pragma omp parallel for default(none) shared(requested_id_count, requested_ids, m_constructed_detail_starts, m_constructed_detail, detail_starts, detail)
            for (int i = 0; i < requested_id_count; i++) {
                uint32_t brick_id = requested_ids[i];
                uint32_t start = (*detail_starts)[brick_id];
                uint32_t end = (*detail_starts)[brick_id + 1];
                memcpy(m_constructed_detail.data() + m_constructed_detail_starts[brick_id], detail->data() + start, (end - start) * sizeof(uint32_t));
            }

#if 0
            if (m_constructed_detail_starts.back() > 0u) {
                std::stringstream ss;
                ss << "Total size: " << m_constructed_detail_starts.back() << " for bricks ";
                for (int i = 0; i < requested_id_count; i++) {
                    ss << requested_ids[i] << " ";
                }
                Logger(INFO) << ss.str();
            }
#endif
        }
    }

    // Update GPU memory usage regularly
    if(m_frame % 300  == 0u) {
        updateDeviceMemoryUsage();
    }

    // update tracking variables
    m_frame = m_frame >= UINT32_MAX ? 0u : m_frame + 1u;

    m_mostRecentFrame = vvv::RendererOutput{
        .texture = m_inpaintedOutColor->getActive().get(),
        .renderingComplete = {renderingFinished},
    };
    return m_mostRecentFrame.value();
}

void CompressedSegmentationVolumeRenderer::initResources(GpuContext *ctx) {
    setCtx(ctx);
    updateDeviceMemoryUsage();
    Logger(INFO) << "Device memory on startup: " << m_gui_device_mem_text;

    // allocate GPU buffers for our data
    size_t bricks_in_volume = 0u;
    size_t lods_in_volume = 0u;
    size_t encoding_byte_size = 0u;
    m_detail_capacity = 0u; // measured in number of uints
    bool detail_buffer_fits_whole_detail = false;
    if(m_compressed_segmentation_volume) {
        auto brick_count = m_compressed_segmentation_volume->getBrickCount();
        bricks_in_volume = brick_count.x * brick_count.y * brick_count.z;
        encoding_byte_size = m_compressed_segmentation_volume->getEncoding()->size() * sizeof(uint32_t);
        lods_in_volume = m_compressed_segmentation_volume->getLodCountPerBrick();

        if(m_compressed_segmentation_volume->isUsingSeparateDetail() && !m_compressed_segmentation_volume->isUsingDetailFreq())
            throw std::runtime_error("Renderer only supports detail separation when rANS is in double table mode!");

        if(m_compressed_segmentation_volume->isUsingSeparateDetail()) {
            size_t optimal_detail_size = m_compressed_segmentation_volume->getDetail()->size();
            // we can't fit the complete detail buffer onto the GPU
//#define ALWAYS_STREAM_DETAIL
#ifdef ALWAYS_STREAM_DETAIL
            if(true) {
#else
            if(m_max_detail_byte_size / sizeof(uint32_t) < optimal_detail_size) {
#endif
                m_detail_capacity = m_max_detail_byte_size / sizeof(uint32_t);
            }
            // we can fit the complete detail buffer onto the GPU
            else {
                m_detail_capacity = optimal_detail_size;
                detail_buffer_fits_whole_detail = true;
            }
            m_constructed_detail_starts.resize(bricks_in_volume + 1u, 0u);
            m_constructed_detail.resize(m_detail_capacity, 0u);
        }

        // check limits of physical device (GPU)
        size_t maxGPUBufferSize = getCtx()->getPhysicalDevice().getProperties().limits.maxStorageBufferRange;
        if (encoding_byte_size > maxGPUBufferSize) {
            throw std::runtime_error("Base encoding buffer size exceeds max. GPU buffer range (" + std::to_string(encoding_byte_size) + " > " +
                                     std::to_string(maxGPUBufferSize) + ")");
        }
        if(m_max_detail_byte_size > maxGPUBufferSize) {
            throw std::runtime_error("Detail encoding buffer size exceeds max. GPU buffer range (" + std::to_string(m_max_detail_byte_size) + " > " +
                                     std::to_string(maxGPUBufferSize) + ")");
        }
    }
    else {
        throw std::runtime_error("Currently, a Compressed Segmentation Volume must be passed before rendering to allocate correct GPU buffer sizes.");
    }
    m_brick_starts_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_brick_start_buffer", .byteSize = (bricks_in_volume + 1u)*sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    m_encoding_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_encoding_buffer", .byteSize = encoding_byte_size, .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    Buffer::deviceAddressUvec2(m_encoding_buffer->getDeviceAddress(), &m_encoding_buffer_address.x);
    m_attribute_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_attribute_buffer", .byteSize = m_max_attribute_buffer_size, .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    m_materials_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_materials_buffer", .byteSize = sizeof(GPUSegmentedVolumeMaterial) * SEGMENTED_VOLUME_MATERIAL_COUNT, .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});

    m_cache_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_cache_buffer", .byteSize = m_cache_capacity * (2u*2u*2u) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    m_free_stack_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_free_stack_buffer", .byteSize = (m_free_stack_capacity * (lods_in_volume - 1u) + (lods_in_volume + 1u)) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    m_cache_info_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_cache_info_buffer", .byteSize = bricks_in_volume*sizeof(uint32_t)*4u, .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    m_assign_info_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_assign_buffer", .byteSize = (1u + (lods_in_volume - 1u) * 3u) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});

    if(m_detail_capacity > 0ul) {
        m_detail_requests_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_detail_requests_buffer", .byteSize = (m_max_detail_requests_per_frame + 2u) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc, .memoryUsage = vk::MemoryPropertyFlagBits::eHostVisible});
        m_detail_starts_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_detail_starts_buffer", .byteSize = (bricks_in_volume + 1u)*sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
        m_detail_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_detail_buffer", .byteSize = m_detail_capacity * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
        Buffer::deviceAddressUvec2(m_detail_buffer->getDeviceAddress(), &m_detail_buffer_address.x);

        if(detail_buffer_fits_whole_detail) {
            Logger(WARN) << "GPU detail buffer fits the whole detail level. Performing full upload, effectively disabling detail streaming. Consider to not use detail streaming for better performance!";
            m_detail_staging = m_detail_buffer->uploadWithStagingBuffer(m_compressed_segmentation_volume->getDetail()->data(), m_compressed_segmentation_volume->getDetail()->size() * sizeof(uint32_t));
            m_detail_starts_staging = m_detail_starts_buffer->uploadWithStagingBuffer(m_compressed_segmentation_volume->getDetailStarts()->data(),
                                                                                      m_compressed_segmentation_volume->getDetailStarts()->size() * sizeof(uint32_t));
            m_constructed_detail_starts = *m_compressed_segmentation_volume->getDetailStarts(); // just to be sure: we tell the CPU side that every brick is uploaded
            getCtx()->sync->hostWaitOnDevice({m_detail_staging.first, m_detail_starts_staging.first});
            m_detail_staging = {nullptr, nullptr};
            m_detail_starts_staging = {nullptr, nullptr};
        }
        else {
            // initialize detail starts buffer on the GPU with zeros (no detail is uploaded initially)
            m_detail_starts_staging = m_detail_starts_buffer->uploadWithStagingBuffer(m_constructed_detail_starts.data(), m_constructed_detail_starts.size() * sizeof(uint32_t), {.queueFamily = getCtx()->getQueueFamilyIndices().transfer.value()});
            getCtx()->sync->hostWaitOnDevice({m_detail_starts_staging.first});
            m_detail_staging = {nullptr, nullptr};
            m_detail_starts_staging = {nullptr, nullptr};
        }
    }

    // GPU stats buffer
    m_gpu_stats_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_gpu_stats_buffer", .byteSize = sizeof(GPUStats), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc, .memoryUsage = vk::MemoryPropertyFlagBits::eHostVisible});

    updateDeviceMemoryUsage();
    Logger(INFO) << "Device memory after initialization: " << m_gui_device_mem_text;

    // Set camera to a nice start position
    getCamera().reset();

    if(m_compressed_segmentation_volume)
        m_data_changed = true; // trigger re-upload to new buffers
    for (int m = 0; m < m_gpu_material_changed.size(); m++)
        m_gpu_material_changed[m] = true;
    int attributeCount = m_csgv_db ? static_cast<int>(m_csgv_db->getAttributeCount()) : 1;
    for (int a = 0; a < attributeCount; a++)
        m_attribute_start_position.at(a) = -1;
}

void CompressedSegmentationVolumeRenderer::releaseResources() {
    m_gpu_stats_buffer = nullptr;
    m_assign_info_buffer = nullptr;
    m_cache_info_buffer = nullptr;
    m_free_stack_buffer = nullptr;
    m_cache_buffer = nullptr;
    m_attribute_buffer = nullptr;
    m_materials_buffer = nullptr;
    m_encoding_buffer = nullptr;
    m_brick_starts_buffer = nullptr;
    m_detail_buffer = nullptr;
    m_detail_starts_buffer = nullptr;
    m_detail_requests_buffer = nullptr;
    m_detail_starts_staging.second = nullptr;
    m_detail_staging.second = nullptr;
    for(auto& tf : m_materialTransferFunctions)
        tf = nullptr;
    setCtx(nullptr);
}

void CompressedSegmentationVolumeRenderer::initShaderResources() {
    assert(getCtx() != nullptr && "renderer needs a valid GPU context");
    assert(m_compressed_segmentation_volume && "can't render without a CompressedSegmentationVolume");

    std::vector<std::string> shader_defines;
    if(m_compressed_segmentation_volume->isUsingRANS()) {
        shader_defines.push_back("USE_RANS");
        // @ToDo the rANS symbol tables should not be compile time definition as it prohibits using precompiled shaders for release builds
        shader_defines.push_back("RANS_SYMBOL_TABLE=" + m_compressed_segmentation_volume->getGLSLSymbolArrayStringRANS());
        if(m_compressed_segmentation_volume->isUsingDetailFreq())
            shader_defines.push_back("USE_RANS_DOUBLE_TABLE");
    }
    if(m_compressed_segmentation_volume->isUsingSeparateDetail()) {
        shader_defines.push_back("SEPARATE_DETAIL");
    }
    shader_defines.push_back("SEGMENTED_VOLUME_MATERIAL_COUNT=" + std::to_string(SEGMENTED_VOLUME_MATERIAL_COUNT));
    // ToDo: does this work? if we're rendering without a GLFW window / WSI, we're disabling MultiBuffering
    if(getCtx()->getWsi())
        m_pass = std::make_unique<PassCompSegVolRender>(getCtx(), getCtx()->getWsi()->stateInFlight(), shader_defines);
    else
        m_pass = std::make_unique<PassCompSegVolRender>(getCtx(), NoMultiBuffering, shader_defines);
    m_pass->allocateResources();
    m_urender_info = m_pass->getUniformSet("render_info");
    m_usegmented_volume_info = m_pass->getUniformSet("segmented_volume_info");
    m_pass->setStorageBuffer(0, 1, *m_brick_starts_buffer);
    m_pass->setStorageBuffer(0, 2, *m_encoding_buffer);
    m_pass->setStorageBuffer(0, 3, *m_cache_info_buffer);
    m_pass->setStorageBuffer(0, 4, *m_assign_info_buffer);
    m_pass->setStorageBuffer(0, 5, *m_free_stack_buffer);
    m_pass->setStorageBuffer(0, 6, *m_cache_buffer);
    if(m_compressed_segmentation_volume->isUsingSeparateDetail()) {
        m_pass->setStorageBuffer(0, 7, *m_detail_starts_buffer);
        m_pass->setStorageBuffer(0, 8, *m_detail_buffer);
        m_pass->setStorageBuffer(0, 9, *m_detail_requests_buffer);
    }
    m_pass->setStorageBuffer(0, 16, *m_gpu_stats_buffer);
    m_pass->setStorageBuffer(0, 17, *m_attribute_buffer);
    m_pass->setStorageBuffer(0, 18, *m_materials_buffer);
    m_pass->setVolumeInfo(m_compressed_segmentation_volume->getBrickCount(), m_compressed_segmentation_volume->getLodCountPerBrick());
    // reset all camera hashes and frame counters
    m_camHash = static_cast<size_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    m_framesSinceCameraMove = 0;
    m_frame = 0u;
//    m_pass->resetCacheOnNextCall();
}

void CompressedSegmentationVolumeRenderer::releaseShaderResources() {
    m_usegmented_volume_info = nullptr;
    m_urender_info = nullptr;
    if(m_pass)
        m_pass->freeResources();
    m_pass = nullptr;
}


void CompressedSegmentationVolumeRenderer::initSwapchainResources() {
    updateRenderResolutionFromWSI();

    // tell the pass the new invocation size
    m_pass->setImageInfo(m_resolution.width, m_resolution.height);

    // recreate all swapchain image sized textures
    vvv::AwaitableList reinitDone;
    m_feedback_tex[0] = m_pass->reflectTexture("feedbackIn", {.width = m_resolution.width, .height = m_resolution.height, .format = vk::Format::eR32G32B32A32Sfloat, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    m_feedback_tex[1] = m_pass->reflectTexture("feedbackOut", {.width = m_resolution.width, .height = m_resolution.height, .format = vk::Format::eR32G32B32A32Sfloat, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    for (auto & texture : m_feedback_tex) {
        texture->ensureResources();
        const auto layoutTransformDone = texture->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eAllCommands);
        reinitDone.push_back(layoutTransformDone);
    }
    m_gBuffer_tex = m_pass->reflectTexture("gBuffer", {.width = m_resolution.width, .height = m_resolution.height, .format = vk::Format::eR8G8Uint, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    {
        m_gBuffer_tex->ensureResources();
        const auto layoutTransformDone = m_gBuffer_tex->setImageLayout(vk::ImageLayout::eGeneral,vk::PipelineStageFlagBits::eAllCommands);
        reinitDone.push_back(layoutTransformDone);
    }
    m_inpaintedOutColor = m_pass->reflectTextures(
        "inpaintedOutColor", {.width = m_resolution.width, .height = m_resolution.height, .format = vk::Format::eR8G8B8A8Unorm, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    for (auto& texture : *m_inpaintedOutColor){
        texture->ensureResources();
        const auto layoutTransformDone = texture->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eAllCommands);
        reinitDone.push_back(layoutTransformDone);
    }

    // write all transfer function samplers once
    for(int m = 0; m < m_materials.size(); m++) {
        if(m >= m_materialTransferFunctions.size() || !m_materialTransferFunctions[m])
            updateSegmentedVolumeMaterial(m);
        else
            m_pass->setImageSamplerArray("s_transferFunctions", m, m_materialTransferFunctions[m]->texture(), vk::ImageLayout::eReadOnlyOptimal, false);
    }

    m_gui_resolution_text = "Render resolution: " + std::to_string(m_resolution.width) + "x" + std::to_string(m_resolution.height);
    getCtx()->sync->hostWaitOnDevice(reinitDone);

    // trigger a temporal accumulation flush
    m_camHash = static_cast<size_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    m_framesSinceCameraMove = 0;
    m_frame = 0u;
//    m_pass->resetCacheOnNextCall();
}

void CompressedSegmentationVolumeRenderer::releaseSwapchain() {
    if(m_mostRecentFrame.has_value()) {
        m_mostRecentFrame->texture = nullptr;
        m_mostRecentFrame->renderingComplete = {};
    }
    if(m_feedback_tex[0])
        m_feedback_tex[0] = nullptr;
    if(m_feedback_tex[1])
        m_feedback_tex[1] = nullptr;
    if(m_gBuffer_tex)
        m_gBuffer_tex = nullptr;
    if (m_inpaintedOutColor)
        m_inpaintedOutColor.reset();// = nullptr;
}

void CompressedSegmentationVolumeRenderer::resetGPU() {
    releaseGui();
    releaseSwapchain();
    releaseShaderResources();
    releaseResources();
}

void CompressedSegmentationVolumeRenderer::updateUniformDescriptorset() {
    const auto camera = getCamera();
    updateRenderResolutionFromWSI();

    glm::vec3 voldim = glm::vec3(m_compressed_segmentation_volume->getVolumeDim());
    if(m_subblock_enabled)
        voldim = m_subblock_size;
    glm::vec3 physical_voldim = voldim * m_voxel_size;

    // size in world space: uniformly scaled so that the largest component is one
    float scalingFactor = glm::max(physical_voldim.x, glm::max(physical_voldim.y, physical_voldim.z));
    glm::vec3 normalized_volume_size(physical_voldim / scalingFactor);

    // render info uniform
    {
        m_urender_info->setUniform<glm::vec4>("g_background_color_a", m_background_color_a);
        m_urender_info->setUniform<glm::vec4>("g_background_color_b", m_background_color_b);
        int max_active_material = -1;
        for(int m = 0; m < m_materials.size(); m++)
            if (m_materials[m].isActive())
                max_active_material = m;
        m_urender_info->setUniform<int32_t>("g_max_active_material", max_active_material);
        m_urender_info->setUniform<uint32_t>("g_global_illumination_enable", m_global_illumination_enabled ? 1 : 0);
        m_urender_info->setUniform<uint32_t>("g_envmap_enable", m_envmap_enabled ? 1 : 0);
        m_urender_info->setUniform<float>("g_shadow_pathtracing_ratio", m_shadow_pathtracing_ratio);
        m_urender_info->setUniform<uint32_t>("g_tonemap_enable", m_tonemap_enabled ? 1 : 0);
        m_urender_info->setUniform<glm::vec3>("g_light_direction", m_light_direction);
        m_urender_info->setUniform<float>("g_light_intensity", m_light_intensity);
        m_urender_info->setUniform<int32_t>("g_max_path_length", m_max_path_length);
        m_urender_info->setUniform<int32_t>("g_maxSteps", m_max_steps);
        m_urender_info->setUniform<int32_t>("g_subsampling", (1 << m_subsampling));
        // bbox is the volume dimension in voxels centered around the origin (if no bbox reduction is applied)
        m_urender_info->setUniform<glm::vec4>("g_bboxMin", glm::vec4(m_bboxMin, 1.f));
        m_urender_info->setUniform<glm::vec4>("g_bboxMax", glm::vec4(m_bboxMax, 1.f));
        m_urender_info->setUniform<uint32_t>("g_local_shading_enable", m_cook_torrance_shading ? 1 : 0);
        m_urender_info->setUniform<float>("g_factor_ambient", m_factor_ambient);
        m_urender_info->setUniform<float>("g_ratio_spec_diff", m_ratio_spec_diff);
        m_urender_info->setUniform<uint32_t>("g_blue_noise_enable", m_blue_noise ? 1 : 0);
        m_urender_info->setUniform<uint32_t>("g_denoise", m_denoise ? 1 : 0);
        m_urender_info->setUniform<float>("g_difference_depth_denoising", m_difference_depth_denoising);
        m_urender_info->setUniform<float>("g_spatial_sigma", m_spatial_sigma);
        m_urender_info->setUniform<float>("g_depth_sigma", m_depth_sigma);
        m_urender_info->setUniform<int>("g_denoise_filter_kernel_size", m_denoise_filter_kernel_size);
        m_urender_info->setUniform<float>("g_opacityThreshold",
                                          0.5); // TODO: we have this low opacity treshold to render opaque first hits
        m_urender_info->setUniform<glm::vec3>("g_camera_position_world_space", camera->position_world_space);
        m_urender_info->setUniform<float>("g_lod_bias", m_lod_bias);
        // the g_voxels_per_pixel_per_dist determines how many voxels an image pixel footprint overlaps for a camera distance
        float voxels_per_pixel_at_near = scalingFactor / float(m_resolution.height);
        m_urender_info->setUniform<float>("g_voxels_per_pixel_per_dist", glm::tan(this->getCamera()->vertical_fov) * voxels_per_pixel_at_near);

        // debug
        m_urender_info->setUniform<uint32_t>("g_debug_model_space", m_show_model_space ? 1 : 0);
        m_urender_info->setUniform<uint32_t>("g_debug_brick_cache", m_show_brick_cache ? 1 : 0);
        m_urender_info->setUniform<uint32_t>("g_debug_lod", m_show_lod ? 1 : 0);
        m_urender_info->setUniform<uint32_t>("g_debug_step_count", m_show_step_count ? 1 : 0);
        m_urender_info->setUniform<uint32_t>("g_debug_envmap", m_show_envmap ? 1 : 0);
        m_urender_info->setUniform<uint32_t>("g_debug_normals", m_show_normals ? 1 : 0);

        // Transformation matrices:
        // ToDo: use push constants for camera related changes and upload uniforms only on demand
        // In world space, everything should be a cuboid with the largest dimension being one, centered around the origin.
        // In model space, one voxel must be a unit cube. The normalization transform scales this down to world space [-0.5, 0.5]^3
        glm::mat4 world_to_model_space;
        // ToDo: generalize switching model space axes in the GUI as axes selector [xyz, xzy, yxz, ...]) and remove the hacky fix
//        // hacky fix for switching axes for the mouse cortex
//        if(m_compressed_segmentation_volume->getVolumeDim().x > 2000) {
//            glm::mat4 _world_to_model_space = glm::translate(glm::scale(glm::mat4(1.f), glm::vec3(scalingFactor)), glm::vec3(normalized_volume_size / 2.f));
//            world_to_model_space = glm::mat4(_world_to_model_space[0], _world_to_model_space[2], _world_to_model_space[1], _world_to_model_space[3]);
//        }
//        else
        world_to_model_space = glm::translate(glm::scale(glm::mat4(1.f), voldim / normalized_volume_size), normalized_volume_size / 2.f);
        m_urender_info->setUniform<glm::mat4x4>("g_model_to_world_space", glm::inverse(world_to_model_space));
        m_urender_info->setUniform<glm::mat4x4>("g_world_to_model_space", world_to_model_space);
        m_urender_info->setUniform<glm::mat3x3>("g_world_to_model_space_dir", glm::mat3(world_to_model_space));
        m_urender_info->setUniform<float>("g_world_to_model_space_scaling", scalingFactor);
        const auto world_to_projection_space = camera->get_world_to_projection_space(m_resolution);
        const auto projection_to_world_space = glm::inverse(world_to_projection_space);
        m_urender_info->setUniform<glm::mat4x4>("g_world_to_projection_space", world_to_projection_space);
        m_urender_info->setUniform<glm::mat4x4>("g_projection_to_world_space", projection_to_world_space);
        m_urender_info->setUniform<glm::mat4x4>("g_projection_to_view_space",
                                                glm::inverse(camera->get_view_to_projection_space(m_resolution)));
        m_urender_info->setUniform<glm::mat4x4>("g_view_to_world_space",
                                                glm::inverse(camera->get_world_to_view_space()));
        m_urender_info->setUniform<glm::mat4x4>("g_view_to_projection_space",
                                                camera->get_view_to_projection_space(m_resolution));
        m_urender_info->setUniform<glm::mat4x4>("g_world_to_view_space", camera->get_world_to_view_space());
        glm::mat4 projection_to_world_space_no_translation = projection_to_world_space;
        glm::vec2 viewportScale(2.0f / m_resolution.width, 2.0f / m_resolution.height);
        glm::mat4 pixel_to_ray_direction_projection_space({viewportScale[0], 0.0f, 0.0f, 0.0f},
                                                          {0.0f, viewportScale[1], 0.0f, 0.0f},
                                                          {0.5f * viewportScale[0] - 1.0f,
                                                           0.5f * viewportScale[1] - 1.0f, 1.0f, 1.0f},
                                                          {0.f, 0.f, 0.f, 1.f});
        m_urender_info->setUniform<glm::mat3x3>("g_pixel_to_ray_direction_world_space", glm::mat3x3(
                projection_to_world_space_no_translation * pixel_to_ray_direction_projection_space));

        // detect if the camera was moved since the last frame (useful for progressive rendering etc.)
        // (or if any rendering parameters changed, technically not "camera" only anymore, but we can use it for resetting all accumulation buffers.)
        m_framesSinceCameraMove++;
        auto newCamHash = hashMemory(&world_to_projection_space[0].x, sizeof(glm::mat4));
        newCamHash = hashMemory(&m_subsampling, sizeof(m_subsampling), newCamHash);
        newCamHash = hashMemory(&m_bboxMin, sizeof(m_bboxMin), newCamHash);
        newCamHash = hashMemory(&m_bboxMax, sizeof(m_bboxMax), newCamHash);
        newCamHash = hashMemory(&m_voxel_size, sizeof(m_voxel_size), newCamHash);
        newCamHash = hashMemory(&m_subblock_start, sizeof(m_subblock_start), newCamHash);
        newCamHash = hashMemory(&m_subblock_size, sizeof(m_subblock_size), newCamHash);
        newCamHash = hashMemory(&m_subblock_enabled, sizeof(m_subblock_enabled), newCamHash);
        newCamHash = hashMemory(&m_show_model_space, sizeof(m_show_model_space), newCamHash);
        newCamHash = hashMemory(&m_show_brick_cache, sizeof(m_show_brick_cache), newCamHash);
        newCamHash = hashMemory(&m_show_lod, sizeof(m_show_lod), newCamHash);
        newCamHash = hashMemory(&m_show_step_count, sizeof(m_show_step_count), newCamHash);
        newCamHash = hashMemory(&m_background_color_a, sizeof(m_background_color_a), newCamHash);
        newCamHash = hashMemory(&m_background_color_b, sizeof(m_background_color_b), newCamHash);
        newCamHash = hashMemory(&m_global_illumination_enabled, sizeof(m_global_illumination_enabled), newCamHash);
        newCamHash = hashMemory(&m_envmap_enabled, sizeof(m_envmap_enabled), newCamHash);
        newCamHash = hashMemory(&m_light_direction, sizeof(m_light_direction), newCamHash);
        newCamHash = hashMemory(&m_light_intensity, sizeof(m_light_intensity), newCamHash);
        newCamHash = hashMemory(&m_ambient_occlusion_dist_strength, sizeof(m_ambient_occlusion_dist_strength),
                                newCamHash);
        newCamHash = hashMemory(&m_max_path_length, sizeof(m_max_path_length), newCamHash);
        newCamHash = hashMemory(&m_max_steps, sizeof(m_max_steps), newCamHash);
        newCamHash = hashMemory(&m_cook_torrance_shading, sizeof(m_cook_torrance_shading), newCamHash);
        newCamHash = hashMemory(&m_show_normals, sizeof(m_show_normals), newCamHash);
        newCamHash = hashMemory(&m_show_envmap, sizeof(m_show_envmap), newCamHash);
        newCamHash = hashMemory(&m_factor_ambient, sizeof(m_factor_ambient), newCamHash);
        newCamHash = hashMemory(&m_ratio_spec_diff, sizeof(m_ratio_spec_diff), newCamHash);
        newCamHash = hashMemory(&m_tonemap_enabled, sizeof(m_tonemap_enabled), newCamHash);
        newCamHash = hashMemory(&m_shadow_pathtracing_ratio, sizeof(m_shadow_pathtracing_ratio), newCamHash);
        newCamHash = hashMemory(&m_max_decoding_lod, sizeof(m_max_decoding_lod), newCamHash);
        newCamHash = hashMemory(&m_lod_bias, sizeof(m_lod_bias), newCamHash);
        newCamHash = hashMemory(&m_accum_frames, sizeof(m_accum_frames), newCamHash);
        if (newCamHash != m_camHash || m_clear_accum_every_frame || m_pass->willCacheBeResetOnNextCall()) {
            m_framesSinceCameraMove = 0u;
            m_camHash = newCamHash;
        }
        m_urender_info->setUniform<uint32_t>("g_camera_still_frames", m_framesSinceCameraMove);
        m_urender_info->setUniform<glm::ivec2>("g_subsampling_pixel", PixelSequence::haltonNxNVec(m_subsampling)[m_framesSinceCameraMove % ((1 << m_subsampling)*(1 << m_subsampling))]);
        // random seed
        m_urender_info->setUniform<float>("g_random_seed", static_cast<float>(m_frame) / 10000.f);
        m_urender_info->setUniform<uint32_t>("g_swapchain_index", m_pass->getActiveIndex());
    }

    // volume / Compressed Segmentation Volume uniform
    {
        uint32_t brick_size = m_compressed_segmentation_volume->getBrickSize();
        m_usegmented_volume_info->setUniform<glm::uvec3>("g_vol_dim", m_compressed_segmentation_volume->getVolumeDim());
        m_usegmented_volume_info->setUniform<glm::vec3>("g_voxel_size", m_voxel_size);
        m_usegmented_volume_info->setUniform<glm::ivec3>("g_vol_translation", m_subblock_enabled ? m_subblock_start : glm::ivec3(0));
        m_usegmented_volume_info->setUniform<glm::vec3>("g_physical_vol_dim", physical_voldim);
        m_usegmented_volume_info->setUniform<glm::vec3>("g_normalized_volume_size", normalized_volume_size);
        m_usegmented_volume_info->setUniform<uint32_t>("g_vol_max_label", 1000000);
        m_usegmented_volume_info->setUniform<uint32_t>("g_brick_size", brick_size);
        m_usegmented_volume_info->setUniform<glm::uvec3>("g_brick_count",
                                                         m_compressed_segmentation_volume->getBrickCount());
        auto lod_count = m_compressed_segmentation_volume->getLodCountPerBrick();
        m_usegmented_volume_info->setUniform<uint32_t>("g_lod_count", lod_count);
        m_usegmented_volume_info->setUniform<uint32_t>("g_frame", m_frame);
        m_usegmented_volume_info->setUniform<uint32_t>("g_max_decoding_lod", glm::min(static_cast<uint32_t>(m_max_decoding_lod), lod_count));
        m_usegmented_volume_info->setUniform<uint32_t>("g_cache_capacity", m_cache_capacity);
        m_usegmented_volume_info->setUniform<uint32_t>("g_free_stack_capacity", m_free_stack_capacity);
        m_usegmented_volume_info->setUniform<uint32_t>("g_request_buffer_capacity", m_max_detail_requests_per_frame);
        m_usegmented_volume_info->setUniform<glm::uvec2>("g_encoding_buffer_address", m_encoding_buffer_address);
        m_usegmented_volume_info->setUniform<glm::uvec2>("g_detail_buffer_address", m_detail_buffer_address);
    }
}

void CompressedSegmentationVolumeRenderer::initGui(vvv::GuiInterface *gui) {
    Renderer::initGui(gui);
//    GuiInterface::GuiElementList* g = gui->get("Compressed Segmentation Volume Renderer");
    GuiInterface::GuiElementList* g_gen = gui->get("General");
    GuiInterface::GuiElementList* g_dis = gui->get("Display");
    GuiInterface::GuiElementList* g_render = gui->get("Rendering");
    GuiInterface::GuiElementList* g_dev = gui->get("Development");
    // we create an invisible GUI window to export all parameters but keep them hidden from the user
    gui->getWindow("Development")->setVisible(!m_release_version);
    // specify a docking layout for the windows
    gui->setDockingLayout({{"General", "d"},
                           {"Rendering", "d"},
                           {"Display", "d"},
                           {"Materials", "r"},
                           {"Development", "Materials"}});

    // General options
//ToDo: addFloatRange2 to the GUIInterface
#ifdef IMGUI
    g_gen->addCustomCode(
            [this]() {
                auto old_voxel_size = m_voxel_size;
                ImGui::InputFloat3("Voxel Size", &m_voxel_size.x);
                if(glm::any(glm::lessThanEqual(m_voxel_size, glm::vec3(0.f)))) {
                    Logger(WARN) << "voxel size must be > 0 in all dimensions! Resetting..";
                    m_voxel_size = old_voxel_size;
                }
            },
            "Voxel Size");
    g_gen->addCustomCode(
            [this]() {
                ImGui::DragFloatRange2("Splitting Plane X", &m_bboxMin.x, &m_bboxMax.x, 0.01f, 0.0f, 1.f, "Min: %.2f %%", "Max: %.2f %%");
                ImGui::DragFloatRange2("Splitting Plane Y", &m_bboxMin.y, &m_bboxMax.y, 0.01f, 0.0f, 1.f, "Min: %.2f %%", "Max: %.2f %%");
                ImGui::DragFloatRange2("Splitting Plane Z", &m_bboxMin.z, &m_bboxMax.z, 0.01f, 0.0f, 1.f, "Min: %.2f %%", "Max: %.2f %%");
                m_bboxMin = glm::clamp(m_bboxMin, glm::vec3(0.f), glm::vec3(1.f));
                m_bboxMax = glm::clamp(m_bboxMax, m_bboxMin, glm::vec3(1.f));
            },
            "Splitting Planes");
#endif
    g_gen->addSeparator();
    g_gen->addAction([this]() {
        if (!pfd::settings::available()) {
            Logger(WARN) << "Can not open file dialog for screenshot export. Using default file ./volcanite_output.png";
            m_download_frame_to_image_file = "./volcanite_output.png";
            return;
        }

        // Open a file dialog to choose a file
        auto selected_file = pfd::save_file("Save Screenshot", pfd::path::home(),
                                            { "Image File (.png .jpg .jpeg)", "*.png *.jpg *.jpeg", "All Files", "*" });
        if(!selected_file.result().empty()) {
            m_download_frame_to_image_file = selected_file.result();
            if(!m_download_frame_to_image_file->ends_with(".png"))
                m_download_frame_to_image_file->append(".png");
        }
    }, "Screenshot");
    //
    g_gen->addAction([this]() {
        std::string file;
        if (!pfd::settings::available()) {
            Logger(WARN) << "Can not open file dialog. Using default file ./parameters.vcfg";
            file = "./parameters.vcfg";
        }

        // Open a file dialog to choose a file
        auto selected_file = pfd::open_file("Import Parameters", pfd::path::home(),
                                            { "Parameter Config (.vcfg)", "*.vcfg", "All Files", "*" });
        if(!selected_file.result().empty())
            file = selected_file.result().at(0);

        std::ifstream in(file);
        if(in.is_open()) {
            if (!readParameters(in, VOLCANITE_VERSION))
                Logger(WARN) << "Could not import parameters from " << file;
            in.close();
        }
    }, "Import Parameters");
    g_gen->addAction([this]() {
        std::string file;
        if (!pfd::settings::available()) {
            Logger(WARN) << "Can not open file dialog. Using default file ./parameters.vcfg";
            file = "./parameters.vcfg";
        }

        // Open a file dialog to choose a file
        auto selected_file = pfd::save_file("Export Parameters", pfd::path::home(),
                                            { "Parameter Config (.vcfg)", "*.vcfg", "All Files", "*" });
        if(!selected_file.result().empty())
            file = selected_file.result();

        if(!file.ends_with(".vcfg"))
            file.append(".vcfg");

        std::ofstream out(file);
        if(out.is_open()) {
            if (!writeParameters(out, VOLCANITE_VERSION))
                Logger(WARN) << "Could not export parameters to " << file;
            out.close();
        }
    }, "Export Parameters");
    //
    g_gen->addSeparator();
    g_gen->addDynamicText(&m_gui_device_mem_text);

    // Displaying and render resolution
    g_dis->addColor(&m_background_color_a, "Background Color A");
    g_dis->addColor(&m_background_color_b, "Background Color B");
    g_dis->addInt(&m_accum_frames, "Accumulation Frames");
    g_dis->addProgress([this]() { return static_cast<float>(m_framesSinceCameraMove) / static_cast<float>(m_accum_frames); }, "Progress");
    g_dis->addInt(&m_subsampling, "Subsampling Resolution", 0, 3, 1);
    //
    g_dis->addSeparator();
    g_dis->addDynamicText(&m_gui_resolution_text);
    g_dis->addBool([this](bool b) { if(getCtx()->getWsi()) getCtx()->getWsi()->setWindowResizable(b); }, [this]() { return getCtx()->getWsi() != nullptr && getCtx()->getWsi()->isWindowResizable(); }, "Resizable Window");
    g_dis->addAction([this]() { getCtx()->getWsi()->setWindowSize(1920, 1080); }, "1920x1080 FullHD");
    g_dis->addAction([this]() { getCtx()->getWsi()->setWindowSize(3840, 2160); }, "3840x2160 4K");

    // Materials
    if(m_csgv_db) {
        gui->get("Materials")->addTFSegmentedVolume(&m_materials, m_csgv_db->getAttributeNames(), m_csgv_db->getAttributeMinMax(), [this](int m) { updateSegmentedVolumeMaterial(m); }, "Materials");
    }

    // Path Tracing / Rendering
    g_render->addFloat(&m_factor_ambient, "Constant Color", 0.0f, 1.f, 0.05f, 2);
    g_render->addBool(&m_envmap_enabled, "Environment Map");
    g_render->addFloat(&m_light_intensity, "Light Intensity", 0.f, 4.f, 0.05f, 2);
    g_render->addDirection(&m_light_direction, "Light Direction");
    g_render->addSeparator();
    g_render->addBool(&m_cook_torrance_shading, "Local Shading");
    g_render->addFloat(&m_ratio_spec_diff, "Specular / Diffuse Shading Ratio", 0.0f, 1.0f, 0.05f, 2);
    g_render->addSeparator();
    g_render->addBool(&m_global_illumination_enabled, "Global Illumination");
    g_render->addFloat(&m_shadow_pathtracing_ratio, "Direct Light / Pathtracing Ratio", 0.f, 1.f, 0.1f, 1);
    g_render->addInt(&m_max_path_length, "Path Length", 1, 32, 1);

    // Development
    g_dev->addInt(&m_max_steps, "Max DDA Steps", 16, 4096, 16);
    g_dev->addFloat(&m_lod_bias, "LOD bias", -4.f, 4.f, 0.1f, 1.f);
    g_dev->addBool(&m_blue_noise, "Blue Noise Shift");
    g_dev->addBool(&m_denoise, "Denoising");
    g_dev->addFloat(&m_difference_depth_denoising, "difference depth denoising", 0.0f, 1.f, 0.004, 3);
    g_dev->addFloat(&m_spatial_sigma, "Spatial Sigma", 0.001f, 5.f, 0.01, 2);
    g_dev->addFloat(&m_depth_sigma, "Depth Sigma", 0.001f, 5.f, 0.01, 2);
    g_dev->addInt(&m_denoise_filter_kernel_size, "Denoise Filter Kernel Size", 0, 10, 1);
    g_dev->addBool(&m_tonemap_enabled, "Tone Mapping");
    g_dev->addSeparator();
    g_dev->addLabel("Debug");
    g_dev->addInt(&m_max_decoding_lod, "Max. Decoding LoD", 0, 6, 1);
    g_dev->addBool(&m_show_model_space, "Show Model Space");
    g_dev->addBool(&m_show_brick_cache, "Show Brick Cache");
    g_dev->addBool(&m_show_lod, "Show LOD Levels");
    g_dev->addBool(&m_show_step_count, "Show Ray Step Count");
    g_dev->addBool(&m_show_envmap, "Show Environment Map");
    g_dev->addBool(&m_show_normals, "Show Normals");
    g_dev->addAction([this]() { getCamera()->reset(); }, "Reset Camera");
    g_dev->addAction([this]() { getCamera()->orbital = !getCamera()->orbital; getCamera()->reset(); }, "Switch Camera Mode");
    g_dev->addAction(
            [this]() {
                if (m_pass)
                    m_pass->resetCacheOnNextCall();
            },
            "Hard Reset Brick Cache");
    g_dev->addBool(&m_clear_cache_every_frame, "Clear Cache Every Frame");
    g_dev->addBool(&m_clear_accum_every_frame, "Clear Accumulation Every Frame");
    g_dev->addSeparator();
}

    void CompressedSegmentationVolumeRenderer::updateDeviceMemoryUsage() {
        auto bu = getMemoryHeapBudgetAndUsage(*getCtx());
        size_t total = getMemoryHeapSize(*getCtx());
        std::stringstream ss;
        ss.precision(4);
        ss << "GPU Memory: " << static_cast<float>(bu.second) / 1073741824.f << "/"
                             << static_cast<float>(bu.first) / 1073741824.f << "/"
                             << static_cast<float>(total) / 1073741824.f << " GB (used/avail/total)";
        m_gui_device_mem_text = ss.str();
    }

    void CompressedSegmentationVolumeRenderer::updateSegmentedVolumeMaterial(int m) {
        constexpr int TF_WIDTH = 256;
        if(m_mostRecentFrame.has_value())
            getCtx()->sync->hostWaitOnDevice(m_mostRecentFrame->renderingComplete);
        if (m_materialTransferFunctions.size() < m_materials.size())
            m_materialTransferFunctions.resize(m_materials.size(), nullptr);
        m_materialTransferFunctions[m] = m_materials[m].tf->rasterize(getCtx(), TF_WIDTH);
        auto [tf1dAwait, tf1dStagingBuf] = m_materialTransferFunctions[m]->upload();

        getCtx()->sync->hostWaitOnDevice({tf1dAwait});
        m_pass->setImageSamplerArray("s_transferFunctions", m, m_materialTransferFunctions[m]->texture(), vk::ImageLayout::eReadOnlyOptimal, false);

        // reset accumulation
        m_camHash = static_cast<size_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        // mark material dirty
        m_gpu_material_changed[m] = true;
    }

    vvv::AwaitableList CompressedSegmentationVolumeRenderer::updateAttributeBuffers() {
        // ToDo: this whole thing could be cleaned up. Encapsulate attribute / material / data buffers in another struct or class at least. And see the notes regarding the attribute upload below.
        if(!m_csgv_db)
            throw std::runtime_error("Missing csgv database at attribute buffer creation.");

        // check which attributes should be present in GPU memory
        std::vector<bool> attributeNeeded(m_attribute_start_position.size(), false);
        for(int m = 0; m < m_materials.size(); m++) {
            if(m_materials[m].discrAttribute > 0)
                attributeNeeded[m_materials[m].discrAttribute] = true;
            if(m_materials[m].tfAttribute > 0)
                attributeNeeded[m_materials[m].tfAttribute] = true;
        }

        // check at which positions in the attribute buffer an element starts
        bool nothingToDo = true;
        int numberOfSlots = m_max_attribute_buffer_size / sizeof(float) / m_csgv_db->getLabelCount();
        int requiredSlots = 0;
        std::vector<int> possiblePositions(numberOfSlots, -1);
        for(int a = 0; a < m_csgv_db->getAttributeCount(); a++) {
            if(attributeNeeded[a]) {
                requiredSlots++;

                if(m_attribute_start_position[a] >= 0) {
                    assert(possiblePositions.at(m_attribute_start_position[a] / m_csgv_db->getLabelCount()) < 0 && "two attributes were assigned to the same position in the attribute buffer");
                    possiblePositions.at(m_attribute_start_position[a] / m_csgv_db->getLabelCount()) = a;
                }
                else {
                    nothingToDo = false;
                }
            }
            else
                m_attribute_start_position[a] = -1;

        }

        if(nothingToDo)
            return {};

        assert(m_attribute_start_position[0] == -1 && "first attribute (csgv_id) should not be uploaded to the GPU");

        if(requiredSlots > numberOfSlots)
            throw::std::runtime_error("attribute buffer is not large enough with " + std::to_string(numberOfSlots) + " out of " + std::to_string(requiredSlots) + " required slots");

        // store all attributes back to back
        // ToDo: upload attributes independently from another instead of as one large buffer?
        // ToDo: only upload the number of requried Slots (would need to re-pack attributes each frame) instead of all slots?
        std::vector<float> attributes(numberOfSlots * m_csgv_db->getLabelCount());

        // put attributes in available slots
        for(int a = 0; a < m_csgv_db->getAttributeCount(); a++) {
            if(attributeNeeded[a] && m_attribute_start_position[a] < 0) {
                for(int p = 0; p < possiblePositions.size(); p++) {
                    if(possiblePositions[p] < 0) {
                        m_attribute_start_position[a] = static_cast<int>(p * m_csgv_db->getLabelCount());
                        possiblePositions[p] = a;
                        break;
                    }
                }
                if(m_attribute_start_position[a] < 0)
                    Logger(WARN) << "could not find an attribute slot for attribute " << m_csgv_db->getAttributeNames()[a] << " in " << numberOfSlots << " slots";
            }
            // copy attribute to buffer
            if(m_attribute_start_position[a] >= 0) {
                m_csgv_db->getAttribute(a, &attributes[m_attribute_start_position[a]], attributes.size() - m_attribute_start_position[a]);
            }
        }

        // reset all accumulation buffers
        m_camHash = static_cast<size_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());

        auto [attr_upload_finished, _attr_staging_buffer] = m_attribute_buffer->uploadWithStagingBuffer(attributes.data(), attributes.size() * sizeof(float), {.queueFamily = getCtx()->getQueueFamilyIndices().transfer.value()});
        getCtx()->sync->hostWaitOnDevice({attr_upload_finished});
        // can't just return the awaitable as _attr_staging_buffer can not be freed yet
        // return {attr_upload_finished};
        return {};
    }

} // namespace vvv
