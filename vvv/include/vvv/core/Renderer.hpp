#pragma once

#include <utility>

#include "GpuContext.hpp"
#include "GuiInterface.hpp"
#include "Texture.hpp"
#include "vvv/util/Paths.hpp"

namespace vvv {

struct RendererOutput {
    Texture *texture;
    /** the callee has to await these semaphores before he can access the
        contents of the rendering output */
    vvv::AwaitableList renderingComplete;

    // TODO(Reiner): let resources track queue family indices
    uint32_t queueFamilyIndex = 0;
};

class Renderer {
public:
    /**
     * Schedule work for the next frame in the frame sequence
     *
     * @param awaitBeforeExecution A set of semaphores that are signaled when frame should start rendering. the rendering engine MUST await these semaphores.
     */
    virtual RendererOutput renderNextFrame(AwaitableList awaitBeforeExecution = {}, BinaryAwaitableList awaitBinaryAwaitableList = {}, vk::Semaphore *signalBinarySemaphore = nullptr) = 0;

    /**
     * Allows the renderer to use `enableInstanceLayer`, `enableDeviceExtension`, `physicalDeviceFeatures` and other configuration methods
     * on the GPU context to enable layers, extensions and features on the Vulkan context.
     */
    virtual void configureExtensionsAndLayersAndFeatures(vvv::GpuContextRwPtr ctx) {};

    /** initialize all resources here that do not depend on the swapchain size or any shaders */
    virtual void initResources(vvv::GpuContextRwPtr ctx){};
    /** initialize your GUI here */
    virtual void initGui(vvv::GuiInterface * gui){ m_gui_interface = gui; };
    /** initialize all resources here that depend on shaders */
    virtual void initShaderResources(){};
    /** initialize all resources here that depend on the swapchain size (e.g. render targets) */
    virtual void initSwapchainResources(){};

    /** Release all vulkan resources.
     *
     * It is not guaranteed that `releaseSwapchain` is called first.
     * This method MUST NOT crash when called multiple times. It MUST NOT release any vulkan resources owned by the GpuContext.
     * It is guaranteed that the object will not be reused after `releaseResources` is called at least once.
     */
    virtual void releaseResources(){};
    virtual void releaseShaderResources(){};
    virtual void releaseGui(){ m_gui_interface = nullptr; };
    virtual void releaseSwapchain(){};

    virtual std::shared_ptr<Camera> getCamera() { return m_camera; }
    virtual void setCamera(std::shared_ptr<Camera> camera) { m_camera = std::move(camera); }

    /**
     * Writes all rendering and camera parameters in human readable form to the given stream. The Renderer superclass
     * exports all GUI interface parameters as well as camera parameters.
     * @return true on success, false otherwise
     */
    virtual bool writeParameters(std::ostream& out, const std::string& version_string="") const {
        out << "Version " << version_string << std::endl;
        if(!m_camera) {
            Logger(WARN) << "Cannot write renderer parameters as camera is not set!";
            return false;
        }
        out << std::endl << "[Camera]" << std::endl;
        m_camera->writeTo(out, true);
        if (!out) {
            Logger(WARN) << "Error writing camera parameters to file.";
            return false;
        }
        out << std::endl;
        if(!m_gui_interface) {
            // If you receive this warning: Did you forget to call Renderer::initGui(gui) from the base class initGui?
            Logger(WARN) << "Cannot write renderer parameters as gui interface is not set!";
            return false;
        }
        if(!m_gui_interface->writeParameters(out))
            return false;
        return true;
    }

    /**
     * Writes all rendering and camera parameters in human readable form to the given file.
     * @return true on success, false otherwise
     */
    virtual bool writeParameterFile(const std::string& path, const std::string& version_string="") const {
        std::ofstream out(path);
        if(out.is_open()) {
            if (!writeParameters(out, version_string)) {
                Logger(WARN) << "Could not export parameters to " << path;
                out.close();
                return false;
            }
            else {
                out.close();
                return true;
            }
        }
        return false;
    }


    /**
     * Reads all rendering and camera parameters from the given stream. The Renderer superclass reads all GUI interface
     * parameters as well as camera parameters if exported with writeParameters(..).
     * @param expected_version_string if not empty, reading configurations with a different version will fail
     * @return true on success, false otherwise
     */
    virtual bool readParameters(std::istream& in, const std::string& expected_version_string="") {
        std::string tmp;
        in >> tmp; // "Version"
        in >> tmp; // VOLCANITE_VERSION
        if(!expected_version_string.empty() && tmp != expected_version_string) {
            Logger(WARN) << "Unexpected config version " << tmp << " instead of " << expected_version_string;
            return false;
        }
        if (!m_camera) {
            Logger(WARN) << "Cannot read renderer parameters as camera is not set!";
            return false;
        }
        in >> tmp; // "[Camera]"
        if(tmp != "[Camera]") {
            Logger(WARN) << "Expecting [Camera] parameter block but got " << tmp;
            return false;
        }
        m_camera->readFrom(in, true);
        if (!in.good()) {
            Logger(WARN) << "Error reading camera parameters from file.";
            return false;
        }
        if(!m_gui_interface) {
            // If you receive this warning: Did you forget to call Renderer::initGui(gui) from the base class initGui?
            Logger(WARN) << "Cannot read renderer parameters as gui interface is not set!";
            return false;
        }
        if(!m_gui_interface->readParameters(in))
            return false;
        if (!in.good()) {
            Logger(WARN) << "Error reading render parameters from file.";
            return false;
        }
        in >> tmp;
        if(!(in.rdstate() & std::istream::eofbit))
            Logger(WARN) << "Possible parameter import error. Did not reach end of file.";
        return true;
    }

    /**
     * Reads all rendering and camera parameters from the given path.
     * If parameters could not be imported from path, the previous parameter state is restored.
     * @return true if parameters were successfully read from path, false otherwise
     */
    virtual bool readParameterFile(const std::string& path, const std::string& expected_version_string= "") {
        // Save old parameters to reload in case of failure
        std::filesystem::path path_backup_config = vvv::Paths::getTempFileWithName("tmp_render_config_params.vcfg");
        if(std::filesystem::exists(path_backup_config))
            std::filesystem::remove(path_backup_config);
        if(!writeParameterFile(path_backup_config, expected_version_string))
            Logger(WARN) << "Could not export backup rendering parameters to " << path_backup_config;

        // Try to load selected config path
        // Load backup config in case of failure
        bool success = true;
        std::ifstream in(path);
        if(in.is_open()) {
            if (!readParameters(in, expected_version_string)) {
                Logger(WARN) << "Could not import rendering parameters from " << path;

                std::ifstream backup_in(path_backup_config);
                if (!backup_in.is_open() || !readParameters(backup_in, expected_version_string)) {
                    Logger(ERROR) << "Could not import backup rendering parameters from " << path_backup_config;
                } else {
                    Logger(DEBUG) << "Imported backup rendering parameters from " << path_backup_config;
                }
                backup_in.close();
                success = false;
            }
            else {
                Logger(DEBUG) << "Imported rendering parameters from " << path;
            }
            in.close();
        }
        else {
            Logger(WARN) << "Could not import parameters from " << path;
            success = false;
        }
        return success;
    }

protected:
    std::shared_ptr<Camera> m_camera = nullptr;
    vvv::GuiInterface* m_gui_interface = nullptr;
};

}