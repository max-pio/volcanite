#include "vvvwindow/tf/TransferFunctionSegmentedVolumeWidget.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_vulkan.h>

void vvv::renderGuiTFSegmentedVolume(GuiInterface::GuiTFSegmentedVolumeEntry& e, GpuContextPtr) {

    // ToDo: use ImGui::pushID to not use labels as ids

    ImGui::TextUnformatted((e.label + " " + std::to_string(e.materials->size())).c_str());

    if(e.attributeNames.empty() || e.attributeMinMax.empty())
        throw std::runtime_error("No attributes for segmented volume material editor specified");

    // iterate over all materials
    for(int m = 0; m < e.materials->size(); m++) {
        SegmentedVolumeMaterial& mat = (*e.materials)[m];
        std::string prefix = ("M" + std::to_string(m) + " ");
        bool materialChanged = false;
        ImGui::BeginChild((prefix + " Material").c_str(),  ImVec2(0, ImGui::GetFontSize() * 10.0f), true, ImGuiWindowFlags_MenuBar);

        // Text field to give the material a name
        ImGui::InputText((prefix + "Name").c_str(), mat.name, sizeof(mat.name) / sizeof(char));

        // Combo to select Discriptor Attribute
        if (ImGui::BeginCombo((prefix + "Discriminator").c_str(), (e.attributeNames.at(mat.discrAttribute) + "    [" + std::to_string(e.attributeMinMax.at(mat.discrAttribute).x) + " - " + std::to_string(e.attributeMinMax.at(mat.discrAttribute).y) + "]").c_str())) {
            for(int i = 0; i < e.attributeNames.size(); i++) {
                const bool is_selected = i == mat.discrAttribute;
                if (ImGui::Selectable(e.attributeNames.at(i).c_str(), is_selected)) {
                    mat.discrAttribute = i;
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
            materialChanged = true;
        }

        // Discriminator range
        glm::vec2 attrRange = e.attributeMinMax.at(mat.discrAttribute);
        ImGui::DragFloatRange2((prefix + "Range").c_str(),
                   &mat.discrInterval.x, &mat.discrInterval.y,
                   glm::max(1.f, (attrRange.y - attrRange.x) / 1000.f),
                   e.attributeMinMax.at(mat.discrAttribute).x, e.attributeMinMax.at(mat.discrAttribute).y);

        ImGui::Separator();

        // ToDo: replace VectorControlPoint TF with normal 1D TF?
        ImGui::ColorEdit3((prefix + "Color").c_str(), &(mat.tf->m_controlPointsRgb.at(0)));

        ImGui::EndChild();
        if (materialChanged && e.onChanged)
            e.onChanged(m);
    }
}

