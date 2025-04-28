//  Copyright (C) 2024, Patrick Jaberg, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
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

#include <vvvwindow/tf/TransferFunction1DWidget.hpp>

#include <vvv/volren/tf/builtin.hpp>

#include <imgui/imgui.h>

namespace vvv {

void renderGuiTF1D(GuiInterface::GuiTF1DEntry &entry) {
    if (!entry.widgetData.has_value())
        entry.widgetData.emplace<GuiTF1DData>(entry);

    std::any_cast<GuiTF1DData &>(entry.widgetData).renderGui();
}

inline glm::vec2 fromPixelSpace(glm::vec2 canvas_p0, glm::vec2 canvas_sz, glm::vec2 v) {
    float x = (v.x - canvas_p0.x - 5) / (canvas_sz.x - 10);
    float y = 1 - (v.y - canvas_p0.y - 5) / (canvas_sz.y - 10);
    return glm::clamp(glm::vec2{x, y}, 0.0f, 1.0f);
}
inline glm::vec2 toPixelSpace(glm::vec2 canvas_p0, glm::vec2 canvas_sz, glm::vec2 v) {
    float x = canvas_p0.x + 5 + v.x * (canvas_sz.x - 10);
    float y = canvas_p0.y + 5 + (1 - v.y) * (canvas_sz.y - 10);
    return {x, y};
}

void GuiTF1DData::renderGui() {
    bool modified = false;

    if (renderButtons())
        modified = true;

    // use ImGUI functions to get available space to paint the TF to
    glm::vec2 canvas_p0 = ImGui::GetCursorScreenPos();
    glm::vec2 canvas_sz = ImGui::GetContentRegionAvail();
    if (canvas_sz.x < 50.0f)
        canvas_sz.x = 50.0f;
    canvas_sz.y = canvasHeight;

    // This will catch our interactions
    ImGui::InvisibleButton("canvas", canvas_sz, ImGuiButtonFlags_MouseButtonLeft);

    renderCanvas(canvas_p0, canvas_sz);

    if (handleInput(canvas_p0, canvas_sz))
        modified = true;

    if (modified && entry.onChanged)
        entry.onChanged();
}

bool GuiTF1DData::renderButtons() {
    bool modified = false;

    ImGui::PushItemWidth(ImGui::CalcTextSize("X:0.99").x + 10);
    {
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("Index:99").x + 20);
        ImGui::DragInt("##index", &selectedControlPoint, 0.25f, 0, static_cast<int>(tf.m_controlPointsOpacity.size() / 2 - 1), "Index:%d");
        ImGui::SameLine();
        if (ImGui::Button("remove") && selectedControlPoint > 0 && selectedControlPoint < tf.m_controlPointsOpacity.size() / 2 - 1) {
            tf.m_controlPointsOpacity.erase(tf.m_controlPointsOpacity.begin() + 2 * selectedControlPoint);
            tf.m_controlPointsOpacity.erase(tf.m_controlPointsOpacity.begin() + 2 * selectedControlPoint);
            selectedControlPoint--;
            if (selectedControlPoint == 0)
                selectedControlPoint = 1;
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::DragFloat("##x", &tf.m_controlPointsOpacity[2 * selectedControlPoint], 0.01f, 0, 1, "X:%.2f")) {
            if (selectedControlPoint == 0)
                tf.m_controlPointsOpacity[0] = 0;
            if (selectedControlPoint == tf.m_controlPointsOpacity.size() / 2 - 1)
                tf.m_controlPointsOpacity[tf.m_controlPointsOpacity.size() - 2] = 1;
            if (!isSorted())
                sort();
            modified = true;
        }
        ImGui::SameLine();
        if (ImGui::DragFloat("##y", &tf.m_controlPointsOpacity[2 * selectedControlPoint + 1], 0.01f, 0, 1, "Y:%.2f")) {
            modified = true;
        }
        ImGui::SameLine();

        const char *currColormapName = "colormap";
        for (int i = 0; auto &[name, _] : vvv::colormaps::colormaps) {
            if (i == selectedColorMap.value_or(-1))
                currColormapName = name.c_str();
            i++;
        }
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("black, orange and white").x + 30);
        if (ImGui::BeginCombo("", currColormapName)) {
            for (int n = 0; auto &[name, value] : vvv::colormaps::colormaps) {
                const bool is_selected = selectedColorMap.value_or(-1) == n;
                if (ImGui::Selectable(name.c_str(), is_selected)) {
                    selectedColorMap = n;
                    entry.value->m_controlPointsRgb = value;
                    modified = true;
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
                n++;
            }
            ImGui::EndCombo();
        }
    }
    ImGui::PopItemWidth();

    return modified;
}

void GuiTF1DData::renderCanvas(glm::vec2 canvas_p0, glm::vec2 canvas_sz) {
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    auto canvas_p1 = canvas_p0 + canvas_sz;

    // Draw Colormap
    for (int x = static_cast<int>(canvas_p0.x) + 5; x <= static_cast<int>(canvas_p1.x) - 5; x++) {
        const float value_x = (static_cast<float>(x) - canvas_p0.x - 5.f) / (canvas_sz.x - 10.f);
        const auto color = entry.value->sampleColor(value_x);
        draw_list->AddRectFilled({static_cast<float>(x), canvas_p0.y + 5.f}, {static_cast<float>(x + 1), canvas_p1.y - 5.f}, ImGui::GetColorU32(ImVec4(color.r, color.g, color.b, 1.f)));
    }

    // Draw histogram
    if (entry.histogram) {
        // const auto histColor1 = ImGui::GetColorU32(IM_COL32(0, 0, 0, 64));
        // const auto histColor2 = ImGui::GetColorU32(IM_COL32(255, 255, 255, 64));
        const float maxValue = *std::max_element(entry.histogram->begin(), entry.histogram->end());

        for (int i = 0; i < entry.histogram->size(); i++) {

            auto transform = [this](const int i) {
                auto x = static_cast<float>(i) / static_cast<float>(entry.histogram->size());
                if (entry.histogramMin && entry.histogramMax)
                    x = (x - *entry.histogramMin) / (*entry.histogramMax - *entry.histogramMin);
                return x;
            };
            auto p0 = toPixelSpace(canvas_p0, canvas_sz, {transform(i), 0.0f});
            auto p1 = toPixelSpace(canvas_p0, canvas_sz, {transform(i + 1), (*entry.histogram)[i] / maxValue});

            draw_list->AddRectFilled(p0, p1, ImGui::GetColorU32(IM_COL32(128, 128, 128, 128)));
        }
    }

    // Draw opacity polygon lines
    auto black = ImGui::GetColorU32(IM_COL32(0, 0, 0, 255));
    auto white = ImGui::GetColorU32(IM_COL32(255, 255, 255, 255));
    for (int i = 0; i < tf.m_controlPointsOpacity.size() / 2 - 1; i++) {
        draw_list->AddLine(toPixelSpace(canvas_p0, canvas_sz, {tf.m_controlPointsOpacity[2 * i], tf.m_controlPointsOpacity[2 * i + 1]}),
                           toPixelSpace(canvas_p0, canvas_sz, {tf.m_controlPointsOpacity[2 * i + 2], tf.m_controlPointsOpacity[2 * i + 3]}),
                           black, 3);
        draw_list->AddLine(toPixelSpace(canvas_p0, canvas_sz, {tf.m_controlPointsOpacity[2 * i], tf.m_controlPointsOpacity[2 * i + 1]}),
                           toPixelSpace(canvas_p0, canvas_sz, {tf.m_controlPointsOpacity[2 * i + 2], tf.m_controlPointsOpacity[2 * i + 3]}),
                           white, 1);
    }

    // Draw opacity polygon dots
    for (int i = 0; i < tf.m_controlPointsOpacity.size() / 2; i++) {
        draw_list->AddCircleFilled(toPixelSpace(canvas_p0, canvas_sz, {tf.m_controlPointsOpacity[2 * i], tf.m_controlPointsOpacity[2 * i + 1]}),
                                   3, black);
        draw_list->AddCircleFilled(toPixelSpace(canvas_p0, canvas_sz, {tf.m_controlPointsOpacity[2 * i], tf.m_controlPointsOpacity[2 * i + 1]}),
                                   2, (i == selectedControlPoint) ? ImGui::GetColorU32(ImGuiCol_PlotHistogram) : white);
    }
}

bool GuiTF1DData::handleInput(glm::vec2 canvas_p0, glm::vec2 canvas_sz) {
    bool modified = false;

    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !isDragging) {
        // check if there is control point to drag
        isDragging = false;
        for (int i = 0; i < tf.m_controlPointsOpacity.size() / 2; i++) {
            ImVec2 controlPoint = toPixelSpace(canvas_p0, canvas_sz, {tf.m_controlPointsOpacity[2 * i], tf.m_controlPointsOpacity[2 * i + 1]});
            ImVec2 diff{controlPoint.x - ImGui::GetIO().MousePos.x, controlPoint.y - ImGui::GetIO().MousePos.y};
            float distSqr = diff.x * diff.x + diff.y * diff.y;
            if (distSqr < snapRadiusInPx * snapRadiusInPx) {
                // drag this point
                selectedControlPoint = i;
                isDragging = true;
            }
        }
        if (!isDragging) {
            // insert new point
            auto pos = fromPixelSpace(canvas_p0, canvas_sz, ImGui::GetIO().MousePos);
            for (int i = 0; i < tf.m_controlPointsOpacity.size() / 2 - 1; i++) {
                if (tf.m_controlPointsOpacity[2 * i] < pos.x && tf.m_controlPointsOpacity[2 * i + 2] >= pos.x) {
                    // insert new point after point i
                    tf.m_controlPointsOpacity.insert(tf.m_controlPointsOpacity.begin() + 2 * i + 2, pos.x);
                    tf.m_controlPointsOpacity.insert(tf.m_controlPointsOpacity.begin() + 2 * i + 3, pos.y);
                    modified = true;
                    selectedControlPoint = i + 1;
                    isDragging = true;
                }
            }
        }
    }
    if (ImGui::IsItemActive() && isDragging) {
        auto pos = fromPixelSpace(canvas_p0, canvas_sz, ImGui::GetIO().MousePos);
        if (selectedControlPoint == 0)
            pos.x = 0;
        if (selectedControlPoint == tf.m_controlPointsOpacity.size() / 2 - 1)
            pos.x = 1;
        tf.m_controlPointsOpacity[2 * selectedControlPoint] = pos.x;
        tf.m_controlPointsOpacity[2 * selectedControlPoint + 1] = pos.y;
        if (!isSorted())
            sort();
        modified = true;
    } else {
        isDragging = false;
    }

    return modified;
}

bool GuiTF1DData::isSorted() {
    auto &opacity = tf.m_controlPointsOpacity;
    for (int i = 0; i < opacity.size() / 2 - 1; i++) {
        if (opacity[2 * i] > opacity[2 * i + 2])
            return false;
    }
    return true;
}

void GuiTF1DData::sort() {
    auto &opacity = tf.m_controlPointsOpacity;

    using CP = std::tuple<int, float, float>;
    std::vector<CP> controlPoints(opacity.size() / 2);

    for (int i = 0; i < controlPoints.size(); i++)
        controlPoints[i] = std::make_tuple(i, opacity[2 * i], opacity[2 * i + 1]);

    std::sort(controlPoints.begin(), controlPoints.end(), [](CP a, CP b) { return std::get<1>(a) < std::get<1>(b); });

    int newSelection = 0;
    for (int i = 0; auto &[idx, x, y] : controlPoints) {
        if (selectedControlPoint == idx) {
            newSelection = i;
        }
        opacity[2 * i] = x;
        opacity[2 * i + 1] = y;
        i++;
    }
    selectedControlPoint = newSelection;
}

} // namespace vvv