#include <utility>

#include "vvv/core/GuiInterface.hpp"

// implementation of the GuiInterface::GuiElementList is located in GuiElementList.cpp

namespace vvv {

    void GuiInterface::GuiTFSegmentedVolumeEntry::updateVectorColormap(int material) {
        SegmentedVolumeMaterial& mat = (*materials)[material];
        GuiInterface::GuiTFSegmentedVolumeEntry::ColorMapConfig& d = colormapConfig[material];
        // transfer functions are currently fully opaque
        if(mat.tf->m_controlPointsOpacity.size() != 4) {
            mat.tf->m_controlPointsOpacity.resize(4);
            mat.tf->m_controlPointsOpacity[0] = 0.f;
            mat.tf->m_controlPointsOpacity[1] = 1.f;
            mat.tf->m_controlPointsOpacity[2] = 1.f;
            mat.tf->m_controlPointsOpacity[3] = 1.f;
        }
        switch(d.type) {
            case GuiInterface::GuiTFSegmentedVolumeEntry::SVTFSolidColor:
                if(mat.tf->m_controlPointsRgb.size() != 8) {
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
            case GuiInterface::GuiTFSegmentedVolumeEntry::SVTFDivergent:
                // @ToDo implement actual Divergent Colormap computation from their paper
                if(mat.tf->m_controlPointsRgb.size() != 12) {
                    mat.tf->m_controlPointsRgb.resize(12);
                }
                mat.tf->m_controlPointsRgb[0] = 0.f;
                mat.tf->m_controlPointsRgb[1] = d.color[0].r;
                mat.tf->m_controlPointsRgb[2] = d.color[0].g;
                mat.tf->m_controlPointsRgb[3] = d.color[0].b;
                mat.tf->m_controlPointsRgb[4] = 0.5f;
                mat.tf->m_controlPointsRgb[5] = 1.f;
                mat.tf->m_controlPointsRgb[6] = 1.f;
                mat.tf->m_controlPointsRgb[7] = 1.f;
                mat.tf->m_controlPointsRgb[8] = 1.f;
                mat.tf->m_controlPointsRgb[9] = d.color[1].r;
                mat.tf->m_controlPointsRgb[10] = d.color[1].g;
                mat.tf->m_controlPointsRgb[11] = d.color[1].b;
                break;
            case GuiInterface::GuiTFSegmentedVolumeEntry::SVTFPrecomputed:
                mat.tf->m_controlPointsRgb = colormaps::colormaps.at(getAvailableColormaps()[d.precomputedIdx]);
                break;
            default:
                Logger(WARN) << "unknown segmentation volume transfer function colormap " << d.type;
        }
    }

    void GuiInterface::GuiTFSegmentedVolumeEntry::initialize() {
        int viridsLocation = static_cast<int>(std::find(getAvailableColormaps().begin(), getAvailableColormaps().end(), "viridis") - getAvailableColormaps().begin());
        for(int m = 0; m < materials->size(); m++) {
            // initialize all colormaps with viridis if they are not initialized yet
            if (colormapConfig[m].precomputedIdx < 0)
                colormapConfig[m].precomputedIdx =
                        viridsLocation < getAvailableColormaps().size() ? viridsLocation : 0;
            updateVectorColormap(m);
            if (onChanged)
                onChanged(m);

            // safeguard attribute IDs
            if (materials->at(m).discrAttribute >= static_cast<int>(attributeNames.size())) {
                Logger(WARN) << "discriminator attribute index " << materials->at(m).discrAttribute
                             << " of material " << m << " references a non existing attribute. Resetting.";
                materials->at(m).discrAttribute = 0;
            }
            if (materials->at(m).tfAttribute >= static_cast<int>(attributeNames.size())) {
                Logger(WARN) << "attribute index of material " << m
                             << " references a non existing attribute. Resetting.";
                materials->at(m).tfAttribute = 0;
            }
        }
    }

    // the static colormaps we provide for the TF
    std::vector<std::string> GuiInterface::GuiTFSegmentedVolumeEntry::availableColormaps = {};

    const std::vector<std::string>& GuiInterface::GuiTFSegmentedVolumeEntry::getAvailableColormaps() {
        if(availableColormaps.empty()) {
            availableColormaps.reserve(colormaps::good_colormaps.size());
            for (const auto &m: colormaps::good_colormaps)
                availableColormaps.push_back(m.first);
        }
        return availableColormaps;
    }


}   // namespace vvv
