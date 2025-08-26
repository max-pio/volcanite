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

#include "vvvwindow/tf/TransferFunctionSegmentedVolumeWidget.hpp"

#include <imgui/imgui.h>

#include "vvv/util/Paths.hpp"

#ifndef HEADLESS
#include "portable-file-dialogs.h"
#include "stb/stb_image.hpp"
#endif

void vvv::GuiTFSegmentedVolumeData::renderGui(vvv::GpuContextPtr ctx) {
    int id = static_cast<int>(e->id);

    //    ImGui::TextUnformatted((e->label + " " + std::to_string(e->materials->size())).c_str());

    if (e->attributeNames.empty() || e->attributeMinMax.empty())
        throw std::runtime_error("No attributes for segmented volume material editor specified");

    // iterate over all materials (we only show GUIs for all non-disabled materials + 1)
    int displayMaterialCount = 1;
    for (int m = 0; m < e->materials->size(); m++)
        if (e->materials->at(m).discrAttribute != SegmentedVolumeMaterial::DISCR_NONE)
            displayMaterialCount = m + 2;
    displayMaterialCount = glm::min(displayMaterialCount, static_cast<int>(e->materials->size()));
    for (int m = 0; m < displayMaterialCount; m++) {
        SegmentedVolumeMaterial &mat = (*e->materials)[m];
        GuiInterface::GuiTFSegmentedVolumeEntry::ColorMapConfig &colormap_config = e->colormapConfig[m];

        bool materialChanged = false;

        // make child collapsable child
        // ImGui::BeginChild(id++,  ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 24.0f), ImGuiChildFlags_None, ImGuiWindowFlags_MenuBar);
        ImGui::PushID(id++);
        if (ImGui::CollapsingHeader(mat.name, ImGuiTreeNodeFlags_DefaultOpen)) {

            // Text field to give the material a name
            ImGui::PushID(id++);
            ImGui::InputText("Name", mat.name, sizeof(mat.name) / sizeof(char));
            // (we do not set materialChanged when the name was changed)
            ImGui::PopID();

            // Combo to select Discriminator Attribute
            ImGui::PushID(id++);
            if (ImGui::BeginCombo("Filter", discriminatorNames.at(mat.discrAttribute + 2).c_str())) {
                for (int i = 0; i < discriminatorNames.size(); i++) {
                    const bool is_selected = (i - 2) == mat.discrAttribute;
                    if (ImGui::Selectable(discriminatorNames.at(i).c_str(), is_selected)) {
                        mat.discrAttribute = i - 2; // DISCR_NONE / disabled = -2, DISCR_ANY / any = -1
                        if (mat.discrAttribute >= 0)
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
            if (mat.discrAttribute != SegmentedVolumeMaterial::DISCR_NONE) {
                bool colormapChanged = false;
                // Discriminator range
                {
                    glm::vec2 attrRange =
                        mat.discrAttribute >= 0 ? e->attributeMinMax.at(mat.discrAttribute) : glm::vec2(0.f, 0.f);
                    ImGui::BeginDisabled();
                    ImGui::PushID(id++);
                    ImGui::DragFloatRange2("Min / Max", &attrRange.x, &attrRange.y);
                    ImGui::PopID();
                    ImGui::EndDisabled();
                    ImGui::PushID(id++);
                    materialChanged |= ImGui::DragFloatRange2("Bounds",
                                                              &mat.discrInterval.x, &mat.discrInterval.y,
                                                              glm::max(0.1f, (attrRange.y - attrRange.x) / 1000.f),
                                                              attrRange.x,
                                                              attrRange.y);
                    ImGui::PopID();
                }

                ImGui::Separator();
                ImGui::Text("Color Map");
                // ---------------------------------------------------------------------------------------------------------
                //               COLORMAP EDITOR
                glm::vec2 colormap_canvas_p0 = ImGui::GetCursorScreenPos();
                glm::vec2 colormap_canvas_sz = ImGui::GetContentRegionAvail();
                ImGui::NewLine(); // free space to draw the colormap
                ImGui::NewLine();

                ImGui::PushID(id++);
                std::string types[] = {"Solid Color", "Divergent", "Precomputed", "Image Import"};
                if (ImGui::BeginCombo("Type", types[colormap_config.type].c_str())) {
                    for (int i = 0; i < 4; i++) {
                        const bool is_selected = i == colormap_config.type;
                        if (ImGui::Selectable(types[i].c_str(), is_selected)) {
                            auto old_type = colormap_config.type;
                            colormap_config.type = static_cast<GuiInterface::GuiTFSegmentedVolumeEntry::ColorMapType>(i);
                            if (colormap_config.type != old_type) {
                                e->initializeSingleColormap(m, true);
                                colormapChanged = true;
                            }
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
                switch (colormap_config.type) {
                case GuiInterface::GuiTFSegmentedVolumeEntry::SVTFSolidColor:
                    ImGui::Columns(2, nullptr, false); // use columns here to have the same offset as for divergent
                    ImGui::PushItemWidth(-FLT_MIN);
                    ImGui::PushID(id++);
                    colormapChanged |= ImGui::ColorEdit3("", &colormap_config.color[0].r);
                    ImGui::PopID();
                    ImGui::PopItemWidth();
                    ImGui::NextColumn();
                    ImGui::Columns();
                    break;
                case GuiInterface::GuiTFSegmentedVolumeEntry::SVTFDivergent:
                    ImGui::Columns(2, nullptr, false);
                    ImGui::PushItemWidth(-FLT_MIN);
                    ImGui::PushID(id++);
                    colormapChanged |= ImGui::ColorEdit3("", &colormap_config.color[0].r);
                    ImGui::PopID();
                    ImGui::PopItemWidth();
                    ImGui::NextColumn();
                    ImGui::PushItemWidth(-FLT_MIN);
                    ImGui::PushID(id++);
                    colormapChanged |= ImGui::ColorEdit3("", &colormap_config.color[1].r);
                    ImGui::PopID();
                    ImGui::PopItemWidth();
                    ImGui::Columns();
                    break;
                case GuiInterface::GuiTFSegmentedVolumeEntry::SVTFPrecomputed:
                    ImGui::PushItemWidth(-FLT_MIN);
                    ImGui::PushID(id++);
                    if (ImGui::BeginCombo("",
                                          GuiInterface::GuiTFSegmentedVolumeEntry::getAvailableColormaps()[colormap_config.precomputedIdx].c_str())) {
                        for (int i = 0;
                             i < GuiInterface::GuiTFSegmentedVolumeEntry::getAvailableColormaps().size(); i++) {
                            const bool is_selected = i == colormap_config.precomputedIdx;
                            if (ImGui::Selectable(
                                    GuiInterface::GuiTFSegmentedVolumeEntry::getAvailableColormaps()[i].c_str(),
                                    is_selected)) {
                                colormap_config.precomputedIdx = i;
                                colormapChanged = true;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                    ImGui::PopItemWidth();
                    break;
                case GuiInterface::GuiTFSegmentedVolumeEntry::SVTFImport: {
                    // fixed number of control points for now (min(png import pixel, 256))
                    //                    ImGui::PushID(id++);
                    //                    ImGui::NextColumn();
                    //                    colormapChanged |= ImGui::InputInt("", &d.validElementCount, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue);
                    //                    ImGui::PopID();
                    //                    ImGui::NextColumn();

                    ImGui::PushItemWidth(-FLT_MIN);
                    ImGui::PushID(id++);
                    if (ImGui::Button("Choose Colormap File")) {
                        if (!pfd::settings::available()) {
                            Logger(Warn)
                                << "Can not open file dialog for import PNG. Choose other segmentation volume transfer function colormap";
                            break;
                        }
                        auto selected_file = pfd::open_file("Color Map Image File",
                                                            Paths::getHomeDirectory().string() + "/*",
                                                            {"Image File",
                                                             "*.jpg *.jpeg *.bmp *.gif *.png *.pic *.pnm"});
                        if (!selected_file.result().empty()) {
                            int img_width, img_height, img_channels;
                            if (unsigned char *image = stbi_load(selected_file.result().at(0).c_str(),
                                                                 &img_width, &img_height, &img_channels,
                                                                 STBI_rgb_alpha);
                                image) {
                                static constexpr int MAX_COLORMAP_CONTROL_POINTS = 256;
                                colormap_config.color.resize(glm::min(MAX_COLORMAP_CONTROL_POINTS, img_width));
                                const int center_line_offset = (img_height / (2 * 4)) * img_width;
                                for (int c = 0; c < colormap_config.color.size(); c++) {
                                    int pixel = static_cast<int>(static_cast<double>(c) / colormap_config.color.size() *
                                                                 img_width);
                                    pixel = center_line_offset + 4u * glm::clamp(pixel, 0, img_width - 1);
                                    colormap_config.color[c] =
                                        glm::vec3(image[pixel], image[pixel + 1], image[pixel + 2]) / 255.f;
                                }
                                stbi_image_free(image);
                                colormapChanged = true;
                            } else {
                                Logger(Error) << "Failed to load png colormap: " << stbi_failure_reason();
                            }
                        }
                    }
                    ImGui::PopID();
                    ImGui::PopItemWidth();
                    break;
                }
                default:
                    Logger(Warn) << "unknown segmentation volume transfer function colormap " << colormap_config.type;
                }

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
                        glm::vec3 color;
                        color = mat.tf->sampleColor(value_x);
                        draw_list->AddRectFilled({static_cast<float>(x), colormap_canvas_p0.y + 5},
                                                 {static_cast<float>(x + 1), colormap_canvas_p1.y - 5},
                                                 ImGui::GetColorU32(ImVec4(color.r, color.g, color.b, 1)));
                    }
                }

                // opacity slider
                ImGui::PushID(id++);
                materialChanged |= ImGui::SliderFloat("Opacity", &mat.opacity, 0.f, 1.f);
                ImGui::PopID();
                // emission slider
                ImGui::PushID(id++);
                materialChanged |= ImGui::SliderFloat("Emission", &mat.emission, 0.f, 4.f);
                ImGui::PopID();

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
                // TF Attribute Range
                {
                    glm::vec2 attrRange = e->attributeMinMax.at(mat.tfAttribute);
                    ImGui::BeginDisabled();
                    ImGui::PushID(id++);
                    ImGui::DragFloatRange2("Min / Max", &attrRange.x, &attrRange.y);
                    ImGui::PopID();
                    ImGui::EndDisabled();
                    ImGui::PushID(id++);
                    materialChanged |= ImGui::DragFloatRange2("Range",
                                                              &mat.tfMinMax.x, &mat.tfMinMax.y,
                                                              glm::max(0.1f, (attrRange.y - attrRange.x) / 1000.f),
                                                              attrRange.x,
                                                              attrRange.y);
                    ImGui::PopID();
                }

                // warpping mode
                ImGui::PushID(id++);
                materialChanged |= ImGui::RadioButton("Clamp", &mat.wrapping, 0);
                ImGui::SameLine();
                materialChanged |= ImGui::RadioButton("Wrap", &mat.wrapping, 1);
                ImGui::SameLine();
                materialChanged |= ImGui::RadioButton("Random", &mat.wrapping, 2);
                ImGui::PopID();

                ImGui::Separator();
            }
        }
        ImGui::PopID();

        // -------------------------------------------------------------------------------------------------------------
        // ImGui::EndChild();

        if (materialChanged && e->onChanged)
            e->onChanged(m);
    }
}

void vvv::renderGuiTFSegmentedVolume(GuiInterface::GuiTFSegmentedVolumeEntry &entry, GpuContextPtr ctx) {
    if (!entry.widgetData.has_value())
        entry.widgetData.emplace<GuiTFSegmentedVolumeData>(entry);
    std::any_cast<GuiTFSegmentedVolumeData &>(entry.widgetData).renderGui(ctx);
}
