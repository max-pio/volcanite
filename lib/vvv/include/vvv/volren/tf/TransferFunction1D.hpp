//  Copyright (C) 2024, Patrick Jaberg, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
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

#include <vvv/core/Shader.hpp>

#include "color.hpp"

#include "TransferFunction.hpp"

#include <memory>

namespace vvv {

enum class ChannelOpacityState { AlphaPremultiplied,
                                 PostMultiplied };
constexpr ChannelOpacityState DefaultChannelOpacityState = ChannelOpacityState::PostMultiplied;

class TransferFunction1D : public TransferFunction {

  public:
    // Note: pattern to get named constructors that are callable with new and without new.
    struct fullyTransparent;
    struct solidColor;
    struct linearRamp;

    /// Create new Transfer Function from discrete values.
    /// @param values array of countValues discrete values to use as transfer function
    /// @param channelOpacityState If PostMultiplied, the values will be pre-multiplied before uploading to the GPU.
    TransferFunction1D(vvv::GpuContextPtr ctx, const uint16_t *const values, size_t countValues, ChannelOpacityState channelOpacityState = DefaultChannelOpacityState)
        : TransferFunction(ctx), m_channelOpacityState(channelOpacityState) {
        m_data.assign(reinterpret_cast<const char *>(values), reinterpret_cast<const char *>(values + countValues));

        if (m_channelOpacityState == ChannelOpacityState::PostMultiplied) {
            premultiplyAlpha(reinterpret_cast<uint16_t *>(m_data.data()), m_data.size() / sizeof(uint16_t));
        }

        m_texture = std::make_unique<Texture>(ctx, vk::Format::eR16G16B16A16Unorm, countValues / FormatComponentCount(static_cast<VkFormat>(vk::Format::eR16G16B16A16Unorm)),
                                              vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, TextureExclusiveQueueUsage);
    }

    TransferFunction1D(vvv::GpuContextPtr ctx, std::vector<uint16_t> values, ChannelOpacityState channelOpacityState = DefaultChannelOpacityState)
        : TransferFunction1D(ctx, values.data(), values.size(), channelOpacityState) {}

    TransferFunction1D(vvv::GpuContextPtr ctx, std::initializer_list<uint16_t> values, ChannelOpacityState channelOpacityState = DefaultChannelOpacityState)
        : TransferFunction1D(ctx, std::data(values), values.size(), channelOpacityState) {}

    ~TransferFunction1D() override = default;

    [[nodiscard]] std::pair<vvv::AwaitableHandle, std::shared_ptr<vvv::Buffer>> upload() override {
        auto ret = m_texture->upload(m_data.data());
        m_texture->setName("tf1d.1d_texture");
        return ret;
    }

    [[nodiscard]] std::pair<vvv::AwaitableHandle, std::shared_ptr<vvv::Buffer>> upload(const std::vector<uint16_t> &values) {
        m_data.assign(reinterpret_cast<const char *>(values.data()), reinterpret_cast<const char *>(values.data() + values.size()));

        if (m_channelOpacityState == ChannelOpacityState::PostMultiplied) {
            premultiplyAlpha(reinterpret_cast<uint16_t *>(m_data.data()), m_data.size() / sizeof(uint16_t));
        }

        return upload();
    }

    std::string preprocessorLabel() override { return "TRANSFER_FUNCTION_MODE_1D"; }

  private:
    ChannelOpacityState m_channelOpacityState;
    std::vector<char> m_data;
};

struct TransferFunction1D::solidColor : public TransferFunction1D {
    solidColor(vvv::GpuContextPtr ctx, std::array<uint16_t, 4> color) : TransferFunction1D(ctx, color.data(), color.size()) {}
};

struct TransferFunction1D::fullyTransparent : public TransferFunction1D {
    explicit fullyTransparent(vvv::GpuContextPtr ctx) : TransferFunction1D(ctx, {0, 0, 0, 0}) {}
};

struct TransferFunction1D::linearRamp : public TransferFunction1D {
    linearRamp(vvv::GpuContextPtr ctx, std::array<uint16_t, 4> minColor, std::array<uint16_t, 4> maxColor)
        : TransferFunction1D(ctx, {
                                      minColor[0],
                                      minColor[1],
                                      minColor[2],
                                      minColor[3],
                                      maxColor[0],
                                      maxColor[1],
                                      maxColor[2],
                                      maxColor[3],
                                  }) {}

    linearRamp(vvv::GpuContextPtr ctx, glm::vec4 minColor, glm::vec4 maxColor)
        : TransferFunction1D(ctx, {
                                      static_cast<uint16_t>(minColor[0] * std::numeric_limits<uint16_t>::max()),
                                      static_cast<uint16_t>(minColor[1] * std::numeric_limits<uint16_t>::max()),
                                      static_cast<uint16_t>(minColor[2] * std::numeric_limits<uint16_t>::max()),
                                      static_cast<uint16_t>(minColor[3] * std::numeric_limits<uint16_t>::max()),
                                      static_cast<uint16_t>(maxColor[0] * std::numeric_limits<uint16_t>::max()),
                                      static_cast<uint16_t>(maxColor[1] * std::numeric_limits<uint16_t>::max()),
                                      static_cast<uint16_t>(maxColor[2] * std::numeric_limits<uint16_t>::max()),
                                      static_cast<uint16_t>(maxColor[3] * std::numeric_limits<uint16_t>::max()),
                                  }) {}
};

} // namespace vvv