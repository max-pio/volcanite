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

#include "vvv/core/GuiInterface.hpp"
#include "vvv/core/WithGpuContext.hpp"

class GuiImgui : public vvv::GuiInterface, public vvv::WithGpuContext {

  public:
    explicit GuiImgui(vvv::GpuContextPtr ctx, float scale = 1.f) : m_gui_scaling(scale), m_firstCall(true), WithGpuContext(ctx) {};
    ~GuiImgui() override = default;

    void updateGui() override;

    // called during render loop inside (Glfw) Window:
    void renderGui();

    void setGuiScaling(float guiScaling);
    float getGuiScaling() const { return m_gui_scaling; }

  private:
    const float m_defaultFontSize = 14.f;
    float m_gui_scaling = 1.f;
    float m_current_gui_scaling = 1.f;
    bool m_firstCall;
};