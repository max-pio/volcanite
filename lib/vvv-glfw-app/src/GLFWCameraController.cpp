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

#include "vvvwindow/GLFWCameraController.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#ifdef IMGUI
#include "imgui.h"
#endif

#include <numbers>
#include <stdexcept>

namespace vvv {

double GLFWCameraController::s_mouse_scroll_wheel = 0.f;

void GLFWCameraController::glfwUpdateScrollWheel(GLFWwindow *window, double xoffset, double yoffset) {
    s_mouse_scroll_wheel += yoffset;
}

void GLFWCameraController::setWindow(GLFWwindow *window) {
    m_window = window;
    glfwSetScrollCallback(m_window, &GLFWCameraController::glfwUpdateScrollWheel);
}

void GLFWCameraController::updateCamera(bool captureMouse, bool captureKeyboard) {
    if (!m_window || !m_camera) {
        throw std::runtime_error("GLFWwindow or camera not set before trying to update camera controller");
    }

    // read scroll wheel value
    float scrollWheelDelta = static_cast<float>(s_mouse_scroll_wheel - m_mouse_scroll_wheel_previous_frame);
    m_mouse_scroll_wheel_previous_frame = s_mouse_scroll_wheel;
    if (!captureMouse)
        scrollWheelDelta = 0.f;

    // Figure out how much time has passed since the last invocation
    static double last_time = 0.0;
    double now = glfwGetTime();
    double elapsed_time = (last_time == 0.0) ? 0.0 : (now - last_time);
    float time_delta = static_cast<float>(elapsed_time);
    last_time = now;

    static const float mouse_radians_per_pixel = 1.0f * std::numbers::pi / 1000.0f;

    int left_mouse_state = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_1);
    int right_mouse_state = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_2);
    if (!captureMouse) {
        left_mouse_state = GLFW_RELEASE;
        right_mouse_state = GLFW_RELEASE;
    }

    float final_speed = m_camera->speed * 0.5f;
    if (captureKeyboard) {
        final_speed *= (glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 2.0f : 1.0f;
        final_speed *= (glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) ? 0.1f : 1.0f;
    }
    float step = time_delta * final_speed;

    double mouse_position_double[2];
    glfwGetCursorPos(m_window, &mouse_position_double[0], &mouse_position_double[1]);
    float mouse_position[2] = {(float)mouse_position_double[0], (float)mouse_position_double[1]};
    if (!m_camera->rotate_camera && (right_mouse_state == GLFW_PRESS || left_mouse_state == GLFW_PRESS)) {
        m_camera->rotate_camera = true;
        m_camera->rotation_x_0 = m_camera->rotation_x - mouse_position[1] * mouse_radians_per_pixel;
        m_camera->rotation_y_0 = m_camera->rotation_y - mouse_position[0] * mouse_radians_per_pixel;
    }
    // in orbital mode, shift and control can lock rotation axes
    if (captureKeyboard) {
        if (m_camera->orbital && m_camera->rotate_camera &&
            glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
            m_camera->rotation_x_0 = m_camera->rotation_x - mouse_position[1] * mouse_radians_per_pixel;
        }
        if (m_camera->orbital && m_camera->rotate_camera &&
            glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
            m_camera->rotation_y_0 = m_camera->rotation_y - mouse_position[0] * mouse_radians_per_pixel;
        }
    }

    if ((left_mouse_state == GLFW_RELEASE && right_mouse_state != GLFW_PRESS) ||
        (right_mouse_state == GLFW_RELEASE && left_mouse_state != GLFW_PRESS))
        m_camera->rotate_camera = false;

    if (m_camera->rotate_camera) {
        m_camera->rotation_x = m_camera->rotation_x_0 + mouse_radians_per_pixel * mouse_position[1];
        m_camera->rotation_y = m_camera->rotation_y_0 + mouse_radians_per_pixel * mouse_position[0];
        m_camera->rotation_x = (m_camera->rotation_x < -std::numbers::pi) ? -std::numbers::pi
                                                                          : m_camera->rotation_x;
        m_camera->rotation_x = (m_camera->rotation_x > std::numbers::pi) ? std::numbers::pi : m_camera->rotation_x;
    }

    // orbital movement
    if (m_camera->orbital) {
        // look at movement
        float forward = 0.0f, right = 0.0f, vertical = 0.0f, zoom = 0.0f;
        if (captureKeyboard) {
            forward += (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) ? step : 0.0f;
            forward -= (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) ? step : 0.0f;
            right += (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS) ? step : 0.0f;
            right -= (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS) ? step : 0.0f;
            vertical += (glfwGetKey(m_window, GLFW_KEY_E) == GLFW_PRESS) ? step : 0.0f;
            vertical -= (glfwGetKey(m_window, GLFW_KEY_Q) == GLFW_PRESS) ? step : 0.0f;
            zoom += (glfwGetKey(m_window, GLFW_KEY_G) == GLFW_PRESS) ? step : 0.0f;
            zoom -= (glfwGetKey(m_window, GLFW_KEY_T) == GLFW_PRESS) ? step : 0.0f;
        }

        // transform the look at offset in world space: move with WASD in the xz plane, move the plane up and down with QE
        glm::vec4 look_at_offset =
            glm::inverse(m_camera->get_world_to_view_space()) * glm::vec4(right, 0.f, forward, 0);
        look_at_offset.y = vertical;
        glm::vec3 old_position_look_at = m_camera->position_look_at_world_space;
        m_camera->position_look_at_world_space += glm::vec3(look_at_offset);

        // clamp the values s.t. the look at point never leaves the unit cube
#if 0
            if(glm::any(glm::lessThan(m_camera->position_look_at_world_space, glm::vec3(-0.5f))) || glm::any(glm::greaterThan(m_camera->position_look_at_world_space, glm::vec3(0.5f)))) {
                m_camera->position_look_at_world_space = old_position_look_at;
            }
#else
        static constexpr float CAMERA_MOVE_BORDER = 1.f; // [-0.5 to 0.5] is inside the data set, [-1 to 1] is double that.
        m_camera->position_look_at_world_space.x = glm::clamp(m_camera->position_look_at_world_space.x,
                                                              -CAMERA_MOVE_BORDER, CAMERA_MOVE_BORDER);
        m_camera->position_look_at_world_space.y = glm::clamp(m_camera->position_look_at_world_space.y,
                                                              -CAMERA_MOVE_BORDER, CAMERA_MOVE_BORDER);
        m_camera->position_look_at_world_space.z = glm::clamp(m_camera->position_look_at_world_space.z,
                                                              -CAMERA_MOVE_BORDER, CAMERA_MOVE_BORDER);
#endif

#ifdef IMGUI
        // TODO: register a GLFW keyboard callback to obtain key modifiers
        if (captureKeyboard && ImGui::IsKeyChordPressed(ImGuiKey_R | ImGuiMod_Ctrl)) {
            m_auto_rotate_camera = !m_auto_rotate_camera;
        }
#endif
        if ((captureKeyboard && !m_camera->rotate_camera && glfwGetKey(m_window, GLFW_KEY_R)) || m_auto_rotate_camera) {
            m_camera->rotation_y += 0.5 * time_delta;
        }
        constexpr float pi_eps = std::numbers::pi / 2.f - 0.001f;
        m_camera->rotation_x = glm::clamp(m_camera->rotation_x, -pi_eps, pi_eps);

        m_camera->orbital_radius += (zoom * glm::min(m_camera->orbital_radius, 1.f));
        m_camera->orbital_radius -= (scrollWheelDelta / 10.f) * final_speed * m_camera->orbital_radius;
        m_camera->orbital_radius = glm::max(0.001f, m_camera->orbital_radius);
        m_camera->position_world_space = m_camera->position_look_at_world_space + glm::vec3(
                                                                                      m_camera->orbital_radius * cos(m_camera->rotation_y) * cos(m_camera->rotation_x),
                                                                                      m_camera->orbital_radius * sin(m_camera->rotation_x),
                                                                                      m_camera->orbital_radius * sin(m_camera->rotation_y) * cos(m_camera->rotation_x));

        if (forward != 0.f || right != 0.f || vertical != 0.f || zoom != 0.f || scrollWheelDelta != 0.f ||
            m_camera->rotate_camera || glfwGetKey(m_window, GLFW_KEY_R)) {
            m_camera->onCameraUpdate();
        }
    } else {
        // Determine camera movement
        float forward = 0.0f, right = 0.0f, vertical = 0.0f;
        if (captureKeyboard) {
            forward += (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) ? step : 0.0f;
            forward -= (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) ? step : 0.0f;
            right += (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS) ? step : 0.0f;
            right -= (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS) ? step : 0.0f;
            vertical += (glfwGetKey(m_window, GLFW_KEY_E) == GLFW_PRESS) ? step : 0.0f;
            vertical -= (glfwGetKey(m_window, GLFW_KEY_Q) == GLFW_PRESS) ? step : 0.0f;
        }
        // Implement camera movement
        float cos_y = cosf(m_camera->rotation_y), sin_y = sinf(m_camera->rotation_y);
        m_camera->position_world_space[0] += sin_y * forward;
        m_camera->position_world_space[0] += cos_y * right;
        m_camera->position_world_space[2] += -cos_y * forward;
        m_camera->position_world_space[2] += sin_y * right;
        m_camera->position_world_space[1] += vertical;

        if (forward != 0.0f || right != 0.0f || vertical != 0.0f || m_camera->rotate_camera) {
            m_camera->onCameraUpdate();
        }
    }
}

} // namespace vvv
