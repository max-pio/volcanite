#include "vvvwindow/tf/TransferFunctionSegmentedVolumeWidget.hpp"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>

void vvv::GuiTFSegmentedVolumeData::renderGui(vvv::GpuContextPtr ctx) {
    int id = static_cast<int>(e->id);

//    ImGui::TextUnformatted((e->label + " " + std::to_string(e->materials->size())).c_str());

    if(e->attributeNames.empty() || e->attributeMinMax.empty())
        throw std::runtime_error("No attributes for segmented volume material editor specified");

    // iterate over all materials (we only show GUIs for all non-disabled materials + 1)
    int displayMaterialCount = 1;
    for(int m = 0; m < e->materials->size(); m++)
        if (e->materials->at(m).discrAttribute != SegmentedVolumeMaterial::DISCR_NONE)
            displayMaterialCount = m + 2;
    displayMaterialCount = glm::min(displayMaterialCount, static_cast<int>(e->materials->size()));
    for(int m = 0; m < displayMaterialCount; m++) {
        SegmentedVolumeMaterial& mat = (*e->materials)[m];
        GuiInterface::GuiTFSegmentedVolumeEntry::ColorMapConfig& d = e->colormapConfig[m];

        bool materialChanged = false;
        bool colormapChanged = false;

        // ToDo: collapsable child for materials
        ImGui::BeginChild(id++,  ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 14.0f), true, ImGuiWindowFlags_MenuBar);

        // Text field to give the material a name
        ImGui::PushID(id++);
        ImGui::InputText("Name", mat.name, sizeof(mat.name) / sizeof(char));
        // (we do not set materialChanged when the name was changed)
        ImGui::PopID();

        // Combo to select Discriminator Attribute
        ImGui::PushID(id++);
        if (ImGui::BeginCombo("Discriminator", discriminatorNames.at(mat.discrAttribute + 2).c_str())) {
            for(int i = 0; i < discriminatorNames.size(); i++) {
                const bool is_selected = (i - 2) == mat.discrAttribute;
                if (ImGui::Selectable(discriminatorNames.at(i).c_str(), is_selected)) {
                    mat.discrAttribute = i - 2; // DISCR_NONE / disabled = -2, DISCR_ANY / any = -1
                    if(mat.discrAttribute >= 0)
                        mat.discrInterval = e->attributeMinMax[mat.discrAttribute];
                    materialChanged = true;
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();


        // skip the rest of the GUI if this material is disabled
        if(mat.discrAttribute != SegmentedVolumeMaterial::DISCR_NONE) {
            // Discriminator range
            {
                glm::vec2 attrRange =
                        mat.discrAttribute >= 0 ? e->attributeMinMax.at(mat.discrAttribute) : glm::vec2(0.f, 0.f);
                ImGui::PushID(id++);
                std::stringstream rangeLabel;
                if (mat.discrAttribute >= 0) {
                    rangeLabel << std::fixed << std::setprecision(3) << "Range  [" << attrRange.x << " - "
                               << attrRange.y << "]";
                } else {
                    rangeLabel << discriminatorNames.at(mat.discrAttribute + 2);
                }
                materialChanged |= ImGui::DragFloatRange2(rangeLabel.str().c_str(),
                                                          &mat.discrInterval.x, &mat.discrInterval.y,
                                                          glm::max(0.1f, (attrRange.y - attrRange.x) / 1000.f),
                                                          attrRange.x,
                                                          attrRange.y);
                ImGui::PopID();
            }

            ImGui::Separator();

            // ---------------------------------------------------------------------------------------------------------
            //               COLORMAP EDITOR
            glm::vec2 colormap_canvas_p0 = ImGui::GetCursorScreenPos();
            glm::vec2 colormap_canvas_sz = ImGui::GetContentRegionAvail();
            ImGui::NewLine();
            ImGui::NewLine();
            ImGui::Columns(2, nullptr, false);   // colormap column layout

            // TF Attribute Combo
            ImGui::PushID(id++);
            if (ImGui::BeginCombo("Attribute", e->attributeNames[mat.tfAttribute].c_str())) {
                for (int i = 0; i < e->attributeNames.size(); i++) {
                    const bool is_selected = i == mat.tfAttribute;
                    if (ImGui::Selectable(e->attributeNames[i].c_str(), is_selected)) {
                        mat.tfAttribute = i;
                        mat.tfMinMax = e->attributeMinMax[mat.tfAttribute];
                        materialChanged = true;
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
            ImGui::NextColumn();
            // TF Attribute Range
            {
                glm::vec2 attrRange = e->attributeMinMax.at(mat.tfAttribute);
                ImGui::PushID(id++);
                std::stringstream rangeLabel;
                rangeLabel << std::fixed << std::setprecision(3) << "Range  [" << attrRange.x << " - "
                           << attrRange.y << "]";
                materialChanged |= ImGui::DragFloatRange2(rangeLabel.str().c_str(),
                                                          &mat.tfMinMax.x, &mat.tfMinMax.y,
                                                          glm::max(0.1f, (attrRange.y - attrRange.x) / 1000.f),
                                                          attrRange.x,
                                                          attrRange.y);
                ImGui::PopID();
            }
            ImGui::NextColumn();

            ImGui::PushID(id++);
            // ToDo: add PNG import for colormaps?
            std::string types[] = {"Solid Color", "Divergent Colormap", "Precomputed Colormap", "PNG Import"};
            if (ImGui::BeginCombo("", types[d.type].c_str())) {
                for (int i = 0; i < 3; i++) {
                    const bool is_selected = i == d.type;
                    if (ImGui::Selectable(types[i].c_str(), is_selected)) {
                        d.type = static_cast< GuiInterface::GuiTFSegmentedVolumeEntry::ColorMapType>(i);
                        colormapChanged = true;
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
            switch (d.type) {
                case GuiInterface::GuiTFSegmentedVolumeEntry::SVTFSolidColor:
                    ImGui::PushID(id++);
                    ImGui::NextColumn();
                    colormapChanged |= ImGui::ColorEdit3("", &d.color[0].r);
                    ImGui::PopID();
                    break;
                case GuiInterface::GuiTFSegmentedVolumeEntry::SVTFDivergent:
                    ImGui::PushID(id++);
                    ImGui::NextColumn();
                    colormapChanged |= ImGui::ColorEdit3("", &d.color[0].r);
                    ImGui::PopID();
                    ImGui::NextColumn();
                    ImGui::NextColumn();
                    ImGui::PushID(id++);
                    colormapChanged |= ImGui::ColorEdit3("", &d.color[1].r);
                    ImGui::PopID();
                    break;
                case GuiInterface::GuiTFSegmentedVolumeEntry::SVTFPrecomputed:
                    ImGui::PushID(id++);
                    ImGui::NextColumn();
                    if (ImGui::BeginCombo("",GuiInterface::GuiTFSegmentedVolumeEntry::getAvailableColormaps()[d.precomputedIdx].c_str())) {
                        for (int i = 0; i < GuiInterface::GuiTFSegmentedVolumeEntry::getAvailableColormaps().size(); i++) {
                            const bool is_selected = i == d.precomputedIdx;
                            if (ImGui::Selectable(GuiInterface::GuiTFSegmentedVolumeEntry::getAvailableColormaps()[i].c_str(), is_selected)) {
                                d.precomputedIdx = i;
                                colormapChanged = true;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                    ImGui::NextColumn();
                    break;
                default:
                    Logger(WARN) << "unknown segmentation volume transfer function colormap " << d.type;
            }
            ImGui::Columns(); // colormap column layout
            if (colormapChanged) {
                materialChanged = true;
                 e->updateVectorColormap(m);
            }
            // draw the colormap
            {
                if (colormap_canvas_sz.x < 50.0f)
                    colormap_canvas_sz.x = 50.0f;
                colormap_canvas_sz.y = ImGui::GetTextLineHeightWithSpacing() * 2.f;

                ImDrawList *draw_list = ImGui::GetWindowDrawList();
                auto colormap_canvas_p1 = colormap_canvas_p0 + colormap_canvas_sz;
                for (int x = static_cast<int>(colormap_canvas_p0.x) + 5;
                     x <= static_cast<int>(colormap_canvas_p1.x) - 5; x++) {
                    float value_x = (x - colormap_canvas_p0.x - 5) / (colormap_canvas_sz.x - 10);
                    auto color = mat.tf->sampleRgb(value_x);
                    draw_list->AddRectFilled({static_cast<float>(x), colormap_canvas_p0.y + 5},
                                             {static_cast<float>(x + 1), colormap_canvas_p1.y - 5},
                                             ImGui::GetColorU32(ImVec4(color.r, color.g, color.b, 1)));
                }
            }
        }


        // -------------------------------------------------------------------------------------------------------------
        ImGui::EndChild();

        if (materialChanged && e->onChanged)
            e->onChanged(m);
    }
}

void vvv::renderGuiTFSegmentedVolume(GuiInterface::GuiTFSegmentedVolumeEntry& entry, GpuContextPtr ctx) {
    if (!entry.widgetData.has_value())
        entry.widgetData.emplace<GuiTFSegmentedVolumeData>(entry);
    std::any_cast<GuiTFSegmentedVolumeData&>(entry.widgetData).renderGui(ctx);
}
