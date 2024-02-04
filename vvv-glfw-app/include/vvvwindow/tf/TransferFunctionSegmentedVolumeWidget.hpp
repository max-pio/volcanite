#pragma once

#include "vvv/core/GuiInterface.hpp"

namespace vvv {

class GuiTFSegmentedVolumeData {
public:
    explicit GuiTFSegmentedVolumeData(GuiInterface::GuiTFSegmentedVolumeEntry &entry) : e(&entry) {
        int viridsLocation = static_cast<int>(std::find(availableColormaps.begin(), availableColormaps.end(), "viridis") - availableColormaps.begin());
        for(int m = 0; m < e->materials->size(); m++) {
            // initialize all colormaps with viridis
            if(e->colormapConfig[m].precomputedIdx < 0)
                e->colormapConfig[m].precomputedIdx = viridsLocation < availableColormaps.size() ? viridsLocation : 0;
            updateVectorColormap(m);
            if(e->onChanged)
                e->onChanged(m);
        }

        // Disable (-2), Any (-1), attributes (0..) for the visibility test of the material
        discriminatorNames.clear();
        discriminatorNames.emplace_back("Disable");
        discriminatorNames.emplace_back("Any");
        discriminatorNames.insert(discriminatorNames.end(), e->attributeNames.begin(), e->attributeNames.end());
    }

    void updateVectorColormap(int material);
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
    std::vector<std::string> discriminatorNames;

    GuiInterface::GuiTFSegmentedVolumeEntry* e;
};

void renderGuiTFSegmentedVolume(GuiInterface::GuiTFSegmentedVolumeEntry &entry, GpuContextPtr);

} // namespace vvv