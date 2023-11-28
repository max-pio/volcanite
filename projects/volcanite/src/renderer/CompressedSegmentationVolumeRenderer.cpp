#include "volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp"

#include <vvv/core/Buffer.hpp>
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
    assert(m_usegmented_volume_info && m_urender_info && m_compressed_segmentation_volume && "CompressedSegmentationVolumeRenderer data missing!");

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
        // we have a getDevice().waitIdle() at the end of this scope anyway, so we won't add those awaitables to any list

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

        // wait until everything is uploaded
        getCtx()->getDevice().waitIdle();
        m_data_changed = false;
    }

    // wait for the last frame to finish execution (which will also mean that the previous upload of the detail starts finished)
    getCtx()->sync->hostWaitOnDevice(awaitBeforeExecution);

    // if a screenshot export was requested, we do this here
    if(m_download_frame_to_image_file.has_value() && m_mostRecentFrame.has_value()) {
        Logger(INFO) << "exporting screenshot to " << m_download_frame_to_image_file.value();
        try {
            m_mostRecentFrame->texture->writeFile(m_download_frame_to_image_file.value());
        }
        catch(std::runtime_error e) {
            Logger(ERROR) << "image export error: " << e.what();
        }
        m_download_frame_to_image_file = {};
    }

    // we have to know if the detail buffer is still in an uploading state. If yes, we don't do anything else with the detail buffer
    bool detail_buffer_dirty = !m_compressed_segmentation_volume->isUsingSeparateDetail()
                               || (m_detail_starts_staging.first != nullptr && !getCtx()->sync->isAwaitableResolved(m_detail_starts_staging.first))
                               || (m_detail_staging.first != nullptr && !getCtx()->sync->isAwaitableResolved(m_detail_staging.first));
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
        const uint32_t zeroes[2] = {0u, 0u};
        m_detail_requests_buffer->upload(m_max_detail_requests_per_frame * sizeof(uint32_t), &zeroes, 2 * sizeof(uint32_t));

        // one element after, we store the current cache usage as number of used 2x2x2 elements
        cache_usage = requested_ids[m_max_detail_requests_per_frame + 1u];
    }
    else {
        // ToDo: download the cache_usage also if we don't use detail separation
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

    if (m_compressed_segmentation_volume->isUsingSeparateDetail()) {
        if(!detail_buffer_dirty && m_detail_update_required && m_constructed_detail_starts.back() > 0u) {
            m_detail_starts_staging = m_detail_starts_buffer->uploadWithStagingBuffer(m_constructed_detail_starts.data(), m_constructed_detail_starts.size() * sizeof(uint32_t), {.queueFamily = getCtx()->getQueueFamilyIndices().transfer.value()});
            m_detail_staging = m_detail_buffer->uploadWithStagingBuffer(m_constructed_detail.data(), m_constructed_detail_starts.back() * sizeof(uint32_t), {.queueFamily = getCtx()->getQueueFamilyIndices().transfer.value()});
//            Logger(INFO) << "upload " << m_constructed_detail_starts.back() << " elements";
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
        Logger(INFO) << cache_state.str() << "   |   " << m_last_gpu_stats.gpu_raymarch_samples << " block samples - " << static_cast<double>(m_last_gpu_stats.gpu_raymarch_samples) / static_cast<double>(m_last_gpu_stats.gpu_bbox_hits) << " per ray.";
        if (decoded_bytes_in_frame > 0)
            Logger(INFO) << "decoded " << std::fixed << std::setprecision(3) << static_cast<double>(decoded_bytes_in_frame) / 1000. / 1000. << " MB";
    }

    //m_pass->setStorageImage("outDepth", *m_outDepth);
    //m_pass->setStorageImage("outColor", *m_outColor);
    m_pass->setStorageImage("inpaintedOutColor", *m_inpaintedOutColor);
    // feedback texture ping pong for the inpainting shader
    m_pass->setStorageImage("feedbackIn", *m_feedback_tex[m_frame % 2u]);
    m_pass->setStorageImage("feedbackOut", *m_feedback_tex[1u - (m_frame % 2u)]);


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
            std::vector<uint32_t> *detail_starts = m_compressed_segmentation_volume->getDetailStarts();
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
            std::vector<uint32_t> *detail = m_compressed_segmentation_volume->getDetail();
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
            if(m_max_detail_byte_size / sizeof(uint32_t) < optimal_detail_size || true) {      // disable automatic full upload for testing
                m_detail_capacity = m_max_detail_byte_size / sizeof(uint32_t);
            }
            // we can fit the compelte detial buffer onto the GPU
            else {
                m_detail_capacity = optimal_detail_size;
                detail_buffer_fits_whole_detail = true;
            }
            m_constructed_detail_starts.resize(bricks_in_volume + 1u, 0u);
            m_constructed_detail.resize(m_detail_capacity, 0u);
        }
    }
    else {
        assert(false && "we would like to know the Compressed Segmentation Volume size before we allocate any memory");
    }
    m_brick_starts_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_brick_start_buffer", .byteSize = (bricks_in_volume + 1u)*sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    m_encoding_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_encoding_buffer", .byteSize = encoding_byte_size, .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});

    m_cache_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_cache_buffer", .byteSize = m_cache_capacity * (2u*2u*2u) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    m_free_stack_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_free_stack_buffer", .byteSize = (m_free_stack_capacity * (lods_in_volume - 1u) + (lods_in_volume + 1u)) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    m_cache_info_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_cache_info_buffer", .byteSize = bricks_in_volume*sizeof(uint32_t)*4u, .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
    m_assign_info_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_assign_buffer", .byteSize = (1u + (lods_in_volume - 1u) * 3u) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});

    if(m_detail_capacity > 0ul) {
        m_detail_requests_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_detail_requests_buffer", .byteSize = (m_max_detail_requests_per_frame + 2u) * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc, .memoryUsage = vk::MemoryPropertyFlagBits::eHostVisible});
        m_detail_starts_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_detail_starts_buffer", .byteSize = (bricks_in_volume + 1u)*sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});
        m_detail_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_detail_buffer", .byteSize = m_detail_capacity * sizeof(uint32_t), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, .memoryUsage = vk::MemoryPropertyFlagBits::eDeviceLocal});

        if(detail_buffer_fits_whole_detail) {
            Logger(WARN) << "GPU detail buffer fits the whole detail level. Performing full upload, effectively disabling detail streaming. Consider setting use_detail to false for better performance!";
            m_detail_staging = m_detail_buffer->uploadWithStagingBuffer(m_compressed_segmentation_volume->getDetail()->data(), m_compressed_segmentation_volume->getDetail()->size() * sizeof(uint32_t));
            m_detail_starts_staging = m_detail_starts_buffer->uploadWithStagingBuffer(m_compressed_segmentation_volume->getDetailStarts()->data(),
                                                                                      m_compressed_segmentation_volume->getDetailStarts()->size() * sizeof(uint32_t));
            m_constructed_detail_starts = *m_compressed_segmentation_volume->getDetailStarts(); // just to be sure: we tell the CPU side that every brick is uploaded
            getCtx()->sync->hostWaitOnDevice({m_detail_staging.first, m_detail_starts_staging.first});
            m_detail_staging = {nullptr, nullptr};
            m_detail_starts_staging = {nullptr, nullptr};
        }
    }

    // GPU stats buffer
    m_gpu_stats_buffer = std::make_shared<Buffer>(ctx, BufferSettings{.label = "CompressedSegmentationVolumeRenderer.m_gpu_stats_buffer", .byteSize = sizeof(GPUStats), .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc, .memoryUsage = vk::MemoryPropertyFlagBits::eHostVisible});

    // Set camera to a nice start position
    auto camera = getCamera();
    camera->position_world_space = {-0.8, 0.6666, -0.8};
    camera->rotation_x = 0.6;
    camera->rotation_y = 2.25;

    if(m_compressed_segmentation_volume)
        m_data_changed = true; // trigger re-upload to new buffers
}

void CompressedSegmentationVolumeRenderer::releaseResources() {
    m_gpu_stats_buffer = nullptr;
    m_assign_info_buffer = nullptr;
    m_cache_info_buffer = nullptr;
    m_free_stack_buffer = nullptr;
    m_cache_buffer = nullptr;
    m_encoding_buffer = nullptr;
    m_brick_starts_buffer = nullptr;
    m_detail_buffer = nullptr;
    m_detail_starts_buffer = nullptr;
    m_detail_requests_buffer = nullptr;
    m_detail_starts_staging.second = nullptr;
    m_detail_staging.second = nullptr;
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
    // ToDo: does this work? if we're rendering without a GLFW window / WSI, we're disabling MultiBuffering
    if(getCtx()->getWsi())
        m_pass = std::make_unique<PassCompSegVolRender>(getCtx(), getCtx()->getWsi()->stateInFlight(), shader_defines);
    else
        m_pass = std::make_unique<PassCompSegVolRender>(getCtx(), NoMultiBuffering, shader_defines);
    m_pass->allocateResources();
    m_pass->resetCacheOnNextCall();
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
    m_pass->setVolumeInfo(m_compressed_segmentation_volume->getBrickCount(), m_compressed_segmentation_volume->getLodCountPerBrick());
    // reset all camera hashes and frame counters
    m_camHash = static_cast<size_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    m_framesSinceCameraMove = 0;
    m_frame = 0u;
}

void CompressedSegmentationVolumeRenderer::releaseShaderResources() {
    m_usegmented_volume_info = nullptr;
    m_urender_info = nullptr;
    if(m_pass)
        m_pass->freeResources();
    m_pass = nullptr;
}



void CompressedSegmentationVolumeRenderer::initSwapchainResources() {
    const auto screen = getRenderResolution();

    // tell the pass the new invocation size
    m_pass->setImageInfo(screen.width, screen.height);

    // recreate all swapchain image sized textures
    vvv::AwaitableList reinitDone;
    m_feedback_tex[0] = m_pass->reflectTexture("feedbackIn", {.width = screen.width, .height = screen.height, .format = vk::Format::eR32G32B32A32Sfloat, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    m_feedback_tex[1] = m_pass->reflectTexture("feedbackOut", {.width = screen.width, .height = screen.height, .format = vk::Format::eR32G32B32A32Sfloat, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    for (auto & texture : m_feedback_tex) {
        texture->ensureResources();
        const auto layoutTransformDone = texture->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eAllCommands);
        reinitDone.push_back(layoutTransformDone);
    }
    m_inpaintedOutColor = m_pass->reflectTextures(
        "inpaintedOutColor", {.width = screen.width, .height = screen.height, .format = vk::Format::eR8G8B8A8Unorm, .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
    for (auto& texture : *m_inpaintedOutColor){
        texture->ensureResources();
        const auto layoutTransformDone = texture->setImageLayout(vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eAllCommands);
        reinitDone.push_back(layoutTransformDone);
    }

    m_gui_resolution_text = "Render resolution: " + std::to_string(screen.width) + "x" + std::to_string(screen.height);
    getCtx()->sync->hostWaitOnDevice(reinitDone);

    // trigger a temporal accumulation flush
    m_camHash = static_cast<size_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
}

void CompressedSegmentationVolumeRenderer::releaseSwapchain() {
    m_mostRecentFrame->texture = nullptr;
    m_mostRecentFrame->renderingComplete = {};
    if (m_outColor)
        m_outColor = nullptr;
    if(m_outDepth)
        m_outDepth = nullptr;
    if(m_feedback_tex[0])
        m_feedback_tex[0] = nullptr;
    if(m_feedback_tex[1])
        m_feedback_tex[1] = nullptr;
    if (m_inpaintedOutColor)
        m_inpaintedOutColor.reset();// = nullptr;
}

void CompressedSegmentationVolumeRenderer::resetGPU() {
    releaseGui();
    releaseSwapchain();
    releaseShaderResources();
    releaseResources();

    //m_compressed_segmentation_volume = nullptr;
}

void CompressedSegmentationVolumeRenderer::updateUniformDescriptorset() {
    const auto camera = getCamera();
    const auto screenExtent = getRenderResolution();

    glm::vec3 voldim = glm::vec3(m_compressed_segmentation_volume->getVolumeDim());
    glm::vec3 physical_voldim = voldim * m_voxel_size;

    // size in world space: uniformly scaled so that the largest component is one
    float scalingFactor = glm::max(physical_voldim.x, glm::max(physical_voldim.y, physical_voldim.z));
    glm::vec4 normalized_volume_size(physical_voldim / scalingFactor, 1.f);

    // render info uniform
    {
        m_urender_info->setUniform<glm::vec4>("g_background_color_a", m_background_color_a);
        m_urender_info->setUniform<glm::vec4>("g_background_color_b", m_background_color_b);
        glm::uvec2 label_minmax;
        uint64_t tmp_64bit = 1ul;   // store this in a variable to force 64 bit computation
        label_minmax.x = static_cast<uint32_t>((tmp_64bit << m_label_minmax.x) - 1);
        tmp_64bit = 1ul;
        label_minmax.y = static_cast<uint32_t>((tmp_64bit << m_label_minmax.y) - 1ul);
        m_urender_info->setUniform<glm::uvec2>("g_label_minmax", label_minmax);
        uint32_t empty_label = static_cast<uint32_t>(m_empty_label);
        m_urender_info->setUniform<uint32_t>("g_empty_label", empty_label);
        m_urender_info->setUniform<float>("g_transferFunction_limits_min", 0);
        m_urender_info->setUniform<float>("g_transferFunction_limits_max", 1000);
        m_urender_info->setUniform<int32_t>("g_shadow_ray_enable", m_shadow_ray_enabled ? 1 : 0);
        m_urender_info->setUniform<float>("g_shadow_ao_ray_distr", m_shadow_ao_ray_distr);
        m_urender_info->setUniform<int32_t>("g_tonemap_enable", m_tonemap_enabled ? 1 : 0);
        m_urender_info->setUniform<glm::vec2>("g_ao_dist_strength", m_ambient_occlusion_dist_strength);
        m_urender_info->setUniform<glm::vec3>("g_light_direction", m_light_direction);
        m_urender_info->setUniform<float>("g_light_intensity", m_light_intensity);
        m_urender_info->setUniform<float>("g_stepSize", m_step_size * scalingFactor);
        m_urender_info->setUniform<int32_t>("g_maxSteps", m_max_steps);
        m_urender_info->setUniform<int32_t>("g_subsampling", (1 << m_subsampling));
        // bbox is the volume dimension in voxels centered around the origin (if no bbox reduction is applied)
        m_urender_info->setUniform<glm::vec4>("g_bboxMin", glm::vec4(m_bboxMin, 1.f));
        m_urender_info->setUniform<glm::vec4>("g_bboxMax", glm::vec4(m_bboxMax, 1.f));
        m_urender_info->setUniform<uint32_t>("g_dda_traversal", m_dda_traversal ? 1 : 0);
        m_urender_info->setUniform<uint32_t>("g_blue_noise", m_blue_noise ? 1 : 0);
        m_urender_info->setUniform<float>("g_opacityThreshold", 0.5); // TODO: we have this low opacity treshold to render opaque first hits
        m_urender_info->setUniform<glm::vec3>("g_camera_position_world_space", camera->position_world_space);
        m_urender_info->setUniform<float>("g_lod_bias", m_lod_bias);

        // debug
        m_urender_info->setUniform<uint32_t>("g_debug_model_space", m_show_model_space ? 1 : 0);
        m_urender_info->setUniform<uint32_t>("g_debug_brick_cache", m_show_brick_cache ? 1 : 0);
        m_urender_info->setUniform<uint32_t>("g_debug_lod", m_show_lod ? 1 : 0);
        m_urender_info->setUniform<uint32_t>("g_debug_step_count", m_show_step_count ? 1 : 0);

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
        world_to_model_space = glm::translate(glm::scale(glm::mat4(1.f), glm::vec3(scalingFactor)), glm::vec3(normalized_volume_size /2.f));
        m_urender_info->setUniform<glm::mat4x4>("g_model_to_world_space", glm::inverse(world_to_model_space));
        m_urender_info->setUniform<glm::mat4x4>("g_world_to_model_space", world_to_model_space);
        m_urender_info->setUniform<glm::mat3x3>("g_world_to_model_space_dir",  glm::mat3(world_to_model_space));
        m_urender_info->setUniform<float>("g_world_to_model_space_scaling", scalingFactor);
        const auto world_to_projection_space = camera->get_world_to_projection_space(screenExtent);
        const auto projection_to_world_space = glm::inverse(world_to_projection_space);
        m_urender_info->setUniform<glm::mat4x4>("g_world_to_projection_space", world_to_projection_space);
        m_urender_info->setUniform<glm::mat4x4>("g_projection_to_world_space", projection_to_world_space);
        m_urender_info->setUniform<glm::mat4x4>("g_projection_to_view_space", glm::inverse(camera->get_view_to_projection_space(screenExtent)));
        m_urender_info->setUniform<glm::mat4x4>("g_view_to_world_space", glm::inverse(camera->get_world_to_view_space()));
        m_urender_info->setUniform<glm::mat4x4>("g_view_to_projection_space", camera->get_view_to_projection_space(screenExtent));
        m_urender_info->setUniform<glm::mat4x4>("g_world_to_view_space", camera->get_world_to_view_space());
        glm::mat4 projection_to_world_space_no_translation = projection_to_world_space;
        glm::vec2 viewportScale(2.0f / screenExtent.width, 2.0f / screenExtent.height);
        glm::mat4 pixel_to_ray_direction_projection_space({viewportScale[0], 0.0f, 0.0f, 0.0f}, {0.0f, viewportScale[1], 0.0f, 0.0f},
                                                          {0.5f * viewportScale[0] - 1.0f, 0.5f * viewportScale[1] - 1.0f, 1.0f, 1.0f}, {0.f, 0.f, 0.f, 1.f});
        m_urender_info->setUniform<glm::mat3x3>("g_pixel_to_ray_direction_world_space", glm::mat3x3(projection_to_world_space_no_translation * pixel_to_ray_direction_projection_space));

        // detect if the camera was moved since the last frame (useful for progressive rendering etc.)
        // (or if any rendering parameters changed, technically not "camera" only anymore, but we can use it for resetting all accumulation buffers.)
        m_framesSinceCameraMove++;
        auto newCamHash = hashMemory(&world_to_projection_space[0].x, sizeof(glm::mat4));
        newCamHash = hashMemory(&m_bboxMin, sizeof(m_bboxMin), newCamHash);
        newCamHash = hashMemory(&m_bboxMax, sizeof(m_bboxMax), newCamHash);
        newCamHash = hashMemory(&m_voxel_size, sizeof(m_voxel_size), newCamHash);
        newCamHash = hashMemory(&m_show_model_space, sizeof(m_show_model_space), newCamHash);
        newCamHash = hashMemory(&m_show_brick_cache, sizeof(m_show_brick_cache), newCamHash);
        newCamHash = hashMemory(&m_show_lod, sizeof(m_show_lod), newCamHash);
        newCamHash = hashMemory(&m_show_step_count, sizeof(m_show_step_count), newCamHash);
        newCamHash = hashMemory(&m_background_color_a, sizeof(m_background_color_a), newCamHash);
        newCamHash = hashMemory(&m_background_color_b, sizeof(m_background_color_b), newCamHash);
        newCamHash = hashMemory(&m_label_minmax, sizeof(m_label_minmax), newCamHash);
        newCamHash = hashMemory(&m_empty_label, sizeof(m_empty_label), newCamHash);
        newCamHash = hashMemory(&m_shadow_ray_enabled, sizeof(m_shadow_ray_enabled), newCamHash);
        newCamHash = hashMemory(&m_light_direction, sizeof(m_light_direction), newCamHash);
        newCamHash = hashMemory(&m_light_intensity, sizeof(m_light_intensity), newCamHash);
        newCamHash = hashMemory(&m_ambient_occlusion_dist_strength, sizeof(m_ambient_occlusion_dist_strength), newCamHash);
        newCamHash = hashMemory(&m_step_size, sizeof(m_step_size), newCamHash);
        newCamHash = hashMemory(&m_tonemap_enabled, sizeof(m_tonemap_enabled), newCamHash);
        newCamHash = hashMemory(&m_shadow_ao_ray_distr, sizeof(m_shadow_ao_ray_distr), newCamHash);
        newCamHash = hashMemory(&m_max_decoding_lod, sizeof(m_max_decoding_lod), newCamHash);
        if(newCamHash != m_camHash || m_clear_accum_every_frame || m_pass->willCacheBeResetOnNextCall()) {
            m_framesSinceCameraMove = 0u;
            m_camHash = newCamHash;
        }
        m_urender_info->setUniform<uint32_t>("g_camera_still_frames", m_framesSinceCameraMove);
        // random seed
        m_urender_info->setUniform<float>("g_random_seed",  static_cast<float>(m_frame) / 10000.f);
        m_urender_info->setUniform<uint32_t>("g_swapchain_index", m_pass->getActiveIndex());
    }

    // volume / Compressed Segmentation Volume uniform
    {
        uint32_t brick_size = m_compressed_segmentation_volume->getBrickSize();
        m_usegmented_volume_info->setUniform<glm::uvec3>("g_vol_dim", m_compressed_segmentation_volume->getVolumeDim());
        m_usegmented_volume_info->setUniform<glm::vec3>("g_voxel_size", m_voxel_size);
        m_usegmented_volume_info->setUniform<glm::vec3>("g_physical_vol_dim", physical_voldim);
        m_usegmented_volume_info->setUniform<glm::vec3>("g_normalized_volume_size", normalized_volume_size);
        m_usegmented_volume_info->setUniform<uint32_t>("g_vol_max_label", 1000000);
        m_usegmented_volume_info->setUniform<uint32_t>("g_brick_size", brick_size);
        m_usegmented_volume_info->setUniform<glm::uvec3>("g_brick_count", m_compressed_segmentation_volume->getBrickCount());
        auto lod_count = m_compressed_segmentation_volume->getLodCountPerBrick();
        m_usegmented_volume_info->setUniform<uint32_t>("g_lod_count", lod_count);
        m_usegmented_volume_info->setUniform<uint32_t>("g_frame", m_frame);
        m_usegmented_volume_info->setUniform<uint32_t>("g_max_decoding_lod", m_max_decoding_lod);
        m_usegmented_volume_info->setUniform<uint32_t>("g_cache_capacity", m_cache_capacity);
        m_usegmented_volume_info->setUniform<uint32_t>("g_free_stack_capacity", m_free_stack_capacity);
        m_usegmented_volume_info->setUniform<uint32_t>("g_request_buffer_capacity", m_max_detail_requests_per_frame);
    }
}

    void CompressedSegmentationVolumeRenderer::initGui(vvv::GuiInterface *gui) {
        auto g = gui->get("Compressed Segmentation Volume Renderer");

        g->addColor(&m_background_color_a, "Background Color A");
        g->addColor(&m_background_color_b, "Background Color B");
        g->addFloat(&m_step_size, "Step Size", 0.0005f, 0.01f, 0.0005f, 4);
        if(!m_release_version) {
            g->addInt(&m_max_steps, "Max Steps", 1, 2048, 1);
            g->addInt(&m_subsampling, "Subsampling Factor (2^n)", 0, 2, 1);
        }
//ToDo: addFloatRange2 to the GUIInterface
#ifdef IMGUI
        g->addCustomCode(
                [this]() {
                    ImGui::DragFloatRange2("Splitting Plane X", &m_bboxMin.x, &m_bboxMax.x, 0.01f, 0.0f, 1.f, "Min: %.2f %%", "Max: %.2f %%");
                    ImGui::DragFloatRange2("Splitting Plane Y", &m_bboxMin.y, &m_bboxMax.y, 0.01f, 0.0f, 1.f, "Min: %.2f %%", "Max: %.2f %%");
                    ImGui::DragFloatRange2("Splitting Plane Z", &m_bboxMin.z, &m_bboxMax.z, 0.01f, 0.0f, 1.f, "Min: %.2f %%", "Max: %.2f %%");
                },
                "Splitting Planes");
        g->addCustomCode(
                [this]() {
                    auto old_voxel_size = m_voxel_size;
                    ImGui::InputFloat3("Voxel Size", &m_voxel_size.x);
                    if(glm::any(glm::lessThanEqual(m_voxel_size, glm::vec3(0.f)))) {
                        Logger(WARN) << "voxel size must be > 0 in all dimensions! Resetting..";
                        m_voxel_size = old_voxel_size;
                    }
                },
                "Voxel Size");
#endif
        g->addInt(&m_empty_label, "Empty Label");
        g->addInt(&m_label_minmax.x, "Label ID Min. 2^", 0, 32, 1);
        g->addInt(&m_label_minmax.y, "Label ID Max. 2^", 0, 32, 1);

        if(!m_release_version) {
            g->addFloat(&m_lod_bias, "LOD bias", -4.f, 4.f, 0.1f, 1.f);
            g->addBool(&m_blue_noise, "Blue Noise Shift");
            g->addBool(&m_dda_traversal, "DDA Traversal");
            g->addBool(&m_tonemap_enabled, "Tone Mapping");
        }
        g->addBool(&m_shadow_ray_enabled, "Shadow Ray Enabled");
        if(!m_release_version)
            g->addFloat(&m_shadow_ao_ray_distr, "Shadow / AO Ray Ratio", 0.f, 1.f, 0.1f, 1);
        g->addDirection(&m_light_direction, "Light Direction");
        if(!m_release_version)
            g->addFloat(&m_light_intensity, "Light Intensity", 0.f, 10.f, 0.02f, 2);

        g->addAction([this]() { getCtx()->getWsi()->setWindowSize(1920, 1080); }, "1920x1080 FullHD");
        g->addAction([this]() { getCtx()->getWsi()->setWindowSize(3840, 2160); }, "3840x2160 4K");

        if(!m_release_version) {
            g->addFloat(&m_ambient_occlusion_dist_strength.x, "Ambient Occlusion Distance", 1.f, 32.f, 1.f);
            g->addFloat(&m_ambient_occlusion_dist_strength.y, "Ambient Occlusion Strength", 0.f, 1.f, 0.1f);
            g->addSeparator();
            g->addLabel("Debug");
            g->addInt(&m_max_decoding_lod, "Max. LOD", 0, 5, 1);
            g->addBool(&m_show_model_space, "Show Model Space");
            g->addBool(&m_show_brick_cache, "Show Brick Cache");
            g->addBool(&m_show_lod, "Show LOD Levels");
            g->addBool(&m_show_step_count, "Show Ray Step Count");
            g->addAction([this]() { getCamera()->reset(); }, "Reset Camera");
            g->addAction(
                    [this]() {
                        if (m_pass)
                            m_pass->resetCacheOnNextCall();
                    },
                    "Hard Reset Brick Cache");
            g->addBool(&m_clear_cache_every_frame, "Clear Cache Every Frame");
            g->addBool(&m_clear_accum_every_frame, "Clear Accumulation Every Frame");
            g->addSeparator();
        }
        g->addDynamicText(&m_gui_resolution_text);
        g->addAction([this]() {
            if (!pfd::settings::available()) {
                Logger(WARN) << "Can not open file dialog for screenshot export. Using default file ./volcanite_output.png";
                m_download_frame_to_image_file = "./volcanite_output.png";
                return;
            }

            // Open a file dialog to choose a file
            auto selected_file = pfd::save_file("Save Screenshot", pfd::path::home(),
                                                { "Image File (.png .jpg .jpeg)", "*.png *.jpg *.jpeg", "All Files", "*" });
            if(!selected_file.result().empty())
                m_download_frame_to_image_file = selected_file.result();
        }, "Screenshot");
    }

} // namespace vvv
