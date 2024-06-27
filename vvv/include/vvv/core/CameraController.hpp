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

#pragma once

#include "Camera.hpp"

class GLFWwindow;

namespace vvv {

    class CameraController {

    private:
        GLFWwindow* m_window;
        Camera* m_camera;
        static double s_mouse_scroll_wheel;
        double m_mouse_scroll_wheel_previous_frame = 0.f;

        static void glfwUpdateScrollWheel(GLFWwindow* window, double xoffset, double yoffset);

    public:
        CameraController() : m_window(nullptr), m_camera(nullptr) {}
        CameraController(Camera* camera) : m_window(nullptr), m_camera(camera) {}

        void setCamera(Camera* camera) { m_camera = camera; }
        Camera* getCamera() { return m_camera; }

        void setWindow(GLFWwindow* window);

        void updateCamera();
    };

}