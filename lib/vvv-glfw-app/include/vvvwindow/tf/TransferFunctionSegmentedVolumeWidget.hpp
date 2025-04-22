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

#include "vvv/core/GuiInterface.hpp"

namespace vvv {

class GuiTFSegmentedVolumeData {
  public:
    explicit GuiTFSegmentedVolumeData(GuiInterface::GuiTFSegmentedVolumeEntry &entry) : e(&entry) {
        e->initialize(false);

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

    GuiInterface::GuiTFSegmentedVolumeEntry *e;
};

void renderGuiTFSegmentedVolume(GuiInterface::GuiTFSegmentedVolumeEntry &entry, GpuContextPtr);

} // namespace vvv