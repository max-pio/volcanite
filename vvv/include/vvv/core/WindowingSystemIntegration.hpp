#pragma once

#include <vulkan/vulkan.hpp>

#include "Camera.hpp"
#include "MultiBuffering.hpp"

namespace vvv {

class WindowingSystemIntegration {
public:

    virtual vk::Extent2D getScreenExtent() const = 0;
    virtual float getScreenContentScale() const = 0;

    virtual void setWindowSize(int width, int height) const = 0;
    virtual void setWindowResizable(bool resizable) const = 0;
    virtual bool isWindowResizable() const = 0;

    // TODO: does not really belong here... camera should be part of the [Renderer](vvv/include/Renderer.cpp), not the WSI
    virtual Camera *getCamera() const = 0;

    /** Number of swapchain images. This is not necessarily the maximal number of images concurrently in flight!
     * This value MUST be dynamically constant and MAY only chance in conjunction with a call to reinitializeSwapchain.
     */
    uint32_t swapChainImageCount() const {
        return stateSwapchain()->getIndexCount();
    }

    uint32_t currentSwapChainImageIndex() const {
        return stateSwapchain()->getActiveIndex();
    }

    uint32_t maximalInFlightFrameCount() const {
        return stateInFlight()->getIndexCount();
    }

    uint32_t currentInFlightFrameIndex() const {
        return stateInFlight()->getActiveIndex();
    }

    std::shared_ptr<MultiBuffering> stateSwapchain() const {
        assert(m_swapchain);
        return m_swapchain;
    }

    std::shared_ptr<MultiBuffering> stateInFlight() const {
        assert(m_inflight);
        return m_inflight;
    }

protected:

    //! recreate MultiBuffering objects if the new sizes are different from the currently used sizes
    void setMultiBuffering(uint32_t countSwapchainImages, uint32_t countInFlight) {
        if (!m_swapchain || m_swapchain->getIndexCount() != countSwapchainImages)
            m_swapchain = std::make_shared<MultiBuffering>(countSwapchainImages);
        if (!m_inflight || m_inflight->getIndexCount() != countInFlight)
            m_inflight = std::make_shared<MultiBuffering>(countInFlight);
    }

private:

    std::shared_ptr<MultiBuffering> m_swapchain = nullptr;
    std::shared_ptr<MultiBuffering> m_inflight = nullptr;
};

} // namespace vvv