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

#include "volcanite/renderer/HelloVolumeRenderer.hpp"

#include <vvv/core/Buffer.hpp>
#include <random>
#include <memory>

#include "glm/gtc/matrix_transform.hpp"

using namespace vvv;

namespace volcanite {


RendererOutput HelloVolumeRenderer::renderNextFrame(AwaitableList awaitBeforeExecution, BinaryAwaitableList awaitBinaryAwaitableList, vk::Semaphore *signalBinarySemaphore) {
    assert(m_urender_info && "HelloVolumeRenderer data missing!");

    // upload point cloud buffers if the point cloud changed
    if(m_data_changed) {
        // wait until all previous frames are processed
        getCtx()->getDevice().waitIdle();

        // TODO upload new data (= volume texture for example)

        // wait until everything is uploaded
        getCtx()->getDevice().waitIdle();
        m_data_changed = false;
    }

    // upload uniforms
    if (m_urender_info) {
        updateUniformDescriptorset();
        m_urender_info->upload(m_pass->getActiveIndex());
    }

    const auto renderingFinished = m_pass->execute(awaitBeforeExecution, awaitBinaryAwaitableList);

    return vvv::RendererOutput{
        .texture = m_outColor->getActive().get(),
        .renderingComplete = {renderingFinished},
    };
}

void HelloVolumeRenderer::initResources(GpuContext *ctx) {
    setCtx(ctx);
    // TODO allocate GPU buffers for our data ..
}

void HelloVolumeRenderer::releaseResources() {
    // TODO delete buffers
}

void HelloVolumeRenderer::initShaderResources() {
    // compute pass for ray marching points
    ShaderCompileErrorCallback compileErrorCallback = [](const ShaderCompileError& err) {
        Logger(ERROR) << err.errorText;
        return ShaderCompileErrorCallbackAction::USE_PREVIOUS_CODE;
    };
    m_pass = std::make_unique<SinglePassCompute>(SinglePassComputeSettings{.ctx = getCtx(), .label = "HelloVolumeRenderer", .multiBuffering = getCtx()->getWsi()->stateInFlight()},
                                                 SimpleGlslShaderRequest{.filename = "volcanite/renderer/hello_volume_renderer.comp"}, compileErrorCallback);
    m_pass->allocateResources();
    m_urender_info = m_pass->getUniformSet("render_info");

    m_camHash = MultiBufferedResource<size_t>(getCtx()->getWsi()->stateInFlight(), 0);

    // TODO reflect textures, assign storage buffers etc
    if(m_outColor)
        m_pass->setStorageImage("outColor", *m_outColor);
}

void HelloVolumeRenderer::releaseShaderResources() {
    m_urender_info = nullptr;
    if(m_pass)
        m_pass->freeResources();
    m_pass = nullptr;
}



void HelloVolumeRenderer::initSwapchainResources() {
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
    if(m_pass)
        m_pass->setStorageImage("outColor", *m_outColor);
}

void HelloVolumeRenderer::releaseSwapchain() {
    if (m_outColor)
        m_outColor.reset();
}

void HelloVolumeRenderer::updateUniformDescriptorset() {
    const auto wsi = getCtx()->getWsi();
    const auto camera = wsi->getCamera();
    const auto screenExtent = wsi->getScreenExtent();

    glm::vec4 physical_volume_size(1.f, 1.f, 1.f, 1.f);

    // render info uniform
    {
        m_urender_info->setUniform<glm::vec4>("g_background_color_a", m_background_color_a);
        m_urender_info->setUniform<glm::vec4>("g_background_color_b", m_background_color_b);
        m_urender_info->setUniform<float>("g_transferFunction_limits_min", 0);
        m_urender_info->setUniform<float>("g_transferFunction_limits_max", 1000);
        m_urender_info->setUniform<float>("g_stepSize", 0.2f);
        m_urender_info->setUniform<int32_t>("g_maxSteps", 512);
        m_urender_info->setUniform<float>("g_opacityThreshold", .95);
        m_urender_info->setUniform<glm::vec3>("g_camera_position_world_space", camera->position_world_space);

        // debug
        m_urender_info->setUniform<uint32_t>("g_debug_model_space", m_show_model_space ? 1 : 0);

        // transformation matrices
        // beware of our terminology / coordinate spaces here:
        // in world space, everything should be a centered unit cube! in model space, points can have arbitrary coordinates. The normalization transform scales this down to world space [-0.5, 0.5]^3
        glm::mat4 world_to_model_space = glm::translate(glm::mat4(1.f), glm::vec3(physical_volume_size / 2.f));
        m_urender_info->setUniform<glm::mat4x4>("g_model_to_world_space", glm::inverse(world_to_model_space));
        m_urender_info->setUniform<glm::mat4x4>("g_world_to_model_space", world_to_model_space);
        m_urender_info->setUniform<glm::mat3x3>("g_world_to_model_space_dir", glm::mat3(1.f));
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
        auto newCamHash = hashMemory(&world_to_projection_space[0].x, sizeof(glm::mat4));
        if (newCamHash != m_camHash.getActive()) {
        }
        else {
        }
    }
}

} // namespace volcanite
