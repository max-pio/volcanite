#pragma once

#include "GpuContext.hpp"
#include "GuiInterface.hpp"
#include "Texture.hpp"

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
    virtual void initGui(vvv::GuiInterface * gui){};
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
    virtual void releaseGui(){};
    virtual void releaseSwapchain(){};
};

}