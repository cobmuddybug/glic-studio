#include "explore_ui.hpp"

#include "keybindings.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace studio {

namespace {

// Fits w x h into the box, centred.
void fitRect(ImVec2 b0, ImVec2 b1, float w, float h, ImVec2& p0, ImVec2& p1) {
    const float bw = b1.x - b0.x, bh = b1.y - b0.y;
    const float k = std::min(bw / std::max(w, 1.0f), bh / std::max(h, 1.0f));
    const float dw = w * k, dh = h * k;
    p0 = ImVec2(std::floor(b0.x + (bw - dw) / 2), std::floor(b0.y + (bh - dh) / 2));
    p1 = ImVec2(p0.x + dw, p0.y + dh);
}

void centredText(ImDrawList* dl, ImVec2 b0, ImVec2 b1, const char* text, ImU32 col) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2(b0.x + (b1.x - b0.x - ts.x) / 2, b0.y + (b1.y - b0.y - ts.y) / 2), col, text);
}

} // namespace

void drawMutateGrid(App& app) {
    MutateGrid& g = app.mutateGrid();

    ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x + 4, ImGui::GetCursorPosY() + 4));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Mutate");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    ImGui::SliderFloat("##strength", &g.strength, 0.02f, 1.0f, "strength %.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("How far each mutation strays from the current settings");
    if (ImGui::IsItemDeactivatedAfterEdit()) app.regenerateMutations();
    ImGui::SameLine();
    if (ImGui::Button("New mutations")) app.regenerateMutations();
    ImGui::SameLine();
    char close[48];
    std::snprintf(close, sizeof close, "Close (%s)", shortcutLabel(Action::MutateGrid));
    if (ImGui::Button(close) || (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        app.openMutateGrid(false);
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Click one to adopt it");

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 30 || avail.y < 30) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(24, 24, 24, 255));

    const float gap = 6;
    const float cw = (avail.x - gap * 4) / 3, ch = (avail.y - gap * 4) / 3;
    for (int i = 0; i < MutateGrid::kCells; i++) {
        const auto idx = static_cast<size_t>(i);
        const ImVec2 b0(origin.x + gap + (i % 3) * (cw + gap), origin.y + gap + (i / 3) * (ch + gap));
        const ImVec2 b1(b0.x + cw, b0.y + ch);
        ImGui::SetCursorScreenPos(b0);
        ImGui::PushID(i);
        const bool clicked = ImGui::InvisibleButton("##cell", ImVec2(cw, ch));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        ImVec2 p0, p1;
        fitRect(b0, b1, static_cast<float>(g.cellWidth), static_cast<float>(g.cellHeight), p0, p1);
        dl->AddRectFilled(p0, p1, IM_COL32(40, 40, 40, 255));
        const Texture& t = g.textures[idx];
        if (g.state[idx] == 1 && t.valid()) {
            const ImVec2 uv(static_cast<float>(g.cellWidth) / t.width(), static_cast<float>(g.cellHeight) / t.height());
            dl->AddImage(ImTextureRef(t.imguiId()), p0, p1, ImVec2(0, 0), uv);
        } else if (g.state[idx] == 2) {
            centredText(dl, p0, p1, "failed", IM_COL32(255, 120, 110, 255));
        } else {
            centredText(dl, p0, p1, "rendering...", ImGui::GetColorU32(ImGuiCol_TextDisabled));
        }
        if (hovered) dl->AddRect(ImVec2(p0.x - 2, p0.y - 2), ImVec2(p1.x + 2, p1.y + 2), ImGui::GetColorU32(ImGuiCol_ButtonActive), 0.0f, 0, 2.0f);
        char num[4];
        std::snprintf(num, sizeof num, "%d", i + 1);
        dl->AddText(ImVec2(p0.x + 5, p0.y + 3), IM_COL32(255, 255, 255, 200), num);

        if (hovered && g.state[idx] == 2) ImGui::SetTooltip("%s", g.errors[idx].c_str());
        if (clicked && g.state[idx] == 1) app.adoptMutation(i);
    }
}

void drawHistoryStrip(App& app) {
    const History& h = app.history();
    if (h.empty()) {
        ImGui::TextDisabled("History appears here after the first full render. %s undoes, %s redoes.",
                            shortcutLabel(Action::Undo), shortcutLabel(Action::Redo));
        return;
    }
    const std::vector<int> line = h.mainLine();
    const float thumbH = std::max(24.0f, ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ScrollbarSize - 4);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    static int lastCurrent = -1;
    bool afterCurrent = false;
    int branchMenuNode = -1;

    for (size_t k = 0; k < line.size(); k++) {
        const int n = line[k];
        const History::Node& node = h.node(n);
        const glic::Image& th = node.thumbnail;
        const float aspect = th.width > 0 ? static_cast<float>(th.width) / th.height : 1.5f;
        const ImVec2 size(thumbH * aspect, thumbH);
        if (k) ImGui::SameLine(0, 4);
        ImGui::PushID(n);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##node", size)) app.jumpHistory(n);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && node.children.size() > 1) branchMenuNode = n;
        ImGui::PopID();
        const ImVec2 p1(p0.x + size.x, p0.y + size.y);

        const ImU32 tint = afterCurrent ? IM_COL32(255, 255, 255, 110) : IM_COL32(255, 255, 255, 255);
        if (const Texture* t = app.historyTexture(n)) dl->AddImage(ImTextureRef(t->imguiId()), p0, p1, ImVec2(0, 0), ImVec2(1, 1), tint);
        else dl->AddRectFilled(p0, p1, IM_COL32(45, 45, 45, 255));

        const bool current = n == h.current();
        if (current) {
            dl->AddRect(ImVec2(p0.x - 1, p0.y - 1), ImVec2(p1.x + 1, p1.y + 1), ImGui::GetColorU32(ImGuiCol_CheckMark), 0.0f, 0, 2.0f);
            if (lastCurrent != n) {
                ImGui::SetScrollHereX(0.5f);
                lastCurrent = n;
            }
        } else if (hovered) {
            dl->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_ButtonHovered));
        }
        if (node.children.size() > 1) {
            // Branch count badge.
            char badge[24];
            std::snprintf(badge, sizeof badge, "%zu", node.children.size());
            const ImVec2 ts = ImGui::CalcTextSize(badge);
            const ImVec2 b0(p1.x - ts.x - 6, p0.y + 2);
            dl->AddRectFilled(b0, ImVec2(p1.x - 2, b0.y + ts.y + 2), IM_COL32(230, 160, 50, 230), 3);
            dl->AddText(ImVec2(b0.x + 2, b0.y + 1), IM_COL32(20, 20, 20, 255), badge);
        }
        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::Text("%s%s", node.label.c_str(), current ? " (current)" : "");
            if (node.children.size() > 1) ImGui::TextDisabled("%zu branches: right-click to switch", node.children.size());
            ImGui::EndTooltip();
        }
        afterCurrent |= current;
    }

    if (branchMenuNode >= 0) {
        ImGui::OpenPopup("##branches");
        ImGui::GetStateStorage()->SetInt(ImGui::GetID("##branchnode"), branchMenuNode);
    }
    if (ImGui::BeginPopup("##branches")) {
        const int n = ImGui::GetStateStorage()->GetInt(ImGui::GetID("##branchnode"), -1);
        if (n >= 0 && n < h.size()) {
            ImGui::TextDisabled("Branches from here");
            const float bh = ImGui::GetFontSize() * 4;
            for (int c : h.node(n).children) {
                ImGui::PushID(c);
                const History::Node& child = h.node(c);
                const float aspect = child.thumbnail.width > 0 ? static_cast<float>(child.thumbnail.width) / child.thumbnail.height : 1.5f;
                if (const Texture* t = app.historyTexture(c)) {
                    if (ImGui::ImageButton("##b", ImTextureRef(t->imguiId()), ImVec2(bh * aspect, bh))) {
                        app.jumpHistory(c);
                        ImGui::CloseCurrentPopup();
                    }
                } else if (ImGui::Button(child.label.c_str(), ImVec2(bh * aspect, bh))) {
                    app.jumpHistory(c);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(child.label.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndPopup();
    }
}

} // namespace studio
