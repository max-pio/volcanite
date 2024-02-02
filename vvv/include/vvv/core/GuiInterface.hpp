#pragma once

#include <vvv/volren/tf/VectorTransferFunction.hpp>
#include <vvv/volren/tf/TransferFunction2D.hpp>
#include <vvv/volren/tf/SegmentedVolumeMaterial.hpp>
#include <vvv/util/Logger.hpp>

#include <glm/glm.hpp>

#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <any>

typedef size_t gui_id;

// ToDo: Refactor GuiInterface to ParameterInterface as parameters can be managed with it without having a visible GUI

/**
 * Steps to add a new data or entry type T:
 * (1) create an entry to GuiType enum
 * (2) create add<T>(...) functions for T in GuiElementList class
 * optional: (3) create a GUI_*_CAST define for casting from the BaseGuiEntry to the right derived entry class
 *
 * optional:
 * (3) update the methods to render GUI in the derived classes of GuiInterface to include the new type
 */

#define PROPERTY_REF(F, T, G)                                                                                                                                                                     \
    virtual gui_id F(T *v, const std::string &name) { return add<T>(v, name, G); }                                                                                                                \
    virtual gui_id F(std::function<void(T)> setter, std::function<T()> getter, const std::string &name = "") { return add<T>(setter, getter, name, G); }

#define PROPERTY_REF_MINMAX(F, T, G)                                                                                                                                                              \
    virtual gui_id F(T *v, const std::string &name, T min, T max, T step) { return add<T>(v, name, G, min, max, step, 0); }                                                                       \
    virtual gui_id F(std::function<void(T)> setter, std::function<T()> getter, const std::string &name, T min, T max, T step) { return add<T>(setter, getter, name, G, min, max, step, 0); }

#define FLOAT_PROPERTY_REF(F, T, G)                                                                                                                                                               \
    virtual gui_id F(T *v, const std::string &name, int decimals = 3) { return add<T>(v, name, G, decimals); }                                                                                    \
    virtual gui_id F(T *v, const std::string &name, T min, T max, T step, int decimals = 3) { return add<T>(v, name, G, min, max, step, decimals); }                                              \
    virtual gui_id F(std::function<void(T)> setter, std::function<T()> getter, const std::string &name = "", int decimals = 3) { return add<T>(setter, getter, name, G, decimals); }

#define GUI_CAST(e, T) (reinterpret_cast<GuiEntry<T> *>(e))
#define GUI_FUNC_CAST(e) (reinterpret_cast<GuiFuncEntry *>(e))

namespace vvv {


/**
 * Connection to a (graphical) parameter interface.
 *
 * Can contain multiple GUI windows that are identified by their name. A window is obtained with the get(windowName) method.
 * If a window with that name doesn't exist yet, it is created.
 *
 * Each window contains a number of columns. Each column is a GuiElementList where elements can be added in a sequential manner.
 *
 * Properties are added in a sequential manner to a window using the add[Type] methods which return an unique id corresponding to this GUI element.
 * Each property can be given a name, that is used as its label in the GUI. Seperators can be used to group GUI elements.
 * The gui changes the property either directly through a pointer to the property or with a function pointer to a setter.
 *
 * The interface automatically enters all added properties to a vector of GuiEntries. Base classes should work hand in hand with the
 * rendering window or window framework to display the list or properties, for example by using an explicit Gui engine. In a minimal case,
 * this requires only some kind of "renderGui()" method in the base class, that iterates over all m_windows and their elements in m_entries and display according gui elements.
 *
 * You can use the addCustomCode method to add an entry than runs a lambda function.
 * This can be used for quick prototyping, for example directly adding ImGUI-Code when using the ImGUI backend.
 */
class GuiInterface {
protected:
    enum GuiType { GuiNoneType, GuiBool, GuiInt, GuiFloat, GuiString, GuiIVec2, GuiIVec3, GuiIVec4, GuiVec2, GuiVec3, GuiDirection, GuiVec4, GuiColor, GuiCombo, GuiAction, GuiLabel, GuiDynamicText, GuiSeparator, GuiTF1D, GuiTF2D, GuiTFSegmentedVolume, GuiCustomCode };

    // ------------------------------- GUI ENTRIES ------------------------------------ //
public:
    struct BaseGuiEntry {
        virtual ~BaseGuiEntry() = default;
        gui_id id{};
        GuiType type{GuiNoneType};
        std::string label{};
    };

    template <class T> struct GuiEntry : BaseGuiEntry {
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

    struct GuiTF2DEntry : BaseGuiEntry {
        TransferFunction2D *value = nullptr;
        Texture *histogramTexture = nullptr;
        glm::vec2 *histogramMin = nullptr;
        glm::vec2 *histogramMax = nullptr;
        bool *histogramChanged = nullptr;
        std::function<void()> onChanged = {};
        std::any widgetData = {};
    };

    struct GuiTFSegmentedVolumeEntry : BaseGuiEntry {
        std::vector<SegmentedVolumeMaterial> *materials;
        std::function<void(int)> onChanged = {};
        std::vector<std::string> attributeNames = {};
        std::vector<glm::vec2> attributeMinMax = {};

        // colormap information (stored here so we can import/export)
        enum ColorMapType { SVTFSolidColor = 0, SVTFDivergent, SVTFPrecomputed, SVTFPNGimport};
        struct ColorMapConfig {
            ColorMapType type = SVTFPrecomputed;
            int precomputedIdx = 0;
            glm::vec3 color[2] = {glm::vec3(00.2298f,0.2987f,0.7537f), glm::vec3(0.7057f,0.01556f,0.1502f)};
        };
        std::vector<ColorMapConfig> colormapConfig = {};
        // additional widget data
        std::any widgetData = {};   // ToDo: any is not nice
    };

    struct GuiComboEntry : BaseGuiEntry {
        int *selection = nullptr;
        std::function<void(int)> onChanged = {};
        std::vector<std::string> options = {};
    };

    //  ------------------------------ GUI ELEMENT LIST ------------------------------- //
public:
    class GuiElementList {
        friend class GuiInterface;
    protected:

        std::vector<BaseGuiEntry *> m_entries{};
        gui_id m_id_counter = 0;

        // helper functions for adding gui elements that are used in the macros. decimals is unused for non-decimal elements.
        template <class T> gui_id add(T *v, const std::string &name, GuiType type, int decimals = 3);
        template <class T> gui_id add(T *v, const std::string &name, GuiType type, T min, T max, T step, int decimals = 3);

        // --- getter setter ---
        template <class T> gui_id add(std::function<void(T)> setter, std::function<T()> getter, const std::string &name, GuiType type, int decimals = 3);
        template <class T> gui_id add(std::function<void(T)> setter, std::function<T()> getter, const std::string &name, GuiType type, T min, T max, T step, int decimals = 3);

    public:
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
        PROPERTY_REF(addIVec3, glm::ivec3, GuiIVec3)
        PROPERTY_REF(addIVec4, glm::ivec4, GuiIVec4)
        FLOAT_PROPERTY_REF(addVec2, glm::vec2, GuiVec2)
        FLOAT_PROPERTY_REF(addVec3, glm::vec3, GuiVec3)
        PROPERTY_REF(addDirection, glm::vec3, GuiDirection)
        FLOAT_PROPERTY_REF(addVec4, glm::vec4, GuiVec4)
        PROPERTY_REF(addColor, glm::vec4, GuiColor)

        // vvv types
        virtual gui_id addTF1D(VectorTransferFunction* tf, std::vector<float> *histogram = nullptr, float *histMin = nullptr, float *histMax = nullptr, std::function<void()> onChanged = nullptr);
        virtual gui_id addTF2D(TransferFunction2D* tf, Texture* histogramTexture, bool* histogramChanged = nullptr, std::function<void()> onChanged = nullptr, glm::vec2* histogramMin = nullptr, glm::vec2* histogramMax = nullptr);
        virtual gui_id addTFSegmentedVolume(std::vector<SegmentedVolumeMaterial>* materials, const std::vector<std::string>& attributeNames, const std::vector<glm::vec2>& attributeMinMax, std::function<void(int)> onChanged = nullptr, const std::string& name = "");

        // special types and grouping
        virtual gui_id addCombo(int* selection, const std::vector<std::string>& options, std::function<void(int)> onChanged = nullptr, const std::string& name = "");
        virtual gui_id addAction(void (*callback)(), std::string name);
        virtual gui_id addAction(std::function<void()> callback, std::string name);
        virtual gui_id addCustomCode(std::function<void()> callback, std::string name);
        virtual gui_id addLabel(std::string name);
        virtual gui_id addDynamicText(std::string* text, std::string name = "");
        virtual gui_id addSeparator();

        virtual bool writeParameters(std::ostream& out) const;
        virtual bool readParameters(std::istream& in);
    };

protected:
    //  ------------------------------ GUI WINDOW CLASS ------------------------------- //
    /**;
     * A GuiWindow contains multiple columns which in turn are lists of Gui elements
     */
    class GuiWindow {
    protected:
        std::string m_name;
        std::vector<GuiElementList> m_columns;
        bool m_visible;

        constexpr static unsigned int MAX_GUI_COLUMN_COUNT = 8;     // we only allow this many columns per window

    public:
        explicit GuiWindow(std::string name) : m_name(std::move(name)),  m_columns{GuiElementList()}, m_visible(true) {}
        GuiWindow() : m_name(),  m_columns{GuiElementList()} {}
        virtual ~GuiWindow() { clear(); }

        void setVisible(bool visible) { m_visible = visible; }
        bool isVisible() const { return m_visible; }

        const std::string& getName() const { return m_name; }

        virtual GuiElementList* getColumn(unsigned int i) {
            assert(i < MAX_GUI_COLUMN_COUNT);

            if(i >= m_columns.size())
            {
                m_columns.resize(i+1, GuiElementList());
            }
            return &m_columns.at(i);
        }

        virtual const std::vector<GuiElementList>& getColumns() const { return m_columns; }

        virtual void clear()
        {
            for(auto c : m_columns)
                c.clear();
            m_columns.resize(1, GuiElementList());
        }

        bool removeColumn(unsigned int i) {
            if(i < m_columns.size()) {
                m_columns[i].clear();
                m_columns.erase(m_columns.begin() + i);
                return true;
            }
            return false;
        }

        virtual bool writeParameters(std::ostream& out) const {
            out << "[" << m_name << "]" << std::endl;
            for(const auto& c: m_columns) {
                if(!c.writeParameters(out))
                    return false;
                out << std::endl;
            }
            return true;
        }

        virtual bool readParameters(std::istream& in) {
            std::string tmp;
            // read window name
            while(tmp.empty() && in.good())
                std::getline(in, tmp); // one empty line
            if(tmp != "[" + m_name + "]") {
                Logger(WARN) << "Reading window parameters for " << tmp << " instead of expected " << m_name << ":";
                return false;
            }
            else {
                Logger(DEBUG) << tmp;
            }
            for(auto& c: m_columns) {
                if(!c.readParameters(in))
                    return false;
            }
            return true;
        }
    };

    // --------------------------- GUI WINDOW CLASS END ------------------------------------- //

    std::unordered_map<std::string, GuiWindow> m_windows;

    // accessor function to the gui entries
    static std::vector<BaseGuiEntry*>& getEntriesForColumn(GuiElementList& l) { return l.m_entries; }

public:
    GuiElementList* get(const std::string windowName, unsigned int column = 0) {
        // use of non-existing window name inserts a new window object.
        if(!m_windows.contains(windowName))
            m_windows.insert({{windowName, GuiWindow(windowName)}});
        // use of non-existing columns resizes window to have as many columns as the column id.
        return m_windows[windowName].getColumn(column);
    }

    GuiWindow* getWindow(std::string windowName) {
        // use of non-existing window name inserts a new window object.
        if(!m_windows.contains(windowName))
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
        if(!m_windows.contains(windowName))
        {
            Logger(WARN) << "removeColumn: GUI Window " << windowName << " does not exist";
            return false;
        }
        return m_windows[windowName].removeColumn(column);
    }

    bool writeParameters(std::ostream& out) {
        for(const auto& w: m_windows) {
            if(!w.second.writeParameters(out))
                return false;
        }
        return true;
    }

    bool readParameters(std::istream& in) {
        for(auto& w: m_windows) {
            if(!w.second.readParameters(in))
                return false;
        }
        return true;
    }

    /**
     * Updates all GUI elements based on the values read from value pointers or getters if the properties where added with getter/setter
     * function pointers and a getter function pointer was specified.
     */
    virtual void updateGui() = 0;

};



// Implementations for templated add functions

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

} // namespace vvv
