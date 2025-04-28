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

#include <memory>
#include <vector>

#include "TransferFunction1D.hpp"
#include "vvv/util/color_space_converter.hpp"

namespace vvv {

/// A vectorized representation of a transfer function. Should be rasterized to a `DiscreteTransferFunction` before usage.
class VectorTransferFunction {
  public:
    enum ColorSpace { RGB = 0,
                      CIELAB };
    static const std::vector<float> linearOpacityRamp;
    static const std::vector<float> fullyOpaque;

    std::vector<float> m_controlPointsRgb;
    std::vector<float> m_controlPointsOpacity;
    ColorSpace m_interpolationColorSpace = ColorSpace::RGB; ///< color space in which values are interpolated
                                                            ///< note that the control points

    /// @brief Create a linearly interpolated transfer function from control points.
    ///
    /// Datagram for the entries in `controlPointsOpacity`:
    /// ```
    /// ┌────────────────────────────────┬─────────────────────────────────┐
    /// │ <float> control point position │ <float> opacity value in [0,1]  │
    /// └────────────────────────────────┴─────────────────────────────────┘
    /// ```
    ///
    /// Datagram for the entries in `controlPointsRgb`:
    /// ```
    /// ┌────────────────────────────────┬───────────┬─────────────┬─────────────┐
    /// │ <float> control point position │ <float> r │  <float> g  │ <float> b   │
    /// └────────────────────────────────┴───────────┴─────────────┴─────────────┘
    /// ```
    ///
    /// The minimal and maximal position of control points may be arbitrary as long as they are monotonically increasing.
    /// Each sequence will be independently remapped to the unit interval automatically.
    ///
    /// Use to control points with an identical position to create a step within the transfer function.
    ///
    /// @param controlPointsRgb
    /// @param controlPointsOpacity
    explicit VectorTransferFunction(std::vector<float> controlPointsRgb, std::vector<float> controlPointsOpacity = linearOpacityRamp)
        : m_controlPointsRgb(controlPointsRgb), m_controlPointsOpacity(controlPointsOpacity) {
        assert(!controlPointsRgb.empty() && "expecting at least 2 control points");
        assert(!controlPointsOpacity.empty() && "expecting at least 2 control points");
        assert(controlPointsRgb.size() % 4 == 0 && "expecting a rgb vector with alternating control point position and rgb color value");
        assert(controlPointsOpacity.size() % 2 == 0 && "expecting a opacity vector with alternating control point position and control point value");
        assert(areControlPointsAreMonotonicallyIncreasing(controlPointsRgb, 4) && "control point locations of rgb values need to be monotonically increasing");
        assert(areControlPointsAreMonotonicallyIncreasing(controlPointsOpacity, 2) && "control point locations of opacity values need to be monotonically increasing");
    };

    /// Discretize the spline into equidistant samples
    /// @param width width of the transfer function defining the quality of the discretization
    std::shared_ptr<TransferFunction1D> rasterize(vvv::GpuContextPtr ctx, size_t width) const {
        auto samples = rasterize<uint16_t>(width);

        return std::make_shared<TransferFunction1D>(ctx, samples, ChannelOpacityState::PostMultiplied);
    }

    /// Discretize the spline into equidistant samples
    ///
    /// @param width number of equidistant samples
    /// @return straight/post-multiplied rgba values
    template <typename T, typename W>
    std::vector<T> rasterize(W width) const {
        static_assert(std::is_unsigned_v<W>, "width of the rasterized TF must be unsigned integer!");
        assert(width > 0);

        std::vector<T> samples(width * 4);

        for (int i = 0; i < (width * 4); i += 4) {
            const auto samplePosition = std::clamp(static_cast<float>(i) / (4 * (width - 1)), 0.0f, 1.0f);
            const auto color = sampleColor(samplePosition);
            samples[i + 0] = std::round(std::numeric_limits<T>::min() + color.r * (std::numeric_limits<T>::max() - std::numeric_limits<T>::min()));
            samples[i + 1] = std::round(std::numeric_limits<T>::min() + color.g * (std::numeric_limits<T>::max() - std::numeric_limits<T>::min()));
            samples[i + 2] = std::round(std::numeric_limits<T>::min() + color.b * (std::numeric_limits<T>::max() - std::numeric_limits<T>::min()));
            samples[i + 3] = std::round(std::numeric_limits<T>::min() + sampleOpacity(samplePosition) * (std::numeric_limits<T>::max() - std::numeric_limits<T>::min()));
        }

        return samples;
    }

    double sampleOpacity(float samplePosition) const {
        size_t lower = 0;
        float positionMin = m_controlPointsOpacity[0];
        float positionMax = m_controlPointsOpacity[m_controlPointsOpacity.size() - 1 - 1];

        // transform unit range samplePosition to the range of the transfer function
        samplePosition = samplePosition * (positionMax - positionMin) + positionMin;

        for (size_t upper = 0; upper < m_controlPointsOpacity.size(); upper += 2) {
            float upperPosition = m_controlPointsOpacity[upper];
            if (upperPosition >= samplePosition) {
                float lowerPosition = m_controlPointsOpacity[lower];
                float lowerValue = m_controlPointsOpacity[lower + 1];
                float upperValue = m_controlPointsOpacity[upper + 1];

                // Could use spline interpolation. Paraview/vtk for example uses hermite splines.
                float a = (lowerPosition == upperPosition) ? 0.5 : (samplePosition - lowerPosition) / (upperPosition - lowerPosition);
                return lowerValue * (1.0 - a) + upperValue * a;
            }
            lower = upper;
        }

        throw std::runtime_error("invalid sample position");
    }

    glm::vec3 sampleColor(double samplePosition) const {
        size_t lower = 0;
        const float positionMin = m_controlPointsRgb[0];
        const float positionMax = m_controlPointsRgb[m_controlPointsRgb.size() - 1 - 3];

        // transform unit range samplePosition to the range of the transfer function
        samplePosition = samplePosition * (positionMax - positionMin) + positionMin;
        switch (m_interpolationColorSpace) {
        case RGB:
            for (size_t upper = 0; upper < m_controlPointsRgb.size(); upper += 4) {
                float upperPosition = m_controlPointsRgb[upper];
                if (upperPosition >= samplePosition) {
                    float lowerPosition = m_controlPointsRgb[lower];
                    glm::vec3 lowerValue(m_controlPointsRgb[lower + 1], m_controlPointsRgb[lower + 2],
                                         m_controlPointsRgb[lower + 3]);
                    glm::vec3 upperValue(m_controlPointsRgb[upper + 1], m_controlPointsRgb[upper + 2],
                                         m_controlPointsRgb[upper + 3]);

                    float a = (lowerPosition == upperPosition) ? 0.5 : (samplePosition - lowerPosition) / (upperPosition - lowerPosition);
                    return glm::mix(lowerValue, upperValue, a);
                }
                lower = upper;
            }
            throw std::runtime_error("invalid sample position");
        case CIELAB: {
            glm::vec3 RGBLow(m_controlPointsRgb[lower + 1], m_controlPointsRgb[lower + 2],
                             m_controlPointsRgb[lower + 3]);
            glm::vec3 RGBHigh(m_controlPointsRgb[m_controlPointsRgb.size() - 3],
                              m_controlPointsRgb[m_controlPointsRgb.size() - 2],
                              m_controlPointsRgb[m_controlPointsRgb.size() - 1]);
            glm::vec3 mshLow = vvv::RGBToMsh(RGBLow);
            glm::vec3 mshHigh = vvv::RGBToMsh(RGBHigh);
            if (mshLow.y > 0.05f && mshHigh.y > 0.05 && glm::abs(mshLow.z - mshHigh.z) > M_PI / 3.f) {
                auto mMid = glm::max(glm::max(mshLow.x, mshHigh.x), 88.f);
                if (samplePosition < 0.5) {
                    mshHigh.x = mMid;
                    mshHigh.y = 0.f;
                    mshHigh.z = 0.f;
                    samplePosition *= 2.f;
                } else {
                    mshLow.x = mMid;
                    mshLow.y = 0.f;
                    mshLow.z = 0.f;
                    samplePosition = samplePosition * 2.f - 1.f;
                }
            }

            if (mshLow.y < 0.05 && mshHigh.y > 0.05) {
                mshLow.z = vvv::adjustHue(mshHigh, mshLow.x);
            } else if (mshHigh.y < 0.05 && mshLow.y > 0.05) {
                mshHigh.z = vvv::adjustHue(mshLow, mshHigh.x);
            }
            return vvv::MshToRGB(glm::mix(mshLow, mshHigh, samplePosition));
        }
        default:
            throw std::runtime_error("unknown color space");
        }
    }

  private:
    bool areControlPointsAreMonotonicallyIncreasing(std::vector<float> &points, size_t stride) {
        float currentPoint = -std::numeric_limits<float>::infinity();

        for (int i = 0; i < points.size(); i += stride) {
            if (points[i] < currentPoint) {
                return false;
            }
            currentPoint = points[i];
        }

        return true;
    }
};
} // namespace vvv