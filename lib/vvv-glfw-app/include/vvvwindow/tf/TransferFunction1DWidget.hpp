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

#include <vvv/core/GuiInterface.hpp>
#include <vvv/volren/tf/VectorTransferFunction.hpp>

#include <optional>

namespace vvv {

class GuiTF1DData {
  public:
    explicit GuiTF1DData(GuiInterface::GuiTF1DEntry &entry) : entry(entry), tf(*entry.value) {}
    void renderGui(); // returns true if tf is modified

  private:
    [[nodiscard]] bool renderButtons(); // returns true if tf is modified
    void renderCanvas(glm::vec2 canvas_p0, glm::vec2 canvas_sz);
    [[nodiscard]] bool handleInput(glm::vec2 canvas_p0, glm::vec2 canvas_sz); // returns true if tf is modified

    bool isSorted();
    void sort();

    const float canvasHeight = 100;
    const float snapRadiusInPx = 8;

    GuiInterface::GuiTF1DEntry &entry;
    VectorTransferFunction &tf;

    int selectedControlPoint = 0;
    std::optional<int> selectedColorMap = {};
    bool isDragging = false;
};

void renderGuiTF1D(GuiInterface::GuiTF1DEntry &entry);

} // namespace vvv