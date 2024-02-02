#pragma once

#include "vvv/core/GuiInterface.hpp"

namespace vvv {

class GuiTFSegmentedVolumeData {
public:
    explicit GuiTFSegmentedVolumeData(GuiInterface::GuiTFSegmentedVolumeEntry &entry) : e(&entry) {
        int viridsLocation = static_cast<int>(std::find(availableColormaps.begin(), availableColormaps.end(), "viridis") - availableColormaps.begin());
        for(int m = 0; m < e->materials->size(); m++) {
            auto& mat = e->materials->at(m);
            mat.discrAttribute = (m == 0) ? SegmentedVolumeMaterial::DISCR_ANY : SegmentedVolumeMaterial::DISCR_NONE;
            mat.discrInterval = e->attributeMinMax[mat.discrAttribute];
            mat.tfMinMax = e->attributeMinMax[mat.tfAttribute];
            // we use opaque transfer functions
            mat.tf->m_controlPointsOpacity.resize(4);
            mat.tf->m_controlPointsOpacity[0] = 0.f;
            mat.tf->m_controlPointsOpacity[1] = 1.f;
            mat.tf->m_controlPointsOpacity[2] = 1.f;
            mat.tf->m_controlPointsOpacity[3] = 1.f;
            // initialize all colormaps with viridis
            e->colormapConfig[m].precomputedIdx = viridsLocation < availableColormaps.size() ? viridsLocation : 0;
            updateVectorColormap(m);
            if(e->onChanged)
                e->onChanged(m);
        }

        // Disable (-2), Any (-1), attributes (0..) for the visibility test of the material
        discriminatorNames.push_back("Disable");
        discriminatorNames.push_back("Any");
        discriminatorNames.insert(discriminatorNames.end(), e->attributeNames.begin(), e->attributeNames.end());
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
    void updateVectorColormap(int material);

    const std::vector<std::string> availableColormaps = makeAvailableColormaps();
    std::vector<std::string> discriminatorNames;

    GuiInterface::GuiTFSegmentedVolumeEntry* e;
};

void renderGuiTFSegmentedVolume(GuiInterface::GuiTFSegmentedVolumeEntry &entry, GpuContextPtr);

} // namespace vvv