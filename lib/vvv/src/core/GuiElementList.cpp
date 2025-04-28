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
// general GuiElementList functions
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

// GuiElementList Gui Types
gui_id GuiInterface::GuiElementList::addTF1D(VectorTransferFunction *tf, std::vector<float> *histogram, float *histMin, float *histMax, std::function<void()> onChanged) {
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

gui_id GuiInterface::GuiElementList::addTFSegmentedVolume(std::vector<SegmentedVolumeMaterial> *materials, const std::vector<std::string> &attributeNames, const std::vector<glm::vec2> &attributeMinMax, std::function<void(int)> onChanged, const std::string &name) {
    auto entry = new GuiTFSegmentedVolumeEntry();
    entry->id = m_id_counter;
    m_id_counter += 100; // pragmatic: we reserve more IDs because the TF editor will add multiple ImGUI elements with PushID(id + X)
    entry->type = GuiTFSegmentedVolume;
    entry->materials = materials;
    entry->attributeNames = attributeNames;
    entry->attributeMinMax = attributeMinMax;
    entry->onChanged = std::move(onChanged);
    entry->colormapConfig = std::vector<GuiTFSegmentedVolumeEntry::ColorMapConfig>(materials->size());
    entry->label = name;
    m_entries.push_back(entry);
    entry->initialize(true);
    return entry->id;
}

gui_id GuiInterface::GuiElementList::addDirection(glm::vec3 *v, const Camera *camera, const std::string &name) {
    auto entry = new GuiDirectionEntry();
    entry->id = m_id_counter++;
    entry->type = GuiDirection;
    entry->value = v;
    entry->camera = camera;
    entry->getter = nullptr;
    entry->setter = nullptr;
    entry->label = name;
    m_entries.push_back(entry);
    return entry->id;
}

gui_id GuiInterface::GuiElementList::addDirection(std::function<void(glm::vec3)> setter, std::function<glm::vec3()> getter, const Camera *camera, const std::string &name) {
    auto entry = new GuiDirectionEntry();
    entry->id = m_id_counter++;
    entry->type = GuiDirection;
    entry->value = {};
    entry->camera = camera;
    entry->getter = std::move(getter);
    entry->setter = std::move(setter);
    entry->label = name;
    m_entries.push_back(entry);
    return entry->id;
}

// special types and grouping
gui_id GuiInterface::GuiElementList::addCombo(int *selection, const std::vector<std::string> &options, std::function<void(int, bool)> onChanged, const std::string &name) {
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
gui_id GuiInterface::GuiElementList::addBitFlags(unsigned int *bitfield, const std::vector<std::string> &options, const std::vector<unsigned int> &bitFlags, bool singleFlagOnly, const std::string &name) {
    if (options.size() != bitFlags.size())
        throw std::runtime_error("BitFlags option labels and bit flags vectors must have the same size");
    auto entry = new GuiBitFlagsEntry();
    entry->id = m_id_counter++;
    entry->type = GuiBitFlags;
    entry->bitfield = bitfield;
    entry->options = options;
    entry->bitFlags = bitFlags;
    entry->singleFlagOnly = singleFlagOnly;
    entry->label = name;
    m_entries.push_back(entry);
    return entry->id;
}
gui_id GuiInterface::GuiElementList::addAction(void (*callback)(), const std::string &name) {
    auto entry = new GuiFuncEntry();
    entry->id = m_id_counter++;
    entry->type = GuiAction;
    entry->label = name;
    entry->function = callback;
    m_entries.push_back(entry);
    return entry->id;
}
gui_id GuiInterface::GuiElementList::addAction(const std::function<void()> callback, const std::string &name) {
    auto entry = new GuiFuncEntry();
    entry->id = m_id_counter++;
    entry->type = GuiAction;
    entry->label = name;
    entry->function = std::move(callback);
    m_entries.push_back(entry);
    return entry->id;
}
gui_id GuiInterface::GuiElementList::addCustomCode(std::function<void()> callback, const std::string &name) {
    auto entry = new GuiFuncEntry();
    entry->id = m_id_counter++;
    entry->type = GuiCustomCode;
    entry->label = name;
    entry->function = std::move(callback);
    m_entries.push_back(entry);
    return entry->id;
}
gui_id GuiInterface::GuiElementList::addLabel(const std::string &name) {
    auto entry = new BaseGuiEntry();
    entry->id = m_id_counter++;
    entry->type = GuiLabel;
    entry->label = name;
    m_entries.push_back(entry);
    return entry->id;
}
gui_id GuiInterface::GuiElementList::addDynamicText(std::string *text, const std::string &name) {
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
    if (s.empty())
        return std::string("GUI_") + std::to_string(id);
    return s;
}

bool GuiInterface::GuiElementList::writeParameters(std::ostream &out) const {
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
        case GuiTF1D: {
            Logger(Warn) << "Exporting transfer functions not yet supported!";
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
        case GuiIVec2:
        case GuiIntRange: {
            auto value = gui_get(GUI_CAST(be, glm::ivec2));
            for (int i = 0; i < 2; i++)
                vstr += std::to_string(value[i]) + (i < 1 ? " " : "");
            break;
        }
        case GuiIVec3: {
            auto value = gui_get(GUI_CAST(be, glm::ivec3));
            for (int i = 0; i < 3; i++)
                vstr += std::to_string(value[i]) + (i < 1 ? " " : "");
            break;
        }
        case GuiIVec4: {
            auto value = gui_get(GUI_CAST(be, glm::ivec4));
            for (int i = 0; i < 4; i++)
                vstr += std::to_string(value[i]) + (i < 1 ? " " : "");
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
            for (int i = 0; i < 2; i++)
                vstr += std::to_string(value[i]) + (i < 1 ? " " : "");
            break;
        }
        case GuiVec3:
        case GuiDirection: {
            auto value = gui_get(GUI_CAST(be, glm::vec3));
            for (int i = 0; i < 3; i++)
                vstr += std::to_string(value[i]) + (i < 2 ? " " : "");
            break;
        }
        case GuiVec4:
        case GuiColor: {
            auto value = gui_get(GUI_CAST(be, glm::vec4));
            for (int i = 0; i < 4; i++)
                vstr += std::to_string(value[i]) + (i < 3 ? " " : "");
            break;
        }
        case GuiCombo: {
            auto e = reinterpret_cast<GuiComboEntry *>(be);
            vstr = e->selection ? sanitizeExportString(e->options.at(*e->selection)) : "0";
            break;
        }
        case GuiBitFlags: {
            auto e = reinterpret_cast<GuiBitFlagsEntry *>(be);
            vstr = e->bitfield ? std::to_string(*e->bitfield) : "*";
            break;
        }
        case GuiDynamicText: {
            vstr = sanitizeExportString(*GUI_CAST(be, std::string)->value);
            break;
        }
        case GuiTFSegmentedVolume: {
            auto e = reinterpret_cast<GuiTFSegmentedVolumeEntry *>(be);
            vstr = std::to_string(e->materials->size()) + " ";
            for (int i = 0; i < e->materials->size(); i++) {
                const auto &mat = e->materials->at(i);
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
                const auto &[type, precomputedIdx, color] = e->colormapConfig[i];
                vstr.append(std::to_string(color.size()) + " ");
                for (int j = 0; j < color.size(); j++) {
                    auto &c = color[j];
                    vstr.append(std::to_string(c.r) + " " + std::to_string(c.g) + " " + std::to_string(c.b) + " ");
                }
                vstr.append(std::to_string(precomputedIdx) + " ");
                vstr.append(std::to_string(static_cast<int>(type)));
                if (i != e->materials->size() - 1)
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
            Logger(Warn) << "Could not export parameter type " << be->type << " for entry " << be->label;
            break;
        }
        }

        if (!vstr.empty()) {
            out << sanitizeExportString(be->label, be->id) << ": " << vstr << std::endl;
        }

        if (out.fail())
            return false;
    }
    return true;
}

bool checkLabel(std::istream &in, GuiInterface::BaseGuiEntry *be) {
    std::string label;
    in >> label;
    std::string expected = sanitizeExportString(be->label, be->id) + ":";
    if (label != expected) {
        Logger(Warn) << "Reading parameter for " << label << " instead of expected " << expected;
        return false;
    }
    return true;
}

bool GuiInterface::GuiElementList::readParameter(const std::string &parameter_label, std::istream &parameter_stream) {
    // check if this element list contains a parameter of the given label_name
    if (auto it = std::ranges::find_if(m_entries, [parameter_label](const BaseGuiEntry *g) { return sanitizeExportString(g->label, g->id) + ":" == parameter_label; });
        it != m_entries.end()) {
        auto gui_set = []<class T>(GuiEntry<T> *e, const bool changed, T value) {
            if (changed) {
                if (e->setter)
                    e->setter(value);
                else
                    *e->value = value;
            }
        };

        switch (BaseGuiEntry *be = it[0]; be->type) {
        // some parameters do not need to be exported because they are 'constant'
        case GuiAction:
        case GuiLabel:
        case GuiProgress:
        case GuiSeparator:
        case GuiCustomCode:
            break;

        case GuiTF1D: {
            Logger(Warn) << "Importing transfer functions not yet supported.";
            break;
        }
        case GuiBool: {
            bool v;
            parameter_stream >> v;
            gui_set(GUI_CAST(be, bool), true, v);
            break;
        }
        case GuiInt: {
            int v;
            parameter_stream >> v;
            gui_set(GUI_CAST(be, int), true, v);
            break;
        }
        case GuiIVec2:
        case GuiIntRange: {
            glm::ivec2 v;
            for (int i = 0; i < 2; i++)
                parameter_stream >> v[i];
            gui_set(GUI_CAST(be, glm::ivec2), true, v);
            break;
        }
        case GuiIVec3: {
            glm::ivec3 v;
            for (int i = 0; i < 3; i++)
                parameter_stream >> v[i];
            gui_set(GUI_CAST(be, glm::ivec3), true, v);
            break;
        }
        case GuiIVec4: {
            glm::ivec4 v;
            for (int i = 0; i < 4; i++)
                parameter_stream >> v[i];
            gui_set(GUI_CAST(be, glm::ivec4), true, v);
            break;
        }
        case GuiFloat: {
            float v;
            parameter_stream >> v;
            gui_set(GUI_CAST(be, float), true, v);
            break;
        }
        case GuiString: {
            std::string v;
            parameter_stream >> v;
            gui_set(GUI_CAST(be, std::string), true, v);
            break;
        }
        case GuiVec2:
        case GuiFloatRange: {
            glm::vec2 v;
            for (int i = 0; i < 2; i++)
                parameter_stream >> v[i];
            gui_set(GUI_CAST(be, glm::vec2), true, v);
            break;
        }
        case GuiVec3:
        case GuiDirection: {
            glm::vec3 v;
            for (int i = 0; i < 3; i++)
                parameter_stream >> v[i];
            gui_set(GUI_CAST(be, glm::vec3), true, v);
            break;
        }
        case GuiVec4:
        case GuiColor: {
            glm::vec4 v;
            for (int i = 0; i < 4; i++)
                parameter_stream >> v[i];
            gui_set(GUI_CAST(be, glm::vec4), true, v);
            break;
        }
        case GuiCombo: {
            auto e = reinterpret_cast<GuiComboEntry *>(be);
            std::string v;
            parameter_stream >> v;
            v = sanitizeImportString(v);
            int option = -1;
            for (int i = 0; i < e->options.size(); i++) {
                if (e->options[i] == v) {
                    option = i;
                    break;
                }
            }
            if (option < 0) {
                Logger(Warn) << "Could not set option " << v << " for parameter " << e->label;
                return false;
            }
            *e->selection = option;
            if (e->onChanged)
                e->onChanged(option, false);
            break;
        }
        case GuiBitFlags: {
            auto e = reinterpret_cast<GuiBitFlagsEntry *>(be);
            parameter_stream >> *e->bitfield;
            break;
        }
        case GuiDynamicText: {
            std::string text;
            parameter_stream >> text;
            *GUI_CAST(be, std::string)->value = sanitizeImportString(text);
            break;
        }
        case GuiTFSegmentedVolume: {
            auto e = reinterpret_cast<GuiTFSegmentedVolumeEntry *>(be);
            size_t matCount;
            parameter_stream >> matCount;
            if (e->materials->size() != matCount) {
                Logger(Error) << "Material count does not match imported file material count";
                return false;
            }

            for (int m = 0; m < matCount; m++) {
                auto &mat = e->materials->at(m);
                std::string name;
                parameter_stream >> name;
                sanitizeImportString(name);
                if (name == "#")
                    mat.name[0] = '\0';
                else
                    memcpy(mat.name, name.data(), sizeof(mat.name));
                parameter_stream >> mat.discrAttribute;
                parameter_stream >> mat.discrInterval.x;
                parameter_stream >> mat.discrInterval.y;
                parameter_stream >> mat.tfAttribute;
                parameter_stream >> mat.tfMinMax.x;
                parameter_stream >> mat.tfMinMax.y;
                parameter_stream >> mat.opacity;
                parameter_stream >> mat.emission;
                parameter_stream >> mat.wrapping;
                //
                auto &cm = e->colormapConfig[m];
                size_t colormap_control_points = 0;
                parameter_stream >> colormap_control_points;
                if (colormap_control_points > 65536) {
                    Logger(Error) << "Invalid color map control point count " << colormap_control_points;
                    return false;
                }
                cm.color.resize(colormap_control_points);
                for (int i = 0; i < cm.color.size(); i++) {
                    glm::vec3 &c = cm.color[i];
                    parameter_stream >> c.r;
                    parameter_stream >> c.g;
                    parameter_stream >> c.b;
                }
                parameter_stream >> cm.precomputedIdx;
                int type;
                parameter_stream >> type;
                if (type < 0 || type > 3) {
                    Logger(Error) << "Unsupported color map type " << type;
                    return false;
                }

                cm.type = static_cast<GuiTFSegmentedVolumeEntry::ColorMapType>(type);
            }
            e->initialize(false);
            break;
        }
        default: {
            Logger(Warn) << "Could not import parameter type " << be->type << " for entry " << be->label;
            break;
        }
        }
        // parameter was consumed
        return true;
    } else {
        // parameter was not consumed
        return false;
    }
}

} // namespace vvv
