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
    [[nodiscard]] bool renderButtons();                                        // returns true if tf is modified
                  void renderCanvas(glm::vec2 canvas_p0, glm::vec2 canvas_sz);
    [[nodiscard]] bool handleInput( glm::vec2 canvas_p0, glm::vec2 canvas_sz); // returns true if tf is modified

    bool isSorted();
    void sort();

    const float canvasHeight = 100;
    const float snapRadiusInPx = 8;

    GuiInterface::GuiTF1DEntry& entry;
    VectorTransferFunction &tf;

    int selectedControlPoint = 0;
    std::optional<int> selectedColorMap = {};
    bool isDragging = false;
};

void renderGuiTF1D(GuiInterface::GuiTF1DEntry &entry);

}