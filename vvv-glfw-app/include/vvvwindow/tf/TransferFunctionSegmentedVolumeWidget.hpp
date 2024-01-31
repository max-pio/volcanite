#pragma once

#include "vvv/core/GuiInterface.hpp"

namespace vvv {

class GuiTFSegmentedVolumeData {
public:
    explicit GuiTFSegmentedVolumeData(GuiInterface::GuiTFSegmentedVolumeEntry &entry) : e(&entry), guiMaterials(entry.materials->size()) {
        for(int m = 0; m < e->materials->size(); m++) {
            updateVectorColormap(m);
            if(e->onChanged)
                e->onChanged(m);
        }
    }

    void renderGui(GpuContextPtr ctx);

private:
    static std::vector<std::string> makeAvailableColormaps() {
        std::vector<std::string> v;
        v.reserve(colormaps::colormaps.size());
        for(const auto& m : colormaps::colormaps)
            v.push_back(m.first);
        return v;
    }
    const std::vector<std::string> availableColormaps = makeAvailableColormaps();
    enum ColorMapType { SolidColor = 0, Divergent, Precomputed, PNGimport};

    struct GuiMaterialData {
        ColorMapType type = Precomputed;
        glm::vec3 color[2] = {glm::vec3(0.9f, 0.05f, 0.1f), glm::vec3(0.05f, 0.1f, 0.9f)};
        int precomputedIdx = 0;
    };

    void updateVectorColormap(int material);

    std::vector<GuiMaterialData> guiMaterials;
    GuiInterface::GuiTFSegmentedVolumeEntry* e;
};

void renderGuiTFSegmentedVolume(GuiInterface::GuiTFSegmentedVolumeEntry &entry, GpuContextPtr);

} // namespace vvv