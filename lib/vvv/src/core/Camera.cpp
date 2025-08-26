//  Copyright (C) 2024, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
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
//
// This class contains code from the Camera implementation by Christoph Peters "MyToyRenderer" which was released under
// the GPLv3 License. Our adaptions include an added switch between orbital and translational camera modes, file
// import / export, obtaining default parameters, and registering callback functions.
// The original code can be found at https://github.com/MomentsInGraphics/vulkan_renderer/blob/main/src/camera.h

#include "vvv/core/Camera.hpp"
#include <glm/gtx/transform.hpp>
#include <utility>

glm::mat4 vvv::Camera::get_world_to_view_space() const {
    if (orbital) {
        glm::vec3 up = glm::normalize(glm::vec3(position_world_space.z - position_look_at_world_space.z,
                                                0.f,
                                                position_look_at_world_space.x - position_world_space.x)); // project on xz plane, orthogonal
        return glm::lookAt(position_world_space,
                           position_look_at_world_space,
                           glm::cross(glm::normalize(position_world_space - position_look_at_world_space), up));
    } else {
        glm::mat4 translate = glm::translate(-position_world_space);
        glm::mat4 rotY = glm::rotate(rotation_y, glm::vec3(0, 1, 0));
        glm::mat4 rotX = glm::rotate(rotation_x, glm::vec3(1, 0, 0));
        return rotX * rotY * translate;
    }
}

glm::mat4 vvv::Camera::get_view_to_projection_space(float aspect_ratio) const {
    glm::mat4 proj;
    if (camera_mode == Mode::Perspective) {
        proj = glm::perspective(vertical_fov, aspect_ratio, this->near, this->far);
        ;
    } else if (camera_mode == Mode::Orthogonal) {
        float half_width = 0.5f * orthogonal_scale;
        float half_height = 0.5f * orthogonal_scale / aspect_ratio;
        proj = glm::ortho(-half_width, half_width, -half_height, half_height);
    } else
        throw std::runtime_error("Unknown camera_mode encountered in Camera::get_view_to_projection_space()");

    // hacky fix for Vulkan's inverted y-axis
    proj[1][1] *= -1;

    return proj;
}

glm::mat4 vvv::Camera::get_world_to_projection_space(float aspect_ratio) const {
    return get_view_to_projection_space(aspect_ratio) * get_world_to_view_space();
}

void vvv::Camera::registerCameraUpdateCallback(std::function<void()> cameraUpdateFunction) {
    m_cameraUpdateFunction = std::move(cameraUpdateFunction);
}

void vvv::Camera::onCameraUpdate() const {
    if (m_cameraUpdateFunction) {
        m_cameraUpdateFunction();
    }
}
