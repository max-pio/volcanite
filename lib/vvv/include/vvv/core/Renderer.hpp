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

#pragma once

#include <utility>

#include "GpuContext.hpp"
#include "GuiInterface.hpp"
#include "Texture.hpp"
#include "vvv/util/Paths.hpp"

namespace vvv {

struct RendererOutput {
    Texture *texture;
    /// the callee has to await these semaphores before he can access the contents of the rendering output.
    vvv::AwaitableList renderingComplete;

    // TODO: let resources track queue family indices
    uint32_t queueFamilyIndex = 0;
};

class Renderer {
  public:
    virtual ~Renderer() = default;

    /// Schedule work for the next frame in the frame sequence
    /// @param awaitBeforeExecution A set of semaphores that are signaled when frame should start rendering. the rendering engine MUST await these semaphores.
    virtual RendererOutput renderNextFrame(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr) = 0;

    /// Allows the renderer to use `enableInstanceLayer`, `enableDeviceExtension`, `physicalDeviceFeatures` and other configuration methods
    /// on the GPU context to enable layers, extensions and features on the Vulkan context.
    virtual void configureExtensionsAndLayersAndFeatures(vvv::GpuContextRwPtr ctx) {};

    /// initialize all resources here that do not depend on the swapchain size or any shaders
    virtual void initResources(vvv::GpuContextRwPtr ctx) {};
    /// initialize your GUI here
    virtual void initGui(vvv::GuiInterface *gui) { m_gui_interface = gui; };
    /// initialize all resources here that depend on shaders
    virtual void initShaderResources() {};
    /// initialize all resources here that depend on the swapchain size (e.g. render targets)
    virtual void initSwapchainResources() {};

    /// @brief Release all vulkan resources.
    ///
    /// It is not guaranteed that `releaseSwapchain` is called first.
    /// This method MUST NOT crash when called multiple times. It MUST NOT release any vulkan resources owned by the GpuContext.
    /// It is guaranteed that the object will not be reused after `releaseResources` is called at least once.
    virtual void releaseResources() {};
    virtual void releaseShaderResources() {};
    virtual void releaseGui() { m_gui_interface = nullptr; };
    virtual void releaseSwapchain() {};

    virtual std::shared_ptr<Camera> getCamera() { return m_camera; }
    virtual void setCamera(std::shared_ptr<Camera> camera) { m_camera = std::move(camera); }

    /// Updates the current mouse position in screen space coordinates [0,1]^2 for the shaders.
    virtual void setCursorPos(const glm::vec2 mouse_position) {};

    /// Writes all rendering and camera parameters in human readable form to the given stream. The Renderer superclass
    /// exports all GUI interface parameters as well as camera parameters.
    /// @return true on success, false otherwise
    virtual bool writeParameters(std::ostream &out, const std::string &version_string = "") const {
        assert(version_string.find(' ') == std::string::npos && "file version string must be a single token");
        out << "Version " << (version_string.empty() ? "---" : version_string) << std::endl;
        if (!m_camera) {
            Logger(Warn) << "Cannot write renderer parameters as camera is not set!";
            return false;
        }
        out << std::endl
            << "[Camera]" << std::endl;
        m_camera->writeTo(out, true);
        if (!out) {
            Logger(Warn) << "Error writing camera parameters to file.";
            return false;
        }
        out << std::endl;
        if (!m_gui_interface) {
            // If you receive this warning: Did you forget to call Renderer::initGui(gui) from the base class initGui?
            Logger(Warn) << "Cannot write renderer parameters as gui interface is not set!";
            return false;
        }
        if (!m_gui_interface->writeParameters(out))
            return false;
        return true;
    }

    /// Writes all rendering and camera parameters in human readable form to the given file.
    /// @return true on success, false otherwise
    [[nodiscard]] virtual bool writeParameterFile(const std::string &path, const std::string &version_string = "") const {
        std::ofstream out(path);
        if (out.is_open()) {
            if (!writeParameters(out, version_string)) {
                Logger(Warn) << "Could not export parameters to " << path;
                out.close();
                return false;
            } else {
                out.close();
                return true;
            }
        }
        return false;
    }

    /// Reads all rendering and camera parameters from the given stream. The Renderer superclass reads all GUI interface
    /// parameters as well as camera parameters if exported with writeParameters(..).
    /// @param expected_version_string if not empty, reading configurations with a different version will fail
    /// @param backup_parameters if the current parameters will be backed up to a tmp file and re-imported on failure
    /// @return true on success, false otherwise
    virtual bool readParameters(std::istream &in, const std::string &expected_version_string = "", bool backup_parameters = true) {
        // read next one section after the other
        if (!m_gui_interface) {
            // If you receive this warning: Did you forget to call Renderer::initGui(gui) from the base class initGui?
            Logger(Warn) << "Cannot read renderer parameters as gui interface is not set!";
            return false;
        }

        // Save old parameters to reload in case of failure
        std::filesystem::path path_backup_config = vvv::Paths::getTempFileWithName("tmp_render_config_params.vcfg");
        if (backup_parameters) {
            if (std::filesystem::exists(path_backup_config))
                std::filesystem::remove(path_backup_config);
            if (!writeParameterFile(path_backup_config.string(), expected_version_string))
                Logger(Warn) << "Could not export backup rendering parameters to " << path_backup_config;
        }

        if (!m_gui_interface->readParameters(in, m_camera.get())) {
            if (backup_parameters) {
                // error parsing parameters: re-import old parameters
                if (!readParameterFile(path_backup_config.generic_string(), expected_version_string, false)) {
                    Logger(Debug) << "Could not import backup rendering parameters from " << path_backup_config;
                } else {
                    Logger(Debug) << "Imported backup rendering parameters after parsing error.";
                }
            }
            return false;
        }

        if (!(in.rdstate() & std::istream::eofbit))
            Logger(Warn) << "Possible parameter import error. Did not reach end of file.";

        return true;
    }

    /// Reads all rendering and camera parameters from the given path.
    /// If parameters could not be imported from path, the previous parameter state is restored.
    /// @param backup_parameters if the current parameters will be backed up to a tmp file and re-imported on failure
    /// @return true if parameters were successfully read from path, false otherwise
    virtual bool readParameterFile(const std::string &path, const std::string &expected_version_string = "", bool backup_parameters = true) {

        // Try to load selected config path
        // Load backup config in case of failure
        bool success = true;
        if (std::ifstream in(path); in.is_open()) {
            // read version strings from file
            std::string tmp;
            in >> tmp; // "Version"
            in >> tmp; // VOLCANITE_VERSION
            if (!expected_version_string.empty() && tmp != expected_version_string) {
                Logger(Warn) << "Unexpected config version " << tmp << " instead of " << expected_version_string;
                success = false;
            }

            if (!readParameters(in, expected_version_string, backup_parameters)) {
                Logger(Warn) << "Could not import rendering parameters from " << path;
                success = false;
            } else {
                Logger(Debug) << "Imported rendering parameters from " << path;
            }
            in.close();
        } else {
            Logger(Warn) << "Could not open parameter file " << path;
            success = false;
        }
        return success;
    }

    virtual void startFrameTimeTracking() {
        throw std::logic_error("Renderer does not implement frame time tracking.");
    }
    /// Stops the tracking. Should be immediately called after last renderNextFrame. If awaitLastFrameFinished is set,
    /// either to {} or an awaitable list, the method waits for the awaitables to finish and adds a final timing
    /// measurement for the last frame. Query the results with getLastEvaluationResults()
    virtual void stopFrameTimeTracking(std::optional<AwaitableList> awaitLastFrameFinished = {}) {
        throw std::logic_error("Renderer does not implement frame time tracking.");
    }

    /// Called after renderNextFrame to download and export the currently rendered image on the next (!) call of
    /// renderNextFrame. If renderNextFrame is not called after exportCurrentFrameToImage, the frame is not exported.
    /// @param image_path a path to a non-existing png, jpg, or jpeg file
    virtual void exportCurrentFrameToImage(std::string image_path) {
        throw std::logic_error("Renderer does not implement image export");
    }

  protected:
    std::shared_ptr<Camera> m_camera = nullptr;
    vvv::GuiInterface *m_gui_interface = nullptr;
};

} // namespace vvv