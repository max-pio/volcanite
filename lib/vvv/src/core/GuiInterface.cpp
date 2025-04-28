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

#include <utility>

#include "vvv/core/GuiInterface.hpp"

// implementation of the GuiInterface::GuiElementList is located in GuiElementList.cpp

namespace vvv {

void GuiInterface::GuiTFSegmentedVolumeEntry::updateVectorColormap(int material) {
    SegmentedVolumeMaterial &mat = (*materials)[material];
    GuiInterface::GuiTFSegmentedVolumeEntry::ColorMapConfig &d = colormapConfig[material];
    // transfer functions are currently fully opaque
    if (mat.tf->m_controlPointsOpacity.size() != 4) {
        mat.tf->m_interpolationColorSpace = VectorTransferFunction::RGB;
        mat.tf->m_controlPointsOpacity.resize(4);
        mat.tf->m_controlPointsOpacity[0] = 0.f;
        mat.tf->m_controlPointsOpacity[1] = 1.f;
        mat.tf->m_controlPointsOpacity[2] = 1.f;
        mat.tf->m_controlPointsOpacity[3] = 1.f;
    }
    switch (d.type) {
    case GuiInterface::GuiTFSegmentedVolumeEntry::SVTFSolidColor:
        mat.tf->m_interpolationColorSpace = VectorTransferFunction::RGB;
        if (mat.tf->m_controlPointsRgb.size() != 8) {
            mat.tf->m_controlPointsRgb.resize(8);
        }
        mat.tf->m_controlPointsRgb[0] = 0.f;
        mat.tf->m_controlPointsRgb[1] = d.color[0].r;
        mat.tf->m_controlPointsRgb[2] = d.color[0].g;
        mat.tf->m_controlPointsRgb[3] = d.color[0].b;
        mat.tf->m_controlPointsRgb[4] = 1.f;
        mat.tf->m_controlPointsRgb[5] = d.color[0].r;
        mat.tf->m_controlPointsRgb[6] = d.color[0].g;
        mat.tf->m_controlPointsRgb[7] = d.color[0].b;
        break;
    case GuiInterface::GuiTFSegmentedVolumeEntry::SVTFDivergent: {
        mat.tf->m_interpolationColorSpace = VectorTransferFunction::CIELAB;
        if (mat.tf->m_controlPointsRgb.size() != 12) {
            mat.tf->m_controlPointsRgb.resize(12);
        }
        // set rgb colors for interpolation
        mat.tf->m_controlPointsRgb[0] = 0.f;
        mat.tf->m_controlPointsRgb[1] = d.color[0].r;
        mat.tf->m_controlPointsRgb[2] = d.color[0].g;
        mat.tf->m_controlPointsRgb[3] = d.color[0].b;
        mat.tf->m_controlPointsRgb[8] = 1.f;
        mat.tf->m_controlPointsRgb[9] = d.color[1].r;
        mat.tf->m_controlPointsRgb[10] = d.color[1].g;
        mat.tf->m_controlPointsRgb[11] = d.color[1].b;

        // interpolate colors in CIELAB space and convert back to rgb
        glm::vec3 colMid = mat.tf->sampleColor(0.5f);

        mat.tf->m_controlPointsRgb[4] = 0.5f;
        mat.tf->m_controlPointsRgb[5] = colMid.r;
        mat.tf->m_controlPointsRgb[6] = colMid.g;
        mat.tf->m_controlPointsRgb[7] = colMid.b;

        break;
    }
    case GuiInterface::GuiTFSegmentedVolumeEntry::SVTFPrecomputed:
        mat.tf->m_interpolationColorSpace = VectorTransferFunction::RGB;
        mat.tf->m_controlPointsRgb = colormaps::colormaps.at(getAvailableColormaps()[d.precomputedIdx]);
        break;
    case GuiInterface::GuiTFSegmentedVolumeEntry::SVTFImport: {
        mat.tf->m_interpolationColorSpace = VectorTransferFunction::RGB;
        size_t targetSizeControlPointsRgb = d.color.size() * 4;
        if (mat.tf->m_controlPointsRgb.size() != targetSizeControlPointsRgb) {
            mat.tf->m_controlPointsRgb.resize(targetSizeControlPointsRgb);
        }
        for (int i = 0; i < targetSizeControlPointsRgb; i += 4) {
            glm::vec3 color = d.color[static_cast<int>(i / 4)];
            mat.tf->m_controlPointsRgb[i] = static_cast<float>(i) / 255.f;
            mat.tf->m_controlPointsRgb[i + 1] = color.r;
            mat.tf->m_controlPointsRgb[i + 2] = color.g;
            mat.tf->m_controlPointsRgb[i + 3] = color.b;
        }
    } break;
    default:
        Logger(Warn) << "unknown segmentation volume transfer function colormap " << d.type;
    }
}

void GuiInterface::GuiTFSegmentedVolumeEntry::initialize(bool resetColors) {
    for (int m = 0; m < materials->size(); m++) {
        initializeSingleColormap(m, resetColors);
    }
}

void GuiInterface::GuiTFSegmentedVolumeEntry::initializeSingleColormap(const int matId, bool resetColors) {
    // initialize all colormaps with a good default map if they are not initialized yet
    if (colormapConfig[matId].precomputedIdx < 0)
        colormapConfig[matId].precomputedIdx = getDefaultColorMapIdx();
    if (resetColors) {
        colormapConfig[matId].color.clear();
        switch (colormapConfig[matId].type) {
        case SVTFSolidColor:
            colormapConfig[matId].color.resize(1);
            colormapConfig[matId].color[0] = glm::vec3(0.2298f, 0.2987f, 0.7537f);
            break;
        case SVTFDivergent:
            colormapConfig[matId].color.resize(2);
            colormapConfig[matId].color[0] = glm::vec3(0.2298f, 0.2987f, 0.7537f);
            colormapConfig[matId].color[1] = glm::vec3(0.7057f, 0.01556f, 0.1502f);
            break;
        case SVTFPrecomputed:
            colormapConfig[matId].color.clear();
            break;
        case SVTFImport:
            colormapConfig[matId].color.resize(1);
            colormapConfig[matId].color[0] = glm::vec3(1.f);
            break;
        default:
            Logger(Warn) << "unknown segmentation volume transfer function colormap " << colormapConfig[matId].type;
        }
    }
    updateVectorColormap(matId);
    if (onChanged)
        onChanged(matId);

    // safeguard attribute IDs
    if (materials->at(matId).discrAttribute >= static_cast<int>(attributeNames.size())) {
        Logger(Warn) << "discriminator attribute index " << materials->at(matId).discrAttribute
                     << " of material " << matId << " references a non existing attribute. Resetting.";
        materials->at(matId).discrAttribute = 0;
    }
    if (materials->at(matId).tfAttribute >= static_cast<int>(attributeNames.size())) {
        Logger(Warn) << "attribute index of material " << matId
                     << " references a non existing attribute. Resetting.";
        materials->at(matId).tfAttribute = 0;
    }
}

// the static colormaps we provide for the TF
std::vector<std::string> GuiInterface::GuiTFSegmentedVolumeEntry::availableColormaps = {};

const std::vector<std::string> &GuiInterface::GuiTFSegmentedVolumeEntry::getAvailableColormaps() {
    if (availableColormaps.empty()) {
        availableColormaps.reserve(colormaps::good_colormaps.size());
        for (const auto &m : colormaps::good_colormaps)
            availableColormaps.push_back(m.first);
    }
    return availableColormaps;
}

} // namespace vvv
