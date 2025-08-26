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

#ifdef IMGUI

#include "vvvwindow/GuiImgui.hpp"

#include "vvvwindow/tf/TransferFunction1DWidget.hpp"
#include "vvvwindow/tf/TransferFunctionSegmentedVolumeWidget.hpp"

#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#include "imgui/imGuIZMO.quat/imGuIZMOquat.h"
#include "imgui/imgui.h"
#include "imgui/misc/cpp/imgui_stdlib.h"
#include <vvv/util/Paths.hpp>

#include "vvv/util/Logger.hpp"
#include <fmt/core.h>

void GuiImgui::updateGui() {
    // We don't store internal states so far.
    // (ImGui accesses everything directly through pointers)
}

void GuiImgui::renderGui() {
    // check if we have to update GUI scaling (in all childs and for the font)
    constexpr float gui_scaling_eps = 0.2f;
    const bool updateGuiScaling = abs(m_gui_scaling - m_current_gui_scaling) > gui_scaling_eps;
    if (updateGuiScaling || m_firstCall) {
        // if this is called a second time, i.e. a second font is rasterized, some Vulkan image object is not destroyed
        if (!m_firstCall)
            vvv::Logger(vvv::Warn) << "Rescaling the GUI leads to undestroyed Vulkan objects from ImGUI font rasterization!";
        ImGuiIO &io = ImGui::GetIO();
        (void)io;
        io.Fonts->Clear();

#ifdef _WIN32
        // on windows, the path.c_str() returns a wide char pointer which we have to convert to a const char*
        std::string unicode_query = vvv::Paths::findDataPath("QuicksandFamily/Quicksand-Medium.ttf").string();
        io.Fonts->AddFontFromFileTTF(unicode_query.c_str(), m_defaultFontSize * m_gui_scaling);
#else
        io.Fonts->AddFontFromFileTTF(vvv::Paths::findDataPath("QuicksandFamily/Quicksand-Medium.ttf").c_str(),
                                     m_defaultFontSize * m_gui_scaling);
#endif
        ImGui_ImplVulkan_CreateFontsTexture();

        // update the scaling of the GUI if necessary
        if (updateGuiScaling) {
            ImGui::GetStyle().ScaleAllSizes(m_gui_scaling / m_current_gui_scaling);
        }

        // set static render parameters of GuIZMO
        imguiGizmo::cubeSize = 0.15f;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // WINDOW DOCKING
    {
        ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        if (m_firstCall) {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_PassthruCentralNode | static_cast<ImGuiDockNodeFlags_>(ImGuiDockNodeFlags_DockSpace));
            ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

            ImGuiID dock_id_down = 0u, dock_id_left = 0u, dock_id_up = 0u, dock_id_right = 0u;
            ImGuiID dock_id_old;

            // keep track of parents
            std::unordered_map<std::string, ImGuiID> parents = {};

            for (const auto &l : m_docking_layout) {
                const std::string &window = l.first;
                const std::string &loc = l.second;

                if (!m_windows.contains(window)) {
                    vvv::Logger(vvv::Warn) << "can not dock non-existing window " << window;
                    continue;
                }

                if (m_windows.contains(loc)) {
                    // Dock at an existing window
                    if (parents.contains(loc)) {
                        ImGui::DockBuilderDockWindow(window.c_str(), parents[loc]);
                    } else {
                        vvv::Logger(vvv::Warn) << "cannot dock to windows that were not already docked elsewhere (cannot dock " << window << " to " << loc << ")";
                        // would have to create a new docking node as parent for both window and loc..
                    }
                } else {
                    // Dock down / left / up / right of the docking central node
                    // Create a new split location of the central node if none exists yet.
                    // Otherwise, append next to the existing windows.
                    if (loc == "d") {
                        if (dock_id_down == 0u) {
                            dock_id_down = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.3f, nullptr, &dockspace_id);
                            ImGui::DockBuilderDockWindow(window.c_str(), dock_id_down);
                            parents[window] = dock_id_down;
                        } else {
                            dock_id_old = dock_id_down;
                            dock_id_down = ImGui::DockBuilderSplitNode(dock_id_old, ImGuiDir_Right, 0.6f, nullptr, &dock_id_old);
                            ImGui::DockBuilderDockWindow(window.c_str(), dock_id_down);
                            parents[window] = dock_id_down;
                        }
                    } else if (loc == "l") {
                        if (dock_id_left == 0u) {
                            dock_id_left = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.25f, nullptr, &dockspace_id);
                            ImGui::DockBuilderDockWindow(window.c_str(), dock_id_left);
                            parents[window] = dock_id_left;
                        } else {
                            dock_id_old = dock_id_left;
                            dock_id_left = ImGui::DockBuilderSplitNode(dock_id_old, ImGuiDir_Down, 0.6f, nullptr, &dock_id_old);
                            ImGui::DockBuilderDockWindow(window.c_str(), dock_id_left);
                            parents[window] = dock_id_left;
                        }
                    } else if (loc == "u") {
                        if (dock_id_up == 0u) {
                            dock_id_up = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Up, 0.3f, nullptr, &dockspace_id);
                            ImGui::DockBuilderDockWindow(window.c_str(), dock_id_up);
                            parents[window] = dock_id_up;
                        } else {
                            dock_id_old = dock_id_up;
                            dock_id_up = ImGui::DockBuilderSplitNode(dock_id_old, ImGuiDir_Right, 0.6f, nullptr, &dock_id_old);
                            ImGui::DockBuilderDockWindow(window.c_str(), dock_id_up);
                            parents[window] = dock_id_up;
                        }
                    } else if (loc == "r") {
                        if (dock_id_right == 0u) {
                            dock_id_right = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, 0.25f, nullptr, &dockspace_id);
                            ImGui::DockBuilderDockWindow(window.c_str(), dock_id_right);
                            parents[window] = dock_id_right;
                        } else {
                            dock_id_old = dock_id_right;
                            dock_id_right = ImGui::DockBuilderSplitNode(dock_id_old, ImGuiDir_Down, 0.6f, nullptr, &dock_id_old);
                            ImGui::DockBuilderDockWindow(window.c_str(), dock_id_right);
                            parents[window] = dock_id_right;
                        }
                    } else {
                        vvv::Logger(vvv::Warn) << "Unkown window docking location " << loc;
                        continue;
                    }
                }
            }

            ImGui::DockBuilderFinish(dockspace_id);
        }
    }

    // iterate over all windows
    for (const auto &window : m_windows) {

        if (!window.second.isVisible())
            continue;

        // begin window (implicitly pushes the ID of its name)
        ImGui::Begin(window.first.c_str());

        auto columns = window.second.getColumns();
        for (int c_id = 0; c_id < columns.size(); c_id++) {
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
            ImGui::BeginChild((window.second.getName() + std::to_string(c_id)).c_str(), ImVec2(ImGui::GetContentRegionAvail().x / static_cast<float>(columns.size()), 0.f), false, window_flags);

            // iterate over GUI entries
            for (BaseGuiEntry *be : GuiInterface::getEntriesForColumn(columns[c_id])) {
                ImGui::PushID(static_cast<int>(be->id));

                auto gui_get = []<class T>(GuiEntry<T> *e) -> T {
                    if (e->getter)
                        return e->getter();
                    else
                        return *e->value;
                };
                auto gui_set = []<class T>(GuiEntry<T> *e, bool changed, const T &value) {
                    if (changed) {
                        if (e->setter)
                            e->setter(value);
                        else
                            *e->value = value;
                    }
                };

                switch (be->type) {
                case GuiTF1D: {
                    auto e = reinterpret_cast<GuiTF1DEntry *>(be);
                    renderGuiTF1D(*e);
                    break;
                }
                case GuiTFSegmentedVolume: {
                    auto e = reinterpret_cast<GuiTFSegmentedVolumeEntry *>(be);
                    renderGuiTFSegmentedVolume(*e, getCtx());
                    break;
                }
                case GuiString: {
                    auto e = GUI_CAST(be, std::string);
                    auto value = gui_get(e);
                    bool changed = ImGui::InputText(e->label.c_str(), &value);
                    gui_set(e, changed, value);
                    break;
                }
                case GuiBool: {
                    auto e = GUI_CAST(be, bool);
                    auto value = gui_get(e);
                    bool changed = ImGui::Checkbox(e->label.c_str(), &value);
                    gui_set(e, changed, value);
                    break;
                }
                case GuiInt: {
                    auto e = GUI_CAST(be, int);
                    auto value = gui_get(e);
                    bool changed;
                    if (e->min.has_value() && GUI_CAST(be, int)->max.has_value())
                        changed = ImGui::SliderInt(e->label.c_str(), &value, e->min.value(), e->max.value());
                    else
                        changed = ImGui::InputInt(e->label.c_str(), &value);
                    gui_set(e, changed, value);
                    break;
                }
                case GuiIVec2: {
                    auto e = GUI_CAST(be, glm::ivec2);
                    auto value = gui_get(e);
                    bool changed;
                    if (e->min.has_value() && e->max.has_value())
                        changed = ImGui::SliderInt2(e->label.c_str(), &value.r, e->min.value().r, e->max.value().r);
                    else
                        changed = ImGui::InputInt2(e->label.c_str(), &value.r);
                    gui_set(e, changed, value);
                    break;
                }
                case GuiIntRange: {
                    auto e = GUI_CAST(be, glm::ivec2);
                    auto value = gui_get(e);
                    bool changed;
                    if (e->min.has_value() && e->max.has_value())
                        changed = ImGui::DragIntRange2(e->label.c_str(), &value.x, &value.y, static_cast<float>(glm::pow(10, -e->floatDecimals)), e->min.value().r, e->max.value().r);
                    else
                        changed = ImGui::DragIntRange2(e->label.c_str(), &value.x, &value.y, static_cast<float>(glm::pow(10, -e->floatDecimals)), 0.f, 0.f);
                    gui_set(e, changed, value);
                    break;
                }
                case GuiIVec3: {
                    auto e = GUI_CAST(be, glm::ivec3);
                    auto value = gui_get(e);
                    bool changed;
                    if (e->min.has_value() && e->max.has_value())
                        changed = ImGui::SliderInt3(e->label.c_str(), &value.r, e->min.value().r, e->max.value().r);
                    else
                        changed = ImGui::InputInt3(e->label.c_str(), &value.r);
                    gui_set(e, changed, value);
                    break;
                }
                case GuiIVec4: {
                    auto e = GUI_CAST(be, glm::ivec4);
                    auto value = gui_get(e);
                    bool changed;
                    if (e->min.has_value() && e->max.has_value())
                        changed = ImGui::SliderInt4(e->label.c_str(), &value.r, e->min.value().r, e->max.value().r);
                    else
                        changed = ImGui::InputInt4(e->label.c_str(), &value.r);
                    gui_set(e, changed, value);
                    break;
                }
                case GuiFloat: {
                    auto e = GUI_CAST(be, float);
                    auto value = gui_get(e);
                    bool changed;
                    if (e->min.has_value() && e->max.has_value())
                        changed = ImGui::SliderFloat(e->label.c_str(), &value, e->min.value(), e->max.value(), ("%." + std::to_string(e->floatDecimals) + "f").c_str());
                    else
                        // changed = ImGui::InputFloat(e->label.c_str(), &value, 0.0f, 0.0f, ("%." + std::to_string(e->floatDecimals) + "f").c_str());
                        changed = ImGui::DragFloat(e->label.c_str(), &value, 1.0f, 0.0f, 0.0f, ("%." + std::to_string(e->floatDecimals) + "f").c_str());
                    gui_set(e, changed, value);
                    break;
                }

                case GuiVec2: {
                    auto e = GUI_CAST(be, glm::vec2);
                    auto value = gui_get(e);
                    bool changed;
                    if (e->min.has_value() && e->max.has_value())
                        changed = ImGui::SliderFloat2(e->label.c_str(), &value.r, e->min.value().r, e->max.value().r, ("%." + std::to_string(e->floatDecimals) + "f").c_str());
                    else
                        changed = ImGui::InputFloat2(e->label.c_str(), &value.r, ("%." + std::to_string(e->floatDecimals) + "f").c_str());
                    gui_set(e, changed, value);
                    break;
                }
                case GuiFloatRange: {
                    auto e = GUI_CAST(be, glm::vec2);
                    auto value = gui_get(e);
                    bool changed;
                    if (e->min.has_value() && e->max.has_value())
                        changed = ImGui::DragFloatRange2(e->label.c_str(), &value.x, &value.y, static_cast<float>(glm::pow(10, -e->floatDecimals)), e->min.value().r, e->max.value().r, ("%." + std::to_string(e->floatDecimals) + "f").c_str());
                    else
                        changed = ImGui::DragFloatRange2(e->label.c_str(), &value.x, &value.y, static_cast<float>(glm::pow(10, -e->floatDecimals)), 0.f, 0.f, ("%." + std::to_string(e->floatDecimals) + "f").c_str());
                    gui_set(e, changed, value);
                    break;
                }
                case GuiVec3: {
                    auto e = GUI_CAST(be, glm::vec3);
                    auto value = gui_get(e);
                    bool changed;
                    if (e->min.has_value() && e->max.has_value())
                        changed = ImGui::SliderFloat3(e->label.c_str(), &value.r, e->min.value().r, e->max.value().r, ("%." + std::to_string(e->floatDecimals) + "f").c_str());
                    else
                        changed = ImGui::InputFloat3(e->label.c_str(), &value.r, ("%." + std::to_string(e->floatDecimals) + "f").c_str());
                    gui_set(e, changed, value);
                    break;
                }
                case GuiDirection: {
                    auto e = GUI_CAST(be, glm::vec3);
                    auto value = gui_get(e);
                    value = vec3(value.z, value.y, -value.x);
                    float size = ImGui::GetFrameHeightWithSpacing() * 4 - ImGui::GetStyle().ItemSpacing.y * 2;
                    bool changed = ImGui::gizmo3D(("##gizmo_" + std::to_string(e->id)).c_str(), value, size, imguiGizmo::modeDirPlane);
                    ImGui::SameLine();
                    const vvv::Camera *camera_ptr = reinterpret_cast<GuiDirectionEntry *>(be)->camera;
                    quat q = camera_ptr ? quat_cast(glm::mat3(camera_ptr->get_world_to_view_space())) : quat{1, 0, 0, 0};
                    vec3 l = q * -vec3(-value.z, value.y, value.x);
                    ImGui::BeginDisabled();
                    ImGui::gizmo3D(("##gizmo_vis_" + std::to_string(e->id)).c_str(), q, l, size, imguiGizmo::modeFullAxes | imguiGizmo::cubeAtOrigin);
                    ImGui::EndDisabled();
                    imguiGizmo::restoreDirectionColor();
                    ImGui::SameLine();
                    ImGui::Text("x % .2f\ny % .2f\nz % .2f", value.x, value.y, value.z);
                    ImGui::SameLine(-0.0000001f); // should be 0
                    ImGui::LabelText(e->label.c_str(), "\n");
                    gui_set(e, changed, glm::normalize(vec3(-value.z, value.y, value.x)));
                    ImGui::Columns(); // colormap column layout
                    break;
                }
                case GuiVec4: {
                    auto e = GUI_CAST(be, glm::vec4);
                    auto value = gui_get(e);
                    bool changed;
                    if (e->min.has_value() && e->max.has_value())
                        changed = ImGui::SliderFloat4(e->label.c_str(), &value.r, e->min.value().r, e->max.value().r, ("%." + std::to_string(e->floatDecimals) + "f").c_str());
                    else
                        changed = ImGui::InputFloat4(e->label.c_str(), &value.r, ("%." + std::to_string(e->floatDecimals) + "f").c_str());
                    gui_set(e, changed, value);
                    break;
                }
                case GuiColor: {
                    auto e = GUI_CAST(be, glm::vec4);
                    auto value = gui_get(e);
                    bool changed = ImGui::ColorEdit4(e->label.c_str(), &(value.r));
                    gui_set(e, changed, value);
                    break;
                }
                case GuiCombo: {
                    auto e = reinterpret_cast<GuiComboEntry *>(be);
                    if (e->options.empty()) {
                        if (ImGui::BeginCombo(e->label.c_str(), nullptr)) {
                            ImGui::EndCombo();
                        }
                    } else if (ImGui::BeginCombo(e->label.c_str(), e->options.at(*e->selection).c_str())) {
                        for (int i = 0; i < e->options.size(); i++) {
                            const bool is_selected = i == *e->selection;
                            if (ImGui::Selectable(e->options.at(i).c_str(), is_selected)) {
                                *e->selection = i;
                                if (e->onChanged)
                                    e->onChanged(i, true);
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    break;
                }
                case GuiBitFlags: {
                    auto e = reinterpret_cast<GuiBitFlagsEntry *>(be);
                    unsigned int bits_just_set = 0;
                    if (ImGui::CollapsingHeader(e->label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (int i = 0; i < e->options.size(); i++) {
                            if (ImGui::CheckboxFlags(e->options.at(i).c_str(), e->bitfield, e->bitFlags.at(i)))
                                bits_just_set = e->bitFlags.at(i);
                        }
                    }
                    if (e->singleFlagOnly && bits_just_set)
                        *e->bitfield &= bits_just_set;
                    break;
                }
                case GuiAction: {
                    auto e = GUI_FUNC_CAST(be);
                    if (ImGui::Button(e->label.c_str()))
                        e->function();
                    break;
                }
                case GuiLabel: {
                    ImGui::TextUnformatted(be->label.c_str());
                    break;
                }
                case GuiDynamicText: {
                    auto e = GUI_CAST(be, std::string);
                    if (e->label.empty())
                        ImGui::TextUnformatted(e->value->c_str());
                    else
                        ImGui::LabelText(e->label.c_str(), "%s", e->value->c_str());
                    break;
                }
                case GuiProgress: {
                    auto e = GUI_CAST(be, float);
                    float progress = e->getter ? e->getter() : 0.f;
                    // ImVec2(0.0f,0.0f) uses ItemWidth.
                    if (progress >= 0.f) {
                        ImGui::ProgressBar(progress, be->label.empty() ? ImVec2(-FLT_MIN, 0) : ImVec2(0.0f, 0.0f));
                    } else {
                        progress *= -1.f;
                        std::string pstr;
                        int int_progress = static_cast<int>(progress);
                        if (glm::abs(progress - static_cast<float>(int_progress)) < 0.0001f)
                            pstr = fmt::vformat("{}", fmt::make_format_args(int_progress));
                        else
                            pstr = fmt::vformat("{:.4f}", fmt::make_format_args(progress));
                        ImGui::ProgressBar(-progress / 100.f,
                                           be->label.empty() ? ImVec2(-FLT_MIN, 0) : ImVec2(0.0f, 0.0f),
                                           pstr.c_str());
                    }
                    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                    ImGui::TextUnformatted(be->label.c_str());
                    break;
                }
                case GuiSeparator: {
                    ImGui::Separator();
                    break;
                }
                case GuiCustomCode: {
                    auto e = GUI_FUNC_CAST(be);
                    e->function();
                    break;
                }
                default: {
                    vvv::Logger(vvv::Error) << "GuiImgui: cannot render GuiType " << be->type << " for entry " << be->label;
                    break;
                }
                }

                ImGui::PopID();
            }

            ImGui::EndChild();
            if (c_id < columns.size() - 1)
                ImGui::SameLine();
        } // columns

        // end window
        ImGui::End();
    }

    if (updateGuiScaling) {
        m_current_gui_scaling = m_gui_scaling;
    }

    ImGui::Render();

    m_firstCall = false;
}

void GuiImgui::setGuiScaling(float guiScaling) {
    m_gui_scaling = guiScaling;
}

#endif // ifdef IMGUI
