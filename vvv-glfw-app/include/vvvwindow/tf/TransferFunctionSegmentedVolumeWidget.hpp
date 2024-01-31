#pragma once

#include "vvv/core/GuiInterface.hpp"

namespace vvv {

class GuiTFSegmentedVolumeData {
public:
    explicit GuiTFSegmentedVolumeData(GuiInterface::GuiTFSegmentedVolumeEntry &entry) : e(&entry), guiMaterials(entry.materials->size()) {
        int viridsLocation = static_cast<int>(std::find(availableColormaps.begin(), availableColormaps.end(), "viridis") - availableColormaps.begin());
        for(int m = 0; m < e->materials->size(); m++) {
            auto& mat = e->materials->at(m);
            mat.discrInterval = e->attributeMinMax[mat.discrAttribute];
            mat.tfMinMax = e->attributeMinMax[mat.tfAttribute];
            // we use opaque transfer functions
            mat.tf->m_controlPointsOpacity.resize(4);
            mat.tf->m_controlPointsOpacity[0] = 0.f;
            mat.tf->m_controlPointsOpacity[1] = 1.f;
            mat.tf->m_controlPointsOpacity[2] = 1.f;
            mat.tf->m_controlPointsOpacity[3] = 1.f;
            // initialize all colormaps with viridis
            guiMaterials[m].precomputedIdx = viridsLocation < availableColormaps.size() ? viridsLocation : 0;
            updateVectorColormap(m);
            if(e->onChanged)
                e->onChanged(m);
        }
    }

    void renderGui(GpuContextPtr ctx);

private:
    static std::vector<std::string> makeAvailableColormaps() {
        std::vector<std::string> v;
        v.reserve(colormaps::good_colormaps.size());
        for(const auto& m : colormaps::good_colormaps)
            v.push_back(m.first);
        return v;
    }
    const std::vector<std::string> availableColormaps = makeAvailableColormaps();
    enum ColorMapType { SolidColor = 0, Divergent, Precomputed, PNGimport};

    struct GuiMaterialData {
        ColorMapType type = Precomputed;
        glm::vec3 color[2] = {glm::vec3(00.2298f,0.2987f,0.7537f), glm::vec3(0.7057f,0.01556f,0.1502f)};
        int precomputedIdx = 0;
    };

    void updateVectorColormap(int material);

    std::vector<GuiMaterialData> guiMaterials;
    GuiInterface::GuiTFSegmentedVolumeEntry* e;
};

void renderGuiTFSegmentedVolume(GuiInterface::GuiTFSegmentedVolumeEntry &entry, GpuContextPtr);

} // namespace vvv