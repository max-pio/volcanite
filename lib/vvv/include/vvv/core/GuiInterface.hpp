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

#pragma once

#include <utility>
#include <vvv/util/Logger.hpp>
#include <vvv/volren/tf/SegmentedVolumeMaterial.hpp>
#include <vvv/volren/tf/VectorTransferFunction.hpp>

#include <glm/glm.hpp>

#include <any>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

typedef size_t gui_id;

// Note: for historical reasons, parameters and parameter management use the term GUI - graphical user interface -
// even though the parameters and their structure can technically be managed without any graphical interface at all.
// An example is a python binding interface where parameters can be managed through scripts.
// We could rename all of Gui* to Param* but this would have large side effects.

// Steps to add a new data or entry type T:
// (1) create an entry to GuiType enum
// (2s) create add<T>(...) functions for T in GuiElementList class
// optional: (3) create a GUI_*_CAST define for casting from the BaseGuiEntry to the right derived entry class
//
// optional:
// (3) update the methods to render GUI in the derived classes of GuiInterface to include the new type

#define PROPERTY_REF(F, T, G)                                                           \
    virtual gui_id F(T *v, const std::string &name = "") { return add<T>(v, name, G); } \
    virtual gui_id F(std::function<void(T)> setter, std::function<T()> getter, const std::string &name = "") { return add<T>(setter, getter, name, G); }

#define PROPERTY_REF_MINMAX(F, T, G)                                                                                        \
    virtual gui_id F(T *v, const std::string &name, T min, T max, T step) { return add<T>(v, name, G, min, max, step, 0); } \
    virtual gui_id F(std::function<void(T)> setter, std::function<T()> getter, const std::string &name, T min, T max, T step) { return add<T>(setter, getter, name, G, min, max, step, 0); }

#define FLOAT_PROPERTY_REF(F, T, G)                                                                                                                                                  \
    virtual gui_id F(T *v, const std::string &name = "", int decimals = 3) { return add<T>(v, name, G, decimals); }                                                                  \
    virtual gui_id F(T *v, const std::string &name, T min, T max, T step, int decimals = 3) { return add<T>(v, name, G, min, max, step, decimals); }                                 \
    virtual gui_id F(std::function<void(T)> setter, std::function<T()> getter, const std::string &name = "", int decimals = 3) { return add<T>(setter, getter, name, G, decimals); } \
    virtual gui_id F(std::function<void(T)> setter, std::function<T()> getter, const std::string &name, T min, T max, T step, int decimals = 3) { return add<T>(setter, getter, name, G, min, max, step, decimals); }

#define GUI_CAST(e, T) (reinterpret_cast<GuiEntry<T> *>(e))
#define GUI_FUNC_CAST(e) (reinterpret_cast<GuiFuncEntry *>(e))

namespace vvv {

/// @brief Connection to a (graphical) parameter interface.
///
/// Can contain multiple GUI windows that are identified by their name. A window is obtained with the get(windowName) method.
/// If a window with that name doesn't exist yet, it is created.\n
/// \n
/// Each window contains a number of columns. Each column is a GuiElementList where elements can be added in a sequential manner.
/// Properties are added in a sequential manner to a window using the add[Type] methods which return an unique id corresponding to this GUI element.
/// Each property can be given a name, that is used as its label in the GUI. Seperators can be used to group GUI elements.
/// The gui changes the property either directly through a pointer to the property or with a function pointer to a setter.
/// \n
/// The interface automatically enters all added properties to a vector of GuiEntries. Base classes should work hand in hand with the
/// rendering window or window framework to display the list or properties, for example by using an explicit Gui engine. In a minimal case,
/// this requires only some kind of "renderGui()" method in the base class, that iterates over all m_windows and their elements in m_entries and display according gui elements.
/// \n
/// You can use the addCustomCode method to add an entry than runs a lambda function.
/// This can be used for quick prototyping, for example directly adding ImGUI-Code when using the ImGUI backend.
class GuiInterface {
  protected:
    enum GuiType { GuiNoneType,
                   GuiBool,
                   GuiInt,
                   GuiFloat,
                   GuiString,
                   GuiIVec2,
                   GuiIntRange,
                   GuiIVec3,
                   GuiIVec4,
                   GuiVec2,
                   GuiFloatRange,
                   GuiVec3,
                   GuiDirection,
                   GuiVec4,
                   GuiColor,
                   GuiCombo,
                   GuiBitFlags,
                   GuiAction,
                   GuiLabel,
                   GuiDynamicText,
                   GuiProgress,
                   GuiSeparator,
                   GuiTF1D,
                   GuiTFSegmentedVolume,
                   GuiCustomCode };

    // ------------------------------- GUI ENTRIES ------------------------------------ //
  public:
    virtual ~GuiInterface() = default;
    struct BaseGuiEntry {
        virtual ~BaseGuiEntry() = default;
        gui_id id{};
        GuiType type{GuiNoneType};
        std::string label{};
    };

    template <class T>
    struct GuiEntry : BaseGuiEntry {
        T *value = nullptr;
        std::function<T()> getter = nullptr;
        std::function<void(T)> setter = nullptr;
        std::optional<T> min = {};
        std::optional<T> max = {};
        std::optional<T> step = {};
        int floatDecimals = 3;
    };

    struct GuiFuncEntry : BaseGuiEntry {
        std::function<void()> function;
    };

    struct GuiTF1DEntry : BaseGuiEntry {
        VectorTransferFunction *value = nullptr;
        std::function<void()> onChanged = {};
        std::vector<float> *histogram = nullptr;
        float *histogramMin = nullptr;
        float *histogramMax = nullptr;
        std::any widgetData = {};
    };

    struct GuiTFSegmentedVolumeEntry : BaseGuiEntry {
        std::vector<SegmentedVolumeMaterial> *materials = {};
        std::function<void(int)> onChanged = {};
        std::vector<std::string> attributeNames = {};
        std::vector<glm::vec2> attributeMinMax = {};

        // colormap information (stored here so we can import/export)
        enum ColorMapType { SVTFSolidColor = 0,
                            SVTFDivergent,
                            SVTFPrecomputed,
                            SVTFImport };
        struct ColorMapConfig {
            ColorMapType type = SVTFDivergent;
            int precomputedIdx = getDefaultColorMapIdx();
            std::vector<glm::vec3> color = {};
        };
        const static int maxPixelsForColormap = 256;
        std::vector<ColorMapConfig> colormapConfig = {};
        // additional widget data
        std::any widgetData = {};

      private:
        static std::vector<std::string> availableColormaps;
        static int getDefaultColorMapIdx() {
            const std::vector<std::string> &c = getAvailableColormaps();
            int l = static_cast<int>(std::find(c.begin(), c.end(), "coolwarm") - c.begin());
            return l < getAvailableColormaps().size() ? l : 0;
        }

      public:
        void initialize(bool resetColors = false);
        void initializeSingleColormap(int matId, bool resetColors = false);
        void updateVectorColormap(int material);
        static const std::vector<std::string> &getAvailableColormaps();
    };

    struct GuiDirectionEntry : GuiEntry<glm::vec3> {
        const Camera *camera = nullptr;
    };

    struct GuiComboEntry : BaseGuiEntry {
        int *selection = nullptr;
        std::function<void(int, bool)> onChanged = {};
        std::vector<std::string> options = {};
    };

    struct GuiBitFlagsEntry : BaseGuiEntry {
        unsigned int *bitfield = nullptr;
        std::vector<std::string> options = {};
        std::vector<unsigned int> bitFlags = {};
        bool singleFlagOnly = false; ///< allows only a single bit to be set
    };

    //  ------------------------------ GUI ELEMENT LIST ------------------------------- //
  public:
    class GuiElementList {
        friend class GuiInterface;

      protected:
        std::vector<BaseGuiEntry *> m_entries{};
        gui_id m_id_counter = 1;

        // helper functions for adding gui elements that are used in the macros. decimals is unused for non-decimal elements.
        template <class T>
        gui_id add(T *v, const std::string &name, GuiType type, int decimals = 3);
        template <class T>
        gui_id add(T *v, const std::string &name, GuiType type, T min, T max, T step, int decimals = 3);

        // --- getter setter ---
        template <class T>
        gui_id add(std::function<void(T)> setter, std::function<T()> getter, const std::string &name, GuiType type, int decimals = 3);
        template <class T>
        gui_id add(std::function<void(T)> setter, std::function<T()> getter, const std::string &name, GuiType type, T min, T max, T step, int decimals = 3);

      public:
        virtual ~GuiElementList() = default;
        virtual bool remove(gui_id id);
        virtual bool remove(std::string name) { throw std::runtime_error("not implemented yet!"); }
        virtual void clear();

        // ------- Gui entries --------
        // base types
        PROPERTY_REF(addBool, bool, GuiBool)
        PROPERTY_REF(addInt, int, GuiInt)
        PROPERTY_REF_MINMAX(addInt, int, GuiInt)
        FLOAT_PROPERTY_REF(addFloat, float, GuiFloat)
        PROPERTY_REF(addString, std::string, GuiString)

        // glm types
        PROPERTY_REF(addIVec2, glm::ivec2, GuiIVec2)
        PROPERTY_REF_MINMAX(addIVec2, glm::ivec2, GuiIVec2)
        // TODO: GUI range properties receive min/max args in 2D, but use them only in 1D (red channel)
        PROPERTY_REF(addIntRange, glm::ivec2, GuiIntRange)
        PROPERTY_REF_MINMAX(addIntRange, glm::ivec2, GuiIntRange)
        PROPERTY_REF(addIVec3, glm::ivec3, GuiIVec3)
        PROPERTY_REF_MINMAX(addIVec3, glm::ivec3, GuiIVec3)
        PROPERTY_REF(addIVec4, glm::ivec4, GuiIVec4)
        PROPERTY_REF_MINMAX(addIVec4, glm::ivec4, GuiIVec4)
        FLOAT_PROPERTY_REF(addVec2, glm::vec2, GuiVec2)
        // TODO: GUI range properties receive min/max args in 2D, but use them only in 1D (red channel)
        FLOAT_PROPERTY_REF(addFloatRange, glm::vec2, GuiFloatRange)
        FLOAT_PROPERTY_REF(addVec3, glm::vec3, GuiVec3)
        FLOAT_PROPERTY_REF(addVec4, glm::vec4, GuiVec4)
        PROPERTY_REF(addColor, glm::vec4, GuiColor)

        // vvv types
        virtual gui_id addTF1D(VectorTransferFunction *tf, std::vector<float> *histogram = nullptr, float *histMin = nullptr, float *histMax = nullptr, std::function<void()> onChanged = nullptr);
        virtual gui_id addTFSegmentedVolume(std::vector<SegmentedVolumeMaterial> *materials, const std::vector<std::string> &attributeNames, const std::vector<glm::vec2> &attributeMinMax, std::function<void(int)> onChanged = nullptr, const std::string &name = "");

        // special types and grouping
        virtual gui_id addDirection(glm::vec3 *v, const Camera *camera, const std::string &name = "");
        virtual gui_id addDirection(std::function<void(glm::vec3)> setter, std::function<glm::vec3()> getter, const Camera *camera, const std::string &name = "");
        /// Add acombo box GUI element for selecting on e of options.size() entries.
        /// @param onChanged called if an element is selected. the bool parameter is false, if this happens during a vcfg file import
        virtual gui_id addCombo(int *selection, const std::vector<std::string> &options, std::function<void(int, bool)> onChanged = nullptr, const std::string &name = "");
        virtual gui_id addBitFlags(unsigned int *bitfield, const std::vector<std::string> &options, const std::vector<unsigned int> &bitFlags, bool singleFlagOnly, const std::string &name = "");
        virtual gui_id addAction(void (*callback)(), const std::string &name);
        virtual gui_id addAction(std::function<void()> callback, const std::string &name);
        virtual gui_id addCustomCode(std::function<void()> callback, const std::string &name);
        virtual gui_id addLabel(const std::string& name);
        virtual gui_id addDynamicText(std::string *text, const std::string& name = "");
        virtual gui_id addProgress(std::function<float()> getter, const std::string &name = "") { return add<float>(nullptr, getter, name, GuiProgress); }
        virtual gui_id addSeparator();

        virtual bool writeParameters(std::ostream &out) const;

        /// If this GuiElementList has a parameter with name parameter_label, reads the values for the parameter from in
        /// and returns true.
        /// @returns true if the parameter was consumed
        virtual bool readParameter(const std::string &parameter_label, std::istream &parameter_stream);
    };

  protected:
    //  ------------------------------ GUI WINDOW CLASS ------------------------------- //
    /// A GuiWindow contains multiple columns which in turn are lists of Gui elements
    class GuiWindow {
      protected:
        std::string m_name;
        std::vector<GuiElementList> m_columns;
        bool m_visible;

        constexpr static unsigned int MAX_GUI_COLUMN_COUNT = 8; // we only allow this many columns per window

      public:
        explicit GuiWindow(std::string name) : m_name(std::move(name)), m_columns{GuiElementList()}, m_visible(true) {}
        GuiWindow() : m_name(), m_columns{GuiElementList()}, m_visible(true) {}
        virtual ~GuiWindow() { GuiWindow::clear(); }

        void setVisible(const bool visible) { m_visible = visible; }
        bool isVisible() const { return m_visible; }

        const std::string &getName() const { return m_name; }

        virtual GuiElementList *getColumn(unsigned int i) {
            assert(i < MAX_GUI_COLUMN_COUNT);

            if (i >= m_columns.size()) {
                m_columns.resize(i + 1, GuiElementList());
            }
            return &m_columns.at(i);
        }

        virtual const std::vector<GuiElementList> &getColumns() const { return m_columns; }

        virtual void clear() {
            for (auto c : m_columns)
                c.clear();
            m_columns.resize(1, GuiElementList());
        }

        bool removeColumn(unsigned int i) {
            if (i < m_columns.size()) {
                m_columns[i].clear();
                m_columns.erase(m_columns.begin() + i);
                return true;
            }
            return false;
        }

        virtual bool writeParameters(std::ostream &out) const {
            out << "[" << m_name << "]" << std::endl;
            for (const auto &c : m_columns) {
                if (!c.writeParameters(out))
                    return false;
                out << std::endl;
            }
            return true;
        }

        /// Reads all parameters known to this element list from the file, skipping empty lines and unknown parameters.
        /// A warning is printed if an unknown parameter is skipped. Once a new window name as [name] in braces
        /// is encountered, this name is written (without braces) to next_window_name and the function returns.
        /// @returns false if a parameter known to this element list was encountered but could not be read correctly
        virtual bool readParameters(std::istream &in, std::string &next_window_name) {
            std::string line;
            while (!in.eof() && std::getline(in, line)) {

                // skip any empty lines
                if (std::ranges::all_of(line, [](const unsigned char &c) { return std::isspace(c); })) {
                    continue;
                }
                // if this is the next window name which is a single line containing the name between braces as [name],
                // return and let the next window continue
                if (line.starts_with('[') && line.ends_with(']')) {
                    next_window_name = line.substr(1, line.size() - 2);
                    return true;
                }

                // one line contains data for one parameter. a single parameter is read from:
                // [sanitized_parameter_label]: [parameter_values]
                std::istringstream parameter_stream(line);
                std::string parameter_label;
                parameter_stream >> parameter_label;

                bool consumed = false;
                // the first column (GuiElementList) that has this parameter consumes it. A window must not contain
                // a parameter with the same name.
                for (auto &c : m_columns) {
                    if (!consumed && c.readParameter(parameter_label, parameter_stream))
                        consumed = true;
                }
                if (!consumed) {
                    parameter_label.pop_back();
                    Logger(Warn) << "Read unknown parameter " << parameter_label << " in window " << m_name;
                }
                if ((!parameter_stream.eof() && parameter_stream.fail()) || (!in.eof() && in.fail())) {
                    Logger(Warn) << "Error reading parameter " << parameter_label << " in window " << m_name;
                    return false;
                }
            }
            return true;
        }
    };

    // --------------------------- GUI WINDOW CLASS END ------------------------------------- //

    std::unordered_map<std::string, GuiWindow> m_windows;

    std::vector<std::pair<std::string, std::string>> m_docking_layout = {};

    // accessor function to the gui entries
    static std::vector<BaseGuiEntry *> &getEntriesForColumn(GuiElementList &l) { return l.m_entries; }

  public:
    GuiElementList *get(const std::string windowName, unsigned int column = 0) {
        // use of non-existing window name inserts a new window object.
        if (!m_windows.contains(windowName))
            m_windows.insert({{windowName, GuiWindow(windowName)}});
        // use of non-existing columns resizes window to have as many columns as the column id.
        return m_windows[windowName].getColumn(column);
    }

    GuiWindow *getWindow(std::string windowName) {
        // use of non-existing window name inserts a new window object.
        if (!m_windows.contains(windowName))
            m_windows.insert({{windowName, GuiWindow(windowName)}});
        return &(m_windows[windowName]);
    }

    void removeWindow(std::string windowName) {
        m_windows.erase(windowName);
    }

    void removeAllWindows() {
        m_windows.clear();
    }

    bool removeColumn(std::string windowName, int column) {
        if (!m_windows.contains(windowName)) {
            Logger(Warn) << "removeColumn: GUI Window " << windowName << " does not exist";
            return false;
        }
        return m_windows[windowName].removeColumn(column);
    }

    bool writeParameters(std::ostream &out) const {
        for (const auto &w : m_windows) {
            if (!w.second.writeParameters(out))
                return false;
        }
        return true;
    }

    bool readParameters(std::istream &in, Camera *camera = nullptr) {
        // std::getline(in, line);

        // // read first window name (skip empty lines until section key)
        // while((line.empty() || line.front() != '[' || line.back() != ']') && in.good()) {
        //     if (!line.empty())
        //         Logger(Warn) << "Parameter import skipping non-key line " << line;
        //     std::getline(in, line);
        // }
        // if (line.front() != '[' || line.back() != ']') {
        //     Logger(Warn) << "Parameter import error: Could not find section starting with window name [<NAME>]";
        //     return false;
        // }

        // name in braces [name] specifies GUI window / group
        std::string window_name;
        while (!(in.rdstate() & std::istream::eofbit)) {

            // find next window name (if it was not already set by a parameter reader)
            if (window_name.empty()) {
                std::string line;
                while ((line.empty() || line.front() != '[' || line.back() != ']') && in.good()) {
                    // skip any empty lines
                    if (!std::ranges::all_of(line, [](const unsigned char &c) { return std::isspace(c); })) {
                        Logger(Warn) << "Parameter import skipping non-key line " << line;
                    }
                    std::getline(in, line);
                }
                if (line.starts_with('[') && line.ends_with(']')) {
                    window_name = line.substr(1, line.size() - 2);
                } else {
                    break;
                }
            }

            // TODO: camera should be registered in one of the windows?
            if (window_name == "Camera") {
                if (!camera) {
                    Logger(Warn) << "Parameter import error: Reading [Camera] but camera is not set!";
                    return false;
                }
                window_name = "";
                camera->readFrom(in, true);
            } else {
                bool found = false;
                for (auto &w : m_windows) {
                    if (w.second.getName() == window_name) {
                        // read parameters in this window until a new window key occurs
                        window_name = "";
                        if (!w.second.readParameters(in, window_name))
                            return false;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    Logger(Warn) << "Parameter import read unknown window " << window_name << ".";
                    window_name = "";
                }
            }

            if (!in.eof() && in.fail()) {
                Logger(Warn) << "Parameter import error after reading parameters for [" << window_name << "].";
                return false;
            }
        }

        return true;
    }

    /// Pass a list of std::pair objects where each pair contains the: 1. window to dock, and 2. docking location.\n
    /// A docking location can either be a name of another window or one of the placeholders "l", "r", "u", "d" for
    /// left, right, up, or down locations of the central window. Docking multiple windows to the same central window
    /// location results in them being placed next to/below each other at this location.
    void setDockingLayout(std::vector<std::pair<std::string, std::string>> docking_layout) {
        m_docking_layout = std::move(docking_layout);
    }

    /// Updates all GUI elements based on the values read from value pointers or getters if the properties where added with getter/setter
    /// function pointers and a getter function pointer was specified.
    virtual void updateGui() = 0;
};

// Implementations for templated add functions

template <class T>
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

template <class T>
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
template <class T>
gui_id GuiInterface::GuiElementList::add(std::function<void(T)> setter, std::function<T()> getter, const std::string &name, GuiType type, int decimals) {
    auto entry = new GuiEntry<T>();
    entry->id = m_id_counter++;
    entry->type = type;
    entry->getter = std::move(getter);
    entry->setter = std::move(setter);
    entry->label = name;
    entry->floatDecimals = decimals;

    m_entries.push_back(entry);
    return entry->id;
}

template <class T>
gui_id GuiInterface::GuiElementList::add(std::function<void(T)> setter, std::function<T()> getter, const std::string &name, GuiType type, T min, T max, T step, int decimals) {
    auto entry = new GuiEntry<T>();
    entry->id = m_id_counter++;
    entry->type = type;
    entry->getter = std::move(getter);
    entry->setter = std::move(setter);
    entry->label = name;
    entry->min = min;
    entry->max = max;
    entry->step = step;
    entry->floatDecimals = decimals;

    m_entries.push_back(entry);
    return entry->id;
}

} // namespace vvv
