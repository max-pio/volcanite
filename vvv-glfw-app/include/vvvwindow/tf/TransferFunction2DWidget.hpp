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