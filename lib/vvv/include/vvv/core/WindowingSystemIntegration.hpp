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

#include <vulkan/vulkan.hpp>

#include "Camera.hpp"
#include "MultiBuffering.hpp"

namespace vvv {

class WindowingSystemIntegration {
  public:
    virtual ~WindowingSystemIntegration() = default;
    [[nodiscard]] virtual vk::Extent2D getScreenExtent() const = 0;
    virtual float getScreenContentScale() const = 0;

    virtual void setWindowSize(int width, int height) const = 0;
    virtual void setWindowResizable(bool resizable) const = 0;
    virtual bool isWindowResizable() const = 0;

    // TODO: does not belong here. Camera should be part of the [Renderer](vvv/include/Renderer.cpp), not the WSI
    virtual Camera *getCamera() const = 0;

    /// Number of swapchain images. This is not necessarily the maximal number of images concurrently in flight!
    /// This value MUST be dynamically constant and MAY only chance in conjunction with a call to reinitializeSwapchain.
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
    /// recreate MultiBuffering objects if the new sizes are different from the currently used sizes
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