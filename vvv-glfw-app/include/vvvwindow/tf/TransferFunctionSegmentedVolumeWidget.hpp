#pragma once

#include "vvv/core/GuiInterface.hpp"

namespace vvv {

class GuiTFSegmentedVolumeData {
public:
    explicit GuiTFSegmentedVolumeData(GuiInterface::GuiTFSegmentedVolumeEntry &entry) : e(&entry) {
        e->initialize();

        // Disable (-2), Any (-1), attributes (0..) for the visibility test of the material
        discriminatorNames.clear();
        discriminatorNames.emplace_back("Disable");
        discriminatorNames.emplace_back("Any");
        discriminatorNames.insert(discriminatorNames.end(), e->attributeNames.begin(), e->attributeNames.end());
    }

    void updateVectorColormap(int material);
    void renderGui(GpuContextPtr ctx);

private:
    std::vector<std::string> discriminatorNames;

    GuiInterface::GuiTFSegmentedVolumeEntry* e;
};

void renderGuiTFSegmentedVolume(GuiInterface::GuiTFSegmentedVolumeEntry &entry, GpuContextPtr);

} // namespace vvv