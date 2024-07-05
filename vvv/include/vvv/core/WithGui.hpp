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
