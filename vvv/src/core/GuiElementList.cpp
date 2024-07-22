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

namespace vvv {
    bool GuiInterface::GuiElementList::remove(gui_id id) {
        auto it = std::find_if(m_entries.begin(), m_entries.end(), [id](const BaseGuiEntry *g) { return g->id == id; });
        if (it != m_entries.end()) {
            m_entries.erase(it);
            return true;
        } else
            return false;
    }
    void GuiInterface::GuiElementList::clear() {
        for (BaseGuiEntry *e : m_entries)
            delete e;
        m_entries.clear();
    }

    gui_id GuiInterface::GuiElementList::addTF1D(VectorTransferFunction* tf, std::vector<float> *histogram, float *histMin, float *histMax, std::function<void()> onChanged) {
        auto entry = new GuiTF1DEntry();
        entry->id = m_id_counter++;
        entry->type = GuiTF1D;
        entry->value = tf;
        entry->histogram = histogram;
        entry->histogramMin = histMin;
        entry->histogramMax = histMax;
        entry->onChanged = std::move(onChanged);
        m_entries.push_back(entry);
        return entry->id;
    }
    gui_id GuiInterface::GuiElementList::addTF2D(TransferFunction2D* tf, Texture* histogramTexture, bool* histogramChanged, std::function<void()> onChanged, glm::vec2* histogramMin , glm::vec2* histogramMax) {
        auto entry = new GuiTF2DEntry();
        entry->id = m_id_counter++;
        entry->type = GuiTF2D;
        entry->value = tf;
        entry->histogramTexture = histogramTexture;
        entry->histogramMin = histogramMin;
        entry->histogramMax = histogramMax;
        entry->histogramChanged = histogramChanged;
        entry->onChanged = std::move(onChanged);
        m_entries.push_back(entry);
        return entry->id;
    }

    gui_id  GuiInterface::GuiElementList::addTFSegmentedVolume(std::vector<SegmentedVolumeMaterial> *materials, const std::vector<std::string>& attributeNames, const std::vector<glm::vec2>& attributeMinMax, std::function<void(int)> onChanged, const std::string& name) {
        auto entry = new GuiTFSegmentedVolumeEntry();
        entry->id = m_id_counter;
        m_id_counter += 100;    // pragmatic: we reserve more IDs because the TF editor will add multiple ImGUI elements with PushID(id + X)
        entry->type = GuiTFSegmentedVolume;
        entry->materials = materials;
        entry->attributeNames = attributeNames;
        entry->attributeMinMax = attributeMinMax;
        entry->onChanged = std::move(onChanged);
        entry->colormapConfig = std::vector<GuiTFSegmentedVolumeEntry::ColorMapConfig>(materials->size());
        entry->label = name;
        m_entries.push_back(entry);
        entry->initialize();
        return entry->id;
    }


    // special types and grouping
    gui_id GuiInterface::GuiElementList::addCombo(int* selection, const std::vector<std::string>& options, std::function<void(int)> onChanged, const std::string& name) {
        auto entry = new GuiComboEntry();
        entry->id = m_id_counter++;
        entry->type = GuiCombo;
        entry->selection = selection;
        entry->onChanged = std::move(onChanged);
        entry->options = options;
        entry->label = name;
        m_entries.push_back(entry);
        return entry->id;
    }
    gui_id GuiInterface::GuiElementList::addAction(void (*callback)(), const std::string& name) {
        auto entry = new GuiFuncEntry();
        entry->id = m_id_counter++;
        entry->type = GuiAction;
        entry->label = name;
        entry->function = callback;
        m_entries.push_back(entry);
        return entry->id;
    }
    gui_id GuiInterface::GuiElementList::addAction(std::function<void()> callback, const std::string& name) {
        auto entry = new GuiFuncEntry();
        entry->id = m_id_counter++;
        entry->type = GuiAction;
        entry->label = name;
        entry->function = callback;
        m_entries.push_back(entry);
        return entry->id;
    }
    gui_id GuiInterface::GuiElementList::addCustomCode(std::function<void()> callback, const std::string& name) {
        auto entry = new GuiFuncEntry();
        entry->id = m_id_counter++;
        entry->type = GuiCustomCode;
        entry->label = name;
        entry->function = callback;
        m_entries.push_back(entry);
        return entry->id;
    }
    gui_id GuiInterface::GuiElementList::addLabel(std::string name) {
        auto entry = new BaseGuiEntry();
        entry->id = m_id_counter++;
        entry->type = GuiLabel;
        entry->label = name;
        m_entries.push_back(entry);
        return entry->id;
    }
    gui_id GuiInterface::GuiElementList::addDynamicText(std::string* text, std::string name) {
        auto entry = new GuiEntry<std::string>();
        entry->id = m_id_counter++;
        entry->value = text;
        entry->type = GuiDynamicText;
        entry->label = name;
        m_entries.push_back(entry);
        return entry->id;
    }
    gui_id GuiInterface::GuiElementList::addSeparator() {
        auto entry = new BaseGuiEntry();
        entry->id = m_id_counter++;
        entry->type = GuiSeparator;
        entry->label = "Separator" + std::to_string(entry->id);
        m_entries.push_back(entry);
        return entry->id;
    }

    std::string sanitizeExportString(std::string s) {
        std::replace(s.begin(), s.end(), ' ', '~');
        return s;
    }

    std::string sanitizeImportString(std::string s) {
        std::replace(s.begin(), s.end(), '~', ' ');
        return s;
    }

    std::string sanitizeExportString(std::string s, gui_id id) {
        std::replace(s.begin(), s.end(), ' ', '_');
        if(s.empty())
            return std::string("GUI_") + std::to_string(id);
        return s;
    }

    bool GuiInterface::GuiElementList::writeParameters(std::ostream& out) const {
        for (BaseGuiEntry *be : m_entries) {
            auto gui_get = []<class T>(GuiEntry<T> *e) -> T {
                if (e->getter)
                    return e->getter();
                else
                    return *e->value;
            };

            // the string containing the parameter value. Will be exported / written if not empty
            std::string vstr;

            switch (be->type) {
                case GuiTF1D:
                case GuiTF2D: {
                    Logger(WARN) << "Exporting transfer functions not yet supported!";
                    break;
                }
                case GuiBool: {
                    vstr = std::to_string(gui_get(GUI_CAST(be, bool)));
                    break;
                }
                case GuiInt: {
                    vstr = std::to_string(gui_get(GUI_CAST(be, int)));
                    break;
                }
                case GuiFloat: {
                    vstr = std::to_string(gui_get(GUI_CAST(be, float)));
                    break;
                }
                case GuiString: {
                    vstr = gui_get(GUI_CAST(be, std::string));
                    break;
                }
                case GuiVec2:
                case GuiFloatRange: {
                    auto value = gui_get(GUI_CAST(be, glm::vec2));
                    for(int i = 0; i < 2; i++)
                        vstr += std::to_string(value[i]) + (i < 2 ? " " : "");
                    break;
                }
                case GuiVec3:
                case GuiDirection: {
                    auto value = gui_get(GUI_CAST(be, glm::vec3));
                    for(int i = 0; i < 3; i++)
                        vstr += std::to_string(value[i]) + (i < 3 ? " " : "");
                    break;
                }
                case GuiVec4:
                case GuiColor: {
                    auto value = gui_get(GUI_CAST(be, glm::vec4));
                    for(int i = 0; i < 4; i++)
                        vstr += std::to_string(value[i]) + (i < 4 ? " " : "");
                    break;
                }
                case GuiCombo: {
                    auto e = reinterpret_cast<GuiComboEntry*>(be);
                    vstr = e->selection ? sanitizeExportString(e->options.at(*e->selection)) : "*";
                    break;
                }
                case GuiDynamicText: {
                    vstr = sanitizeExportString(*GUI_CAST(be, std::string)->value);
                    break;
                }
                case GuiTFSegmentedVolume: {
                    auto e = reinterpret_cast<GuiTFSegmentedVolumeEntry*>(be);
                    vstr = std::to_string(e->materials->size()) + " ";
                    for(int i = 0; i < e->materials->size(); i++) {
                        const auto& mat = e->materials->at(i);
                        std::string name = sanitizeExportString(mat.name);
                        vstr.append(name.empty() ? "# " : name + " ");
                        vstr.append(std::to_string(mat.discrAttribute) + " ");
                        vstr.append(std::to_string(mat.discrInterval.x) + " ");
                        vstr.append(std::to_string(mat.discrInterval.y) + " ");
                        vstr.append(std::to_string(mat.tfAttribute) + " ");
                        vstr.append(std::to_string(mat.tfMinMax.x) + " ");
                        vstr.append(std::to_string(mat.tfMinMax.y) + " ");
                        vstr.append(std::to_string(mat.opacity) + " ");
                        vstr.append(std::to_string(mat.emission) + " ");
                        vstr.append(std::to_string(mat.wrapping) + " ");
                        //
                        const auto& cm = e->colormapConfig[i];
                        for(auto c : cm.color)
                            vstr.append(std::to_string(c.r) + " " + std::to_string(c.g) + " " + std::to_string(c.b) + " ");
                        vstr.append(std::to_string(cm.precomputedIdx) + " ");
                        vstr.append(std::to_string(static_cast<int>(cm.type)));
                        if(i != e->materials->size() - 1)
                            vstr.append(" ");
                    }
                    break;
                }
                // some parameters do not need to be exported because they are 'constant'
                case GuiAction:
                case GuiLabel:
                case GuiProgress:
                case GuiSeparator:
                case GuiCustomCode:
                    break;
                default: {
                    Logger(WARN) << "Could not export parameter type " << be->type << " for entry " << be->label;
                    break;
                }
            }

            if(!vstr.empty()) {
                out << sanitizeExportString(be->label, be->id) << ": " << vstr << std::endl;
            }

            if(!out.good())
                return false;
        }
        return true;
    }

    bool checkLabel(std::istream& in, GuiInterface::BaseGuiEntry* be) {
        std::string label;
        in >> label;
        std::string expected = sanitizeExportString(be->label, be->id) + ":";
        if(label != expected) {
            Logger(WARN) << "Reading parameter for " << label << " instead of expected " << expected;
            return false;
        }
        return true;
    }

    bool GuiInterface::GuiElementList::readParameters(std::istream& in) {
        for (BaseGuiEntry *be : m_entries) {
            auto gui_set = []<class T>(GuiEntry<T> *e, bool changed, T value) {
                if (changed) {
                    if (e->setter)
                        e->setter(value);
                    else
                        *e->value = value;
                }
            };

            switch (be->type) {
                // some parameters do not need to be exported because they are 'constant'
                case GuiAction:
                case GuiLabel:
                case GuiProgress:
                case GuiSeparator:
                case GuiCustomCode:
                    break;

                case GuiTF1D:
                case GuiTF2D: {
                    Logger(WARN) << "Importing transfer functions not yet supported!";
                    break;
                }
                case GuiBool: {
                    if(!checkLabel(in, be))
                        return false;
                    bool v;
                    in >> v;
                    gui_set(GUI_CAST(be, bool), true, v);
                    break;
                }
                case GuiInt: {
                    if(!checkLabel(in, be))
                        return false;
                    int v;
                    in >> v;
                    gui_set(GUI_CAST(be, int), true, v);
                    break;
                }
                case GuiFloat: {
                    if(!checkLabel(in, be))
                        return false;
                    float v;
                    in >> v;
                    gui_set(GUI_CAST(be, float), true, v);
                    break;
                }
                case GuiString: {
                    if(!checkLabel(in, be))
                        return false;
                    std::string v;
                    in >> v;
                    gui_set(GUI_CAST(be, std::string), true, v);
                    break;
                }
                case GuiVec2:
                case GuiFloatRange: {
                    if(!checkLabel(in, be))
                        return false;
                    glm::vec2 v;
                    for(int i = 0; i < 2; i++)
                        in >> v[i];
                    gui_set(GUI_CAST(be, glm::vec2), true, v);
                    break;
                }
                case GuiVec3:
                case GuiDirection: {
                    if(!checkLabel(in, be))
                        return false;
                    glm::vec3 v;
                    for(int i = 0; i < 3; i++)
                        in >> v[i];
                    gui_set(GUI_CAST(be, glm::vec3), true, v);
                    break;
                }
                case GuiVec4:
                case GuiColor: {
                    if(!checkLabel(in, be))
                        return false;
                    glm::vec4 v;
                    for(int i = 0; i < 4; i++)
                        in >> v[i];
                    gui_set(GUI_CAST(be, glm::vec4), true, v);
                    break;
                }
                case GuiCombo: {
                    if(!checkLabel(in, be))
                        return false;
                    auto e = reinterpret_cast<GuiComboEntry*>(be);
                    std::string v;
                    in >> v;
                    v = sanitizeImportString(v);
                    int option = -1;
                    for(int i = 0; i < e->options.size(); i++) {
                        if(e->options[i] == v) {
                            option = i;
                            break;
                        }
                    }
                    if(option < 0 ) {
                        Logger(WARN) << "Could not set option " << v << " for parameter " << e->label;
                        return false;
                    }
                    *e->selection = option;
                    break;
                }
                case GuiDynamicText: {
                    if(!checkLabel(in, be))
                        return false;
                    std::string text;
                    in >> text;
                    *GUI_CAST(be, std::string)->value = sanitizeImportString(text);
                    break;
                }
                case GuiTFSegmentedVolume: {
                    if(!checkLabel(in, be))
                        return false;

                    auto e = reinterpret_cast<GuiTFSegmentedVolumeEntry*>(be);
                    size_t matCount;
                    in >> matCount;
                    if(e->materials->size() != matCount) {
                        Logger(ERROR) << "Material count does not match imported file material count";
                        return false;
                    }

                    for(int m = 0; m < matCount; m++) {
                        auto& mat = e->materials->at(m);
                        std::string name;
                        in >> name;
                        sanitizeImportString(name);
                        if(name == "#")
                            mat.name[0] = '\0';
                        else
                            memcpy(mat.name, name.data(),sizeof(mat.name));
                        in >> mat.discrAttribute;
                        in >> mat.discrInterval.x;
                        in >> mat.discrInterval.y;
                        in >> mat.tfAttribute;
                        in >> mat.tfMinMax.x;
                        in >> mat.tfMinMax.y;
                        in >> mat.opacity;
                        in >> mat.emission;
                        in >> mat.wrapping;
                        //
                        auto& cm = e->colormapConfig[m];
                        for(glm::vec3& c : cm.color) {
                            in >> c.r;
                            in >> c.g;
                            in >> c.b;
                        }
                        in >> cm.precomputedIdx;
                        int type;
                        in >> type;
                        if(type < 0 || type > 2) {
                            Logger(ERROR) << "Unsupported color map type " << type;
                            return false;
                        }

                        cm.type = static_cast<GuiTFSegmentedVolumeEntry::ColorMapType>(type);

                        e->initialize();
                    }
                    break;
                }
                default: {
                    Logger(WARN) << "Could not import parameter type " << be->type << " for entry " << be->label;
                    break;
                }
            }

            if(!in.good())
                return false;
        }
        return true;
    }


} // namespace vvv
