#include "vvv/core/GuiInterface.hpp"

namespace vvv {

    template<class T>
    gui_id GuiInterface::GuiElementList::add(T *v, const std::string &name, GuiType type, int decimals) {
        auto entry = new GuiEntry<T>();
        entry->id = m_id_counter++;
        entry->type = type;
        entry->value = v;
        entry->label = name;
        entry->floatDecimals = decimals;

        m_entries.emplace_back(entry);
        return entry->id;
    }

    template<class T>
    gui_id GuiInterface::GuiElementList::add(T *v, const std::string &name, GuiType type, T min, T max, T step, int decimals) {
        auto entry = new GuiEntry<T>();
        entry->id = m_id_counter++;
        entry->type = type;
        entry->value = v;
        entry->label = name;
        entry->min = min;
        entry->max = max;
        entry->step = step;
        entry->floatDecimals = decimals;

        m_entries.push_back(entry);
        return entry->id;
    }

// --- getter setter ---
    template<class T>
    gui_id GuiInterface::GuiElementList::add(std::function<void(T)> setter, std::function<T()> getter, const std::string &name, GuiType type, int decimals) {
        auto entry = new GuiEntry<T>();
        entry->id = m_id_counter++;
        entry->type = type;
        entry->getter = getter;
        entry->setter = setter;
        entry->label = name;
        entry->floatDecimals = decimals;

        m_entries.push_back(entry);
        return entry->id;
    }

    template<class T> gui_id GuiInterface::GuiElementList::add(std::function<void(T)> setter, std::function<T()> getter, const std::string &name, GuiType type, T min, T max, T step, int decimals) {
        auto entry = new GuiEntry<T>();
        entry->id = m_id_counter++;
        entry->type = type;
        entry->getter = getter;
        entry->setter = setter;
        entry->label = name;
        entry->min = min;
        entry->max = max;
        entry->step = step;
        entry->floatDecimals = decimals;

        m_entries.push_back(entry);
        return entry->id;
    }

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
    gui_id GuiInterface::GuiElementList::addAction(void (*callback)(), std::string name) {
        auto entry = new GuiFuncEntry();
        entry->id = m_id_counter++;
        entry->type = GuiAction;
        entry->label = name;
        entry->function = callback;
        m_entries.push_back(entry);
        return entry->id;
    }
    gui_id GuiInterface::GuiElementList::addAction(std::function<void()> callback, std::string name) {
        auto entry = new GuiFuncEntry();
        entry->id = m_id_counter++;
        entry->type = GuiAction;
        entry->label = name;
        entry->function = callback;
        m_entries.push_back(entry);
        return entry->id;
    }
    gui_id GuiInterface::GuiElementList::addCustomCode(std::function<void()> callback, std::string name) {
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



    std::string sanitizeExportString(std::string s, gui_id id) {
        std::replace(s.begin(), s.end(), ' ', '_');
        if(s.empty())
            return std::to_string(id);
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
                case GuiVec2: {
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
                    vstr = e->selection ? e->options.at(*e->selection) : "*";
                    break;
                }
                case GuiDynamicText: {
                    vstr = *GUI_CAST(be, std::string)->value;
                    break;
                }
                // some parameters do not need to be exported because they are 'constant'
                case GuiAction:
                case GuiLabel:
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

            Logger(INFO) << be->label;

            switch (be->type) {
                // some parameters do not need to be exported because they are 'constant'
                case GuiAction:
                case GuiLabel:
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
                case GuiVec2: {
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
                    in >> *GUI_CAST(be, std::string)->value;
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
