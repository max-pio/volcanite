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

#include <vvv/core/GuiInterface.hpp>

#include <optional>

namespace vvv {

class GuiTF2DData {
public:
    explicit GuiTF2DData(GuiInterface::GuiTF2DEntry &entry);
    ~GuiTF2DData();

    void renderGui(GpuContextPtr ctx); // returns true if tf is modified

private:
    [[nodiscard]] bool renderButtons(GpuContextPtr ctx);                                        // returns true if tf is modified
    void renderCanvas(glm::vec2 canvas_p0, glm::vec2 canvas_sz);
    [[nodiscard]] bool handleInput(glm::vec2 canvas_p0, glm::vec2 canvas_sz); // returns true if tf is modified

    void updateHistogramTexture(GpuContextPtr ctx);

    GuiInterface::GuiTF2DEntry& entry;
    TransferFunction2D &tf;

    enum class Tool { EditPoints, AddPolygon, AddRect, Delete };
    Tool tool = Tool::EditPoints;

    bool showSettings = false;

    std::optional<int> movingPolygon = {};
    std::optional<std::pair<int, int>> movingPoint = {};
    std::optional<int> lastUsedPolygon = {};
    std::optional<int> currentlyCreatingPolygon = {};
    std::optional<int> selectedColorMap = {};
    bool isDragging = false;

    VkDescriptorSet imguiResultTexture = {};
    VkDescriptorSet imguiHistogramTexture = {};
    std::shared_ptr<Texture> histogramRGBATexture = {};
    std::shared_ptr<SinglePassCompute> histogramCompute = {};
};

void renderGuiTF2D(GuiInterface::GuiTF2DEntry &entry, GpuContextPtr);

}