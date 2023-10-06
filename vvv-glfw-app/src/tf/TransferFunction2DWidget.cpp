#include <vvvwindow/tf/TransferFunction2DWidget.hpp>

#include <vvv/volren/tf/builtin.hpp>
#include <vvv/passes/PassCompute.hpp>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_vulkan.h>

namespace vvv {

void renderGuiTF2D(GuiInterface::GuiTF2DEntry &entry, GpuContextPtr ctx) {
    if (!entry.widgetData.has_value())
        entry.widgetData.emplace<GuiTF2DData>(entry);

    std::any_cast<GuiTF2DData&>(entry.widgetData).renderGui(ctx);
}

// adapted from https://www.shadertoy.com/view/WdSGRd (comment by iq)
inline float sdPoly(const std::vector<glm::vec2>& v, glm::vec2 p) {
    float d = dot(p - v[0], p - v[0]);
    float s = 1.0f;
    for (int i = 0, j = v.size() - 1; i < v.size(); j = i, i++) {
        // distance
        glm::vec2 e = v[j] - v[i];
        glm::vec2 w = p - v[i];
        glm::vec2 b = w - e * glm::clamp(glm::dot(w, e) / glm::dot(e, e), 0.0f, 1.0f);
        d = glm::min(d, dot(b, b));

        // winding number from http://geomalgorithms.com/a03-_inclusion.html
        auto cond = std::array<bool,3>{p.y >= v[i].y, p.y < v[j].y, e.x * w.y > e.y * w.x};
        if (cond[0] && cond[1] && cond[2] || !cond[0] && !cond[1] && !cond[2])
            s *= -1.0;
    }

    return s * glm::sqrt(d);
}

inline glm::vec2 fromPixelSpace(glm::vec2 canvas_p0, glm::vec2 canvas_sz, glm::vec2 v) {
    float x = (v.x - canvas_p0.x - 5) / (canvas_sz.x - 10);
    float y = 1 - (v.y - canvas_p0.y - 5) / (canvas_sz.y - 10);
    return glm::clamp(glm::vec2{x, y}, 0.0f, 1.0f);
}
inline glm::vec2 toPixelSpace(glm::vec2 canvas_p0, glm::vec2 canvas_sz, glm::vec2 v) {
    float x = canvas_p0.x + 5 + v.x * (canvas_sz.x - 10);
    float y = canvas_p0.y + 5 + (1 - v.y) * (canvas_sz.y - 10);
    return {x, y};
}
inline glm::vec2 fromPixelSpaceVec(glm::vec2 canvas_p0, glm::vec2 canvas_sz, glm::vec2 v) {
    return glm::vec2{v.x / (canvas_sz.x - 10), -v.y / (canvas_sz.y - 10)};
}

GuiTF2DData::GuiTF2DData(GuiInterface::GuiTF2DEntry &entry) : entry(entry), tf(*entry.value) {
    const auto &desc = tf.texture().descriptor;

    // Note: This DescriptorSet will never be freed! It is currently (2022-05-04) impossible to free this descriptor set without modifying  imgui_impl_vulkan.cpp
    imguiResultTexture = ImGui_ImplVulkan_AddTexture(desc.sampler, desc.imageView, static_cast<VkImageLayout>(desc.imageLayout));
}

GuiTF2DData::~GuiTF2DData() {
    histogramRGBATexture = nullptr;

    if (histogramCompute)
        histogramCompute->freeResources();
    histogramCompute = nullptr;
}

void GuiTF2DData::renderGui(GpuContextPtr ctx) {
    bool modified = false;

    if (renderButtons(ctx))
        modified = true;

    // use ImGUI functions to get available space to paint the TF to
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz = ImGui::GetContentRegionAvail();
    if (canvas_sz.x < 50.0f)
        canvas_sz.x = 50.0f;
    if (canvas_sz.x > 500.0f)
        canvas_sz.x = 500.0f;
    canvas_sz.y = canvas_sz.x;
    ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

    // This will catch our interactions
    ImGui::InvisibleButton("canvas", canvas_sz, ImGuiButtonFlags_MouseButtonLeft);

    if (entry.histogramTexture) {
        if (!imguiHistogramTexture || (entry.histogramChanged && *entry.histogramChanged)) {
            updateHistogramTexture(ctx);
        }
    }

    renderCanvas(canvas_p0, canvas_sz);

    if (handleInput(canvas_p0, canvas_sz))
        modified = true;

    if (modified && entry.onChanged)
        entry.onChanged();
}

void GuiTF2DData::updateHistogramTexture(GpuContextPtr ctx) {
    if (!histogramCompute) {
        histogramCompute = std::make_shared<SinglePassCompute>(
            SinglePassComputeSettings{.ctx = ctx, .label = "gui_tf2d_histogram"},
            SimpleGlslShaderRequest{.filename = "gui/tf2d_histogram.comp", .label = "gui_tf2d_histogram.shader"});
        histogramCompute->allocateResources();

        histogramCompute->setImageSampler("SAMPLER_in", *entry.histogramTexture);

        histogramRGBATexture = histogramCompute->reflectTexture("IMAGE_out", {
                                                                                 .width = entry.histogramTexture->width, .height = entry.histogramTexture->height,
                                                                                 .format = vk::Format::eR8G8B8A8Unorm,
                                                                                 .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled});
        histogramRGBATexture->initResources();
        histogramRGBATexture->setName("gui_tf2d_histogram.image");
        histogramCompute->setStorageImage("IMAGE_out", *histogramRGBATexture, vk::ImageLayout::eGeneral);

        auto await = histogramRGBATexture->setImageLayout(vk::ImageLayout::eGeneral);
        ctx->sync->hostWaitOnDevice({await});
    }

    if (!imguiHistogramTexture) {
        auto &desc = histogramRGBATexture->descriptor;

        // Note: This DescriptorSet will never be freed! It is currently (2022-05-04) impossible to free this descriptor set without modifying  imgui_impl_vulkan.cpp
        imguiHistogramTexture = ImGui_ImplVulkan_AddTexture(desc.sampler, desc.imageView, static_cast<VkImageLayout>(desc.imageLayout));
    }

    histogramCompute->setGlobalInvocationSize(entry.histogramTexture->width, entry.histogramTexture->height);
    auto await = histogramCompute->execute();
    ctx->sync->hostWaitOnDevice({await});
}

bool GuiTF2DData::renderButtons(GpuContextPtr ctx) {
    bool modified = false;
    ImDrawList *draw_list = ImGui::GetWindowDrawList();

    const auto activeButtonCol = ImGui::GetColorU32(ImGuiCol_ButtonActive);
    const auto normalButtonCol = ImGui::GetColorU32(ImGuiCol_Button);
    const auto textCol = ImGui::GetColorU32(ImGuiCol_Text);

    // Draw Edit Button (cursor symbol)
    ImVec2 button_start = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("tool_edit", {20, 20}, ImGuiButtonFlags_MouseButtonLeft)) {
        tool = GuiTF2DData::Tool::EditPoints;
    }
    draw_list->AddRectFilled(button_start, {button_start.x + 20, button_start.y + 20}, tool == GuiTF2DData::Tool::EditPoints ? activeButtonCol : normalButtonCol);
    auto drawTri = [draw_list,textCol](ImVec2 pos, float x1, float y1, float x2, float y2, float x3, float y3) {
        draw_list->AddTriangleFilled({pos.x + x1, pos.y + y1}, {pos.x + x2, pos.y + y2}, {pos.x + x3, pos.y + y3}, textCol);
    };
    drawTri(button_start, 8, 16, 10, 12, 4, 4);
    drawTri(button_start, 10, 12, 14, 10, 4, 4);

    ImGui::SameLine();

    // Draw Delete Button (x symbol)
    button_start = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("tool_delete", {20, 20}, ImGuiButtonFlags_MouseButtonLeft)) {
        tool = GuiTF2DData::Tool::Delete;
    }
    draw_list->AddRectFilled(button_start, {button_start.x + 20, button_start.y + 20}, tool == GuiTF2DData::Tool::Delete ? activeButtonCol : normalButtonCol);
    draw_list->AddText({button_start.x + 5, button_start.y + 3}, textCol, "X");

    ImGui::SameLine();

    // Draw Polygon Button (triangle symbol)
    button_start = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("tool_poly", {20, 20}, ImGuiButtonFlags_MouseButtonLeft)) {
        tool = GuiTF2DData::Tool::AddPolygon;
    }
    draw_list->AddRectFilled(button_start, {button_start.x + 20, button_start.y + 20}, tool == GuiTF2DData::Tool::AddPolygon ? activeButtonCol : normalButtonCol);
    draw_list->AddTriangle({button_start.x + 4, button_start.y + 16}, {button_start.x + 16, button_start.y + 16}, {button_start.x + 10, button_start.y + 4}, textCol);

    ImGui::SameLine();

    // Draw Rectangle Button (square symbol)
    button_start = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("tool_rect", {20, 20}, ImGuiButtonFlags_MouseButtonLeft)) {
        tool = GuiTF2DData::Tool::AddRect;
    }
    draw_list->AddRectFilled(button_start, {button_start.x + 20, button_start.y + 20}, tool == GuiTF2DData::Tool::AddRect ? activeButtonCol : normalButtonCol);
    draw_list->AddRect({button_start.x + 4, button_start.y + 4}, {button_start.x + 16, button_start.y + 16}, textCol);

    ImGui::SameLine(0, 20);
    ImGui::Checkbox("Settings", &showSettings);

    if (showSettings) {

        // Draw Colormap combo box
        const char *currColormapName = "colormap";
        for (int i = 0; auto& [name, _] : vvv::colormaps::colormaps) {
            if (i == selectedColorMap.value_or(-1))
                currColormapName = name.c_str();
            i++;
        }
        if (ImGui::BeginCombo("Colormap", currColormapName)) {
            for (int n = 0; auto& [name, value] : vvv::colormaps::colormaps) {
                const bool is_selected = selectedColorMap.value_or(-1) == n;
                if (ImGui::Selectable(name.c_str(), is_selected)) {
                    selectedColorMap = n;

                    VectorTransferFunction vecTF(value, {1, 1});
                    auto tf1D = vecTF.rasterize(ctx, entry.value->resolution());
                    auto [await, stagingBuf] = tf1D->upload();
                    ctx->sync->hostWaitOnDevice({await});

                    entry.value->setColormapTF(tf1D);
                    modified = true;
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
                n++;
            }
            ImGui::EndCombo();
        }

        // Draw Colormap Direction combo box
        const std::array<std::string, 3> directionNames = { "horizontal", "vertical", "both" };
        // ImGui::SetNextItemWidth(ImGui::CalcTextSize("horizontal").x + 30);
        if (ImGui::BeginCombo("Colormap Direction", directionNames[entry.value->direction()].c_str())) {
            for (int i = 0; i < directionNames.size(); i++) {
                if (ImGui::Selectable(directionNames[i].c_str(), i == entry.value->direction())) {
                    entry.value->setDirection(static_cast<TransferFunction2D::Direction>(i));
                    modified = true;
                }
            }
            ImGui::EndCombo();
        }

        // Draw Feathering Drag Box
        float feathering = entry.value->feathering();
        ImGui::DragFloat("Feathering", &feathering, 0.001f, -0.1f, 0.1f);
        if (feathering != entry.value->feathering()) {
            entry.value->setFeathering(feathering);
            modified = true;
        }

        ImGui::Spacing();

        // Draw options used per polygon (opacity, custom color)
        if (lastUsedPolygon.has_value()) {
            float opacity = tf.polygonOpacity(lastUsedPolygon.value());
            bool hasCustomColor = tf.polygonHasCustomColor(lastUsedPolygon.value());
            glm::vec3 customColor = tf.polygonCustomColor(lastUsedPolygon.value());

            ImGui::DragFloat("Polygon opacity", &opacity, 0.01f, 0, 1);
            ImGui::Checkbox("Polygon Custom Color", &hasCustomColor);
            if (hasCustomColor)
                ImGui::ColorEdit3("Polygon color", &customColor.x);

            if (opacity != tf.polygonOpacity(lastUsedPolygon.value())) {
                tf.setPolygonOpacity(lastUsedPolygon.value(), opacity);
                modified = true;
            }
            if (hasCustomColor != tf.polygonHasCustomColor(lastUsedPolygon.value())) {
                tf.setPolygonHasCustomColor(lastUsedPolygon.value(), hasCustomColor);
                modified = true;
            }
            if (customColor != tf.polygonCustomColor(lastUsedPolygon.value())) {
                tf.setPolygonCustomColor(lastUsedPolygon.value(), customColor);
                modified = true;
            }
        }
    }

    return modified;
}

void GuiTF2DData::renderCanvas(glm::vec2 canvas_p0, glm::vec2 canvas_sz) {
    ImDrawList *draw_list = ImGui::GetWindowDrawList();

    // Draw background images: resulting transfer function overlayed by histogram
    draw_list->AddImage(imguiResultTexture, toPixelSpace(canvas_p0, canvas_sz, {0, 0}), toPixelSpace(canvas_p0, canvas_sz, {1, 1}));
    if (entry.histogramTexture) {
        auto uvMin = entry.histogramMin ? *entry.histogramMin : glm::vec2{0, 0};
        auto uvMax = entry.histogramMax ? *entry.histogramMax : glm::vec2{1, 1};
        draw_list->AddImage(imguiHistogramTexture, toPixelSpace(canvas_p0, canvas_sz, {0, 0}), toPixelSpace(canvas_p0, canvas_sz, {1, 1}), uvMin, uvMax);
    }

    // Draw histogram polygons as lines and dots
    draw_list->PushClipRect(canvas_p0, canvas_p0 + canvas_sz, true);
    {
        const auto black = ImGui::GetColorU32(IM_COL32(0, 0, 0, 255));
        const auto white = ImGui::GetColorU32(IM_COL32(255, 255, 255, 255));
        for (auto& polygon : tf.polygons()) {
            for (int j = 0; j < polygon.size(); j++) {
                draw_list->AddLine(toPixelSpace(canvas_p0, canvas_sz, polygon[j]), toPixelSpace(canvas_p0, canvas_sz, polygon[(j + 1) % polygon.size()]), black, 3);
                draw_list->AddLine(toPixelSpace(canvas_p0, canvas_sz, polygon[j]), toPixelSpace(canvas_p0, canvas_sz, polygon[(j + 1) % polygon.size()]), white, 1);
            }
        }
        for (auto& polygon : tf.polygons()) {
            for (auto& i : polygon) {
                draw_list->AddCircleFilled(toPixelSpace(canvas_p0, canvas_sz, i), 3, black);
                draw_list->AddCircleFilled(toPixelSpace(canvas_p0, canvas_sz, i), 2, white);
            }
        }
    }
    draw_list->PopClipRect();
}
bool GuiTF2DData::handleInput(glm::vec2 canvas_p0, glm::vec2 canvas_sz) {
    bool modified = false;
    ImGuiIO &io = ImGui::GetIO();
    auto sqr = [](auto v) { return v * v; };
    auto pos = fromPixelSpace(canvas_p0, canvas_sz, io.MousePos);


    // compute hovering state: hovering point, edge, polygon?
    std::optional<std::pair<int, int>> hoveredPoint = {}; // [polygon idx, point idx]
    std::optional<std::pair<int, int>> hoveredEdge = {};  // [polygon idx, idx of point before edge]
    std::optional<int> hoveredPolygon = {};               // [polygon idx]
    if (!isDragging && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsItemActive() && isDragging) {
        const float snapRadius = 8; // px

        float sdf = std::numeric_limits<float>::max();
        for (int i = 0; i < tf.polygons().size(); i++) {
            auto& polygon = tf.polygons()[i];
            for (int j = 0; j < polygon.size(); j++) {
                ImVec2 a = toPixelSpace(canvas_p0, canvas_sz, polygon[j]);
                ImVec2 b = toPixelSpace(canvas_p0, canvas_sz, polygon[(j + 1) % polygon.size()]);
                if (sqr(a.x - io.MousePos.x) + sqr(a.y - io.MousePos.y) < snapRadius * snapRadius) {
                    hoveredPoint = {i, j};
                }

                auto param = ((io.MousePos.x - a.x) * (b.x - a.x) + (io.MousePos.y - a.y) * (b.y - a.y)) / (sqr(b.x - a.x) + sqr(b.y - a.y));
                if (param > 0 && param < 1) {
                    if (sqr(io.MousePos.x - (a.x + param * (b.x - a.x))) + sqr(io.MousePos.y - (a.y + param * (b.y - a.y))) < sqr(snapRadius)) {
                        hoveredEdge = {i, j};
                    }
                }
            }

            float polySdf = sdPoly(polygon, pos);
            if (polySdf < sdf && polySdf < 0) {
                hoveredPolygon = {i};
                sdf = polySdf;
            }
        }
    }

    // update click
    if (!isDragging && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        isDragging = true;
        movingPolygon = {};
        movingPoint = {};

        if (tool == GuiTF2DData::Tool::EditPoints) {
            if (hoveredPoint.has_value()) {
                movingPoint = hoveredPoint.value();
            } else if (hoveredEdge.has_value()) {
                auto [i, j] = hoveredEdge.value();
                auto polygonsCopy = tf.polygons();
                polygonsCopy[i].insert(polygonsCopy[i].begin() + j + 1, pos);
                entry.value->setPolygons(polygonsCopy);
                movingPoint = {i, j + 1};
                modified = true;
            } else if (hoveredPolygon.has_value()) {
                movingPolygon = hoveredPolygon.value();
            }
            currentlyCreatingPolygon = {};
            if (hoveredPolygon.has_value())
                lastUsedPolygon = hoveredPolygon;
        } else if (tool == GuiTF2DData::Tool::Delete) {
            if (hoveredPoint) {
                auto& [i, j] = hoveredPoint.value();
                auto polygonsCopy = tf.polygons();
                polygonsCopy[i].erase(polygonsCopy[i].begin() + j);
                if (polygonsCopy[i].size() < 3)
                    polygonsCopy[i].erase(polygonsCopy[i].begin() + i);
                entry.value->setPolygons(polygonsCopy);
                lastUsedPolygon = i;
                modified = true;
            } else if (hoveredPolygon) {
                auto polygonsCopy = tf.polygons();
                polygonsCopy.erase(polygonsCopy.begin() + hoveredPolygon.value());
                entry.value->setPolygons(polygonsCopy);
                modified = true;
                lastUsedPolygon = {};
            }
            currentlyCreatingPolygon = {};
        } else if (tool == GuiTF2DData::Tool::AddPolygon) {
            auto polygonsCopy = tf.polygons();
            if (currentlyCreatingPolygon.has_value()) {
                auto [hover_i, hover_j] = hoveredPoint.value_or(std::pair{-1, -1});
                if (hover_i == currentlyCreatingPolygon.value() && hover_j == 0) {
                    polygonsCopy[currentlyCreatingPolygon.value()].pop_back();
                    if (polygonsCopy[currentlyCreatingPolygon.value()].size() < 3)
                        polygonsCopy.erase(polygonsCopy.begin() + currentlyCreatingPolygon.value());
                    currentlyCreatingPolygon = {};
                    lastUsedPolygon = hover_i;
                } else {
                    polygonsCopy[currentlyCreatingPolygon.value()].push_back(pos);
                }
            } else {
                polygonsCopy.push_back({pos, pos});
                currentlyCreatingPolygon = polygonsCopy.size() - 1;
            }
            entry.value->setPolygons(polygonsCopy);
            if (currentlyCreatingPolygon.has_value() && polygonsCopy[currentlyCreatingPolygon.value()].size() >= 3)
                modified = true;
        } else if (tool == GuiTF2DData::Tool::AddRect) {
            auto polygonsCopy = tf.polygons();
            polygonsCopy.push_back({pos, pos, pos, pos});
            currentlyCreatingPolygon = polygonsCopy.size() - 1;
            entry.value->setPolygons(polygonsCopy);
            lastUsedPolygon = polygonsCopy.size() - 1;
            modified = true;
        }
    }

    // update drag
    if (ImGui::IsItemActive() && isDragging) {
        if (tool == GuiTF2DData::Tool::EditPoints && movingPoint.has_value()) {
            auto [i, j] = movingPoint.value();
            auto polygonsCopy = tf.polygons();
            auto& p = polygonsCopy[i][j];
            p = p + fromPixelSpaceVec(canvas_p0, canvas_sz, io.MouseDelta);
            entry.value->setPolygons(polygonsCopy);
            modified = true;
        }
        if (tool == GuiTF2DData::Tool::EditPoints && movingPolygon.has_value()) {
            auto polygonsCopy = tf.polygons();
            for (auto& p : polygonsCopy[movingPolygon.value()]) {
                p = p + fromPixelSpaceVec(canvas_p0, canvas_sz, io.MouseDelta);
            }
            entry.value->setPolygons(polygonsCopy);
            modified = true;
        }
        if (tool == GuiTF2DData::Tool::AddRect && currentlyCreatingPolygon.has_value()) {
            auto polygonsCopy = tf.polygons();
            auto &polygon = polygonsCopy[currentlyCreatingPolygon.value()];
            auto o = polygon[0];
            if ((pos.x > o.x) == (pos.y > o.y)) {
                polygon[1] = {o.x, pos.y};
                polygon[2] = pos;
                polygon[3] = {pos.x, o.y};
            } else {
                polygon[1] = {pos.x, o.y};
                polygon[2] = pos;
                polygon[3] = {o.x, pos.y};
            }
            entry.value->setPolygons(polygonsCopy);
            modified = true;
        }
    } else {
        isDragging = false;
        if (tool != GuiTF2DData::Tool::AddPolygon)
            currentlyCreatingPolygon = {};
    }

    // update stuff active at all frames
    if (!currentlyCreatingPolygon.has_value() && ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_Escape))) {
        tool = GuiTF2DData::Tool::EditPoints;
    }
    if (tool == GuiTF2DData::Tool::AddPolygon && currentlyCreatingPolygon.has_value()) {
        auto polygonsCopy = tf.polygons();
        polygonsCopy[currentlyCreatingPolygon.value()].back() = pos;

        if (ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_Escape)) || ImGui::IsMouseDown(ImGuiMouseButton_Right) || pos.x < 0 || pos.y < 0 || pos.x > 1 || pos.y > 1) {
            polygonsCopy[currentlyCreatingPolygon.value()].pop_back();
            if (polygonsCopy[currentlyCreatingPolygon.value()].size() < 3)
                polygonsCopy.erase(polygonsCopy.begin() + currentlyCreatingPolygon.value());
            currentlyCreatingPolygon = {};
            lastUsedPolygon = {};
        }

        entry.value->setPolygons(polygonsCopy);
        modified = true;
    }

    return modified;
}

}