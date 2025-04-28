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

#include <utility>
#include <vvv/volren/tf/TransferFunction.hpp>

namespace vvv {

class TransferFunction1D;
class SinglePassCompute;
class UniformReflected;

/// This 2D-Transfer function uses a fixed color map for x-Values and uses polygons to define regions in the plane with positive opacity.
class TransferFunction2D : public TransferFunction {
  public:
    TransferFunction2D(GpuContextPtr ctx, const std::shared_ptr<MultiBuffering> &multiBuffering, uint32_t resolution, uint32_t queue = 0);
    ~TransferFunction2D() override;

    [[nodiscard]] std::pair<vvv::AwaitableHandle, std::shared_ptr<vvv::Buffer>> upload() override;

    std::string preprocessorLabel() override { return "TRANSFER_FUNCTION_MODE_2D"; }

    /// colormap should already be upload()-ed. You need to call @c upload() on @c TransferFunction2D to apply the new colormap. */
    void setColormapTF(std::shared_ptr<TransferFunction1D> colormap) { m_colormapTF = std::move(colormap); }

    /// each polygon is specified with points in range [0-1] and can be in any order. You need to call @c upload() on @c TransferFunction2D to apply the new colormap.
    void setPolygons(const std::vector<std::vector<glm::vec2>> &polygons) {
        m_polygons = polygons;
        m_polygonOpacity.resize(m_polygons.size(), 1);
        m_polygonHasCustomColor.resize(m_polygons.size(), false);
        m_polygonCustomColor.resize(m_polygons.size(), {1, 1, 1});
    }
    [[nodiscard]] const std::vector<std::vector<glm::vec2>> &polygons() const { return m_polygons; }

    void setFeathering(float feathering) { m_feathering = feathering; }
    [[nodiscard]] float feathering() const { return m_feathering; }

    enum Direction : uint32_t { Horizontal,
                                Vertical,
                                Both };
    /// Sets direction for the color map. Horizontal: color based on x-Coord. Vertical: color based on y-Coord. Both: color based on sum of x and y
    void setDirection(Direction direction) { m_direction = direction; }
    [[nodiscard]] Direction direction() { return m_direction; }

    void setPolygonOpacity(int polygonIdx, float opacity) { m_polygonOpacity.at(polygonIdx) = opacity; }
    void setPolygonHasCustomColor(int polygonIdx, bool hasColor) { m_polygonHasCustomColor.at(polygonIdx) = hasColor; }
    void setPolygonCustomColor(int polygonIdx, glm::vec3 color) { m_polygonCustomColor.at(polygonIdx) = color; }
    [[nodiscard]] float polygonOpacity(int polygonIdx) const { return m_polygonOpacity.at(polygonIdx); }
    [[nodiscard]] bool polygonHasCustomColor(int polygonIdx) const { return m_polygonHasCustomColor.at(polygonIdx); }
    [[nodiscard]] glm::vec3 polygonCustomColor(int polygonIdx) const { return m_polygonCustomColor.at(polygonIdx); }

    [[nodiscard]] uint32_t resolution() const { return m_resolution; }

  private:
    [[nodiscard]] std::vector<glm::vec2> preparePolygonData() const;

  private:
    std::vector<std::vector<glm::vec2>> m_polygons;
    std::vector<float> m_polygonOpacity;
    std::vector<bool> m_polygonHasCustomColor;
    std::vector<glm::vec3> m_polygonCustomColor;
    float m_feathering = 0.02f;
    Direction m_direction = Horizontal;
    uint32_t m_resolution;

    /// number of vec2 values. 64 * sizeof(vec2) = 512 Bytes
    const uint32_t polygonStorageBufferCapacity = 64;
    /// number of vec4 values for data per polygon. 16 * sizeof(vec4) = 64 Bytes
    const uint32_t additionalDataStorageBufferCapacity = 16;

    std::unique_ptr<SinglePassCompute> m_computePass;

    std::shared_ptr<TransferFunction1D> m_colormapTF;

    std::unique_ptr<Buffer> m_polygonStorageBuffer;
    std::unique_ptr<Buffer> m_additionalDataStorageBuffer;
    std::shared_ptr<UniformReflected> m_optionsUniform;
};

} // namespace vvv
