#pragma once

#include <string>

namespace vvv {

class GuiInterface;

/**
 * TODO use this in the future to replace the other addToGui methods, but first decide how to handle Grouping, Windows and GuiElementLists
 * Defines a single method addToGui which will be called with the target GuiInterface and window name to add the classes gui elements to.
 */
class WithGui {
public:
    virtual void addToGui(GuiInterface *gui, const std::string& windowName) = 0;
};

} // namespace vvv
