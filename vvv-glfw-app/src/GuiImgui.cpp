#ifdef IMGUI

#include "vvvwindow/GuiImgui.hpp"

#include "vvvwindow/tf/TransferFunction1DWidget.hpp"
#include "vvvwindow/tf/TransferFunction2DWidget.hpp"

#include "vvv/core/GpuContext.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_stdlib.h"
#include "imgui/imGuIZMO.quat/imGuIZMOquat.h"
#include "imgui/implot/implot.h"
#include <vvv/util/Paths.hpp>

#include "vvv/util/Logger.hpp"

void GuiImgui::updateGui() {
    // We don't store internal states so far.
    // (ImGui accesses everything directly through pointers)
}


void GuiImgui::renderGui() {
    // check if we have to update GUI scaling (in all childs and for the font)
    constexpr float gui_scaling_eps = 0.2f;
    const bool updateGuiScaling = abs(m_gui_scaling - m_current_gui_scaling) > gui_scaling_eps;
    if(updateGuiScaling || m_firstCall) {
        // TODO: if this is called a second time, i.e. a second font is rasterized, some Vulkan image object is not destroyed
        if(!m_firstCall)
            vvv::Logger(vvv::WARN) << "Rescaling the GUI leads to undestroyed Vulkan objects from ImGUI font rasterization!";
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.Fonts->Clear();

        io.Fonts->AddFontFromFileTTF(vvv::Paths::findDataPath("Quicksand-Medium.ttf").c_str(), m_defaultFontSize * m_gui_scaling);
        getCtx()->executeCommands(ImGui_ImplVulkan_CreateFontsTexture, {.hostWait = true});
        ImGui_ImplVulkan_DestroyFontUploadObjects();
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // iterate over all windows
    for(const auto& window : m_windows) {
        // begin window (implicitly pushes the ID of its name)
        ImGui::Begin(window.first.c_str());

        auto columns = window.second.getColumns();
        for(int c_id = 0; c_id < columns.size(); c_id++) {
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
            ImGui::BeginChild((window.second.getName() + std::to_string(c_id)).c_str(), ImVec2(ImGui::GetWindowContentRegionWidth() / columns.size(), 0), false, window_flags);

            // update the scaling of the GUI if necessary
            if(updateGuiScaling) {
                ImGui::GetStyle().ScaleAllSizes(m_gui_scaling / m_current_gui_scaling);
            }

            // iterate over GUI entries
            for (BaseGuiEntry *be : GuiInterface::getEntriesForColumn(columns[c_id])) {
                ImGui::PushID(be->id);

                auto gui_get = []<class T>(GuiEntry<T> *e) -> T {
                    if (e->getter)
                        return e->getter();
                    else
                        return *e->value;
                };
                auto gui_set = []<class T>(GuiEntry<T> *e, bool changed, T value) {
                    if (changed) {
                        if (e->setter)
                            e->setter(value);
                        else
                            *e->value = value;
                    }
                };

                switch (be->type) {
                case GuiTF1D: {
                    auto e = reinterpret_cast<GuiTF1DEntry*>(be);
                    renderGuiTF1D(*e);
                    break;
                }
                case GuiTF2D: {
                    auto e = reinterpret_cast<GuiTF2DEntry*>(be);
                    renderGuiTF2D(*e, getCtx());
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
                case GuiString: {
                    auto e = GUI_CAST(be, std::string);
                    auto value = gui_get(e);
                    bool changed = ImGui::InputText(e->label.c_str(), &value);
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
                    float size = ImGui::GetFrameHeightWithSpacing() * 4 - ImGui::GetStyle().ItemSpacing.y * 2;
                    bool changed = ImGui::gizmo3D("##gizmo1", value, size, imguiGizmo::modeDirPlane);
                    ImGui::SameLine(-0.0000001f); // should be 0 but it's buggy..
                    ImGui::LabelText(e->label.c_str(), "\n");
                    gui_set(e, changed, value);
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
                    auto e = reinterpret_cast<GuiComboEntry*>(be);
                    if (e->options.empty()) {
                        if (ImGui::BeginCombo(e->label.c_str(), nullptr)) {
                            ImGui::EndCombo();
                        }
                    } else if (ImGui::BeginCombo(e->label.c_str(), e->options.at(*e->selection).c_str())) {
                        for(int i = 0; i < e->options.size(); i++) {
                            const bool is_selected = i == *e->selection;
                            if (ImGui::Selectable(e->options.at(i).c_str(), is_selected)) {
                                *e->selection = i;
                                if (e->onChanged) e->onChanged(i);
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
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
                    ImGui::TextUnformatted(e->value->c_str());
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
                    vvv::Logger(vvv::ERROR) << "GuiImgui: cannot render GuiType " << be->type << " for entry " << be->label;
                    break;
                }
                }

                ImGui::PopID();
            }

            ImGui::EndChild();
            if (c_id < columns.size()-1)
                ImGui::SameLine();
        } // columns

        // end window
        ImGui::End();
    }

    if(updateGuiScaling) {
        m_current_gui_scaling = m_gui_scaling;
    }

    ImGui::Render();

    m_firstCall = false;
}

void GuiImgui::setGuiScaling(float guiScaling) {
    m_gui_scaling = guiScaling;
}

#endif //ifdef IMGUI
