#pragma once

#include "vvv/core/WithGpuContext.hpp"
#include "vvv/core/GuiInterface.hpp"

class GuiImgui : public vvv::GuiInterface, public vvv::WithGpuContext {

public:
    explicit GuiImgui(vvv::GpuContextPtr ctx, float scale = 1.f) : m_gui_scaling(scale), m_firstCall(true), WithGpuContext(ctx) {};
    ~GuiImgui() = default;

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