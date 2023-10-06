#include "vvv/core/Camera.hpp"
#include <glm/gtx/transform.hpp>
#include <utility>

glm::mat4 vvv::Camera::get_world_to_view_space() const {
    if(orbital) {
        glm::vec3 up = glm::normalize(glm::vec3(position_world_space.z, 0.f, -position_world_space.x)); // project on xz plane, orthogonal
        return glm::lookAt(position_world_space, glm::vec3(0.f), glm::cross(glm::normalize(position_world_space), up));
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
        proj = glm::perspective(vertical_fov, aspect_ratio, this->near, this->far);;
    }
    else if (camera_mode == Mode::Orthogonal) {
        float half_width  = 0.5f * orthogonal_scale;
        float half_height = 0.5f * orthogonal_scale / aspect_ratio;
        proj = glm::ortho(-half_width, half_width, -half_height, half_height);
    }
    else throw std::runtime_error("Unknown camera_mode encountered in Camera::get_view_to_projection_space()");
    
    // hacky fix for Vulkan's inverted y-axis
    proj[1][1] *= -1;

    return proj;
}

glm::mat4 vvv::Camera::get_world_to_projection_space(float aspect_ratio) const { return get_view_to_projection_space(aspect_ratio) * get_world_to_view_space(); }

void vvv::Camera::registerCameraUpdateCallback(std::function<void()> cameraUpdateFunction) {
    m_cameraUpdateFunction = std::move(cameraUpdateFunction);
}

void vvv::Camera::onCameraUpdate() {
    if (m_cameraUpdateFunction) {
        m_cameraUpdateFunction();
    }
}
