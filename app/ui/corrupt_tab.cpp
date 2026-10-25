#include "corrupt_tab.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace studio {

namespace {

constexpr float kMaxRate = 0.5f;

bool rateSlider(const char* id, double& rate) {
    float r = static_cast<float>(rate);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (!ImGui::SliderFloat(id, &r, 0.0f, kMaxRate, r < 0.001f ? "%.5f" : "%.4f",
                            ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
        return false;
    rate = r;
    return true;
}

struct Section {
    size_t from, to;
    ImU32 color;
    const char* name;
    int channel;
};

// The payload's sections, as GLIC's reader finds them in the clean file.
std::vector<Section> sections(const glic::GlicLayout& l, size_t fileSize) {
    std::vector<Section> out;
    const ImU32 seg[2] = {IM_COL32(52, 84, 140, 255), IM_COL32(64, 100, 160, 255)};
    const ImU32 pred[2] = {IM_COL32(104, 64, 130, 255), IM_COL32(122, 78, 150, 255)};
    const ImU32 data[2] = {IM_COL32(40, 112, 104, 255), IM_COL32(50, 132, 122, 255)};
    auto add = [&](const glic::GlicLayout::Section& s, ImU32 c, const char* n, int ch) {
        const size_t to = std::min(s.offset + s.size, fileSize);
        if (s.offset < to) out.push_back({s.offset, to, c, n, ch});
    };
    for (int ch = 0; ch < 3; ch++) add(l.segmentation[ch], seg[ch % 2], "segmentation", ch);
    for (int ch = 0; ch < 3; ch++) add(l.prediction[ch], pred[ch % 2], "prediction data", ch);
    for (int ch = 0; ch < 3; ch++) add(l.data[ch], data[ch % 2], "image data", ch);
    return out;
}

// The byte map (design §M5): the payload left to right, its sections, where
// bytes were changed, and the regions. Drag to add a region; right-click one
// to remove it.
bool byteMap(App& app, glic::CorruptionSettings& c) {
    const ShownRender& shown = app.shown();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFrameHeight() * 2.4f;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + w, p0.y + h);
    ImGui::InvisibleButton("##bytemap", ImVec2(w, h), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(30, 30, 30, 255));

    const glic::GlicLayout* layout = app.shownLayout();
    const size_t fileSize = shown.data ? shown.data->bytes.size() : 0;
    const size_t payload = layout ? std::min(layout->payloadOffset, fileSize) : 0;
    const size_t payloadLen = fileSize > payload ? fileSize - payload : 0;
    auto xOf = [&](double frac) { return p0.x + static_cast<float>(std::clamp(frac, 0.0, 1.0)) * w; };
    auto fracOf = [&](float x) { return std::clamp(static_cast<double>((x - p0.x) / w), 0.0, 1.0); };
    auto byteFrac = [&](size_t b) { return payloadLen ? static_cast<double>(b - std::min(b, payload)) / payloadLen : 0.0; };

    std::vector<Section> secs;
    if (layout && payloadLen) secs = sections(*layout, fileSize);
    const float bandTop = p0.y + h * 0.55f;
    for (const Section& s : secs) dl->AddRectFilled(ImVec2(xOf(byteFrac(s.from)), bandTop), ImVec2(xOf(byteFrac(s.to)), p1.y), s.color);

    // Changed bytes per bin, scaled to the busiest bin.
    const auto& hist = shown.corruption.histogram;
    if (shown.corrupted && !hist.empty()) {
        const uint32_t peak = *std::max_element(hist.begin(), hist.end());
        const float bw = w / hist.size();
        for (size_t i = 0; i < hist.size() && peak; i++) {
            if (!hist[i]) continue;
            const float bh = std::max(2.0f, (bandTop - p0.y - 2) * hist[i] / peak);
            dl->AddRectFilled(ImVec2(p0.x + i * bw, bandTop - bh), ImVec2(p0.x + (i + 1) * bw, bandTop), IM_COL32(230, 80, 70, 255));
        }
    }

    // Regions; the hovered one is brighter and right-click removes it.
    const double mouseFrac = fracOf(ImGui::GetIO().MousePos.x);
    int hoveredRegion = -1;
    for (int i = static_cast<int>(c.regions.size()) - 1; i >= 0 && hovered; i--) {
        const auto& r = c.regions[static_cast<size_t>(i)];
        if (mouseFrac >= std::min(r.start, r.end) && mouseFrac <= std::max(r.start, r.end)) {
            hoveredRegion = i;
            break;
        }
    }
    for (size_t i = 0; i < c.regions.size(); i++) {
        const auto& r = c.regions[i];
        const ImVec2 a(xOf(std::min(r.start, r.end)), p0.y), b(std::max(xOf(std::max(r.start, r.end)), a.x + 1), p1.y);
        const bool hot = static_cast<int>(i) == hoveredRegion;
        dl->AddRectFilled(a, b, IM_COL32(240, 200, 60, hot ? 90 : 55));
        dl->AddRect(a, b, IM_COL32(240, 200, 60, hot ? 255 : 170));
    }

    bool changed = false;
    static float dragFrom = -1;  // one byte map on screen at a time
    if (ImGui::IsItemActivated() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragFrom = static_cast<float>(mouseFrac);
    if (dragFrom >= 0) {
        const double lo = std::min<double>(dragFrom, mouseFrac), hi = std::max<double>(dragFrom, mouseFrac);
        dl->AddRectFilled(ImVec2(xOf(lo), p0.y), ImVec2(xOf(hi), p1.y), IM_COL32(255, 255, 255, 50));
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if ((hi - lo) * w >= 3) {
                c.regions.push_back({lo, hi, std::clamp(c.rate * 10, 0.005, static_cast<double>(kMaxRate))});
                changed = true;
            }
            dragFrom = -1;
        }
    }
    if (hoveredRegion >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        c.regions.erase(c.regions.begin() + hoveredRegion);
        changed = true;
    }

    if (hovered && dragFrom < 0) {
        const size_t at = payload + static_cast<size_t>(mouseFrac * payloadLen);
        const Section* sec = nullptr;
        for (const Section& s : secs)
            if (at >= s.from && at < s.to) sec = &s;
        ImGui::BeginTooltip();
        ImGui::Text("%.1f%% of the payload (byte %zu)", mouseFrac * 100, at);
        if (sec) ImGui::Text("channel %d %s", sec->channel + 1, sec->name);
        if (hoveredRegion >= 0) ImGui::TextDisabled("Right-click to remove this region");
        else ImGui::TextDisabled("Drag to add a region");
        ImGui::EndTooltip();
    }
    if (!fileSize) {
        const char* msg = "(no encoded data yet)";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(p0.x + (w - ts.x) / 2, p0.y + (h - ts.y) / 2), ImGui::GetColorU32(ImGuiCol_TextDisabled), msg);
    }
    return changed;
}

void legend() {
    auto key = [](ImU32 c, const char* label) {
        const float s = ImGui::GetTextLineHeight() * 0.8f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + s * 0.15f), ImVec2(p.x + s, p.y + s * 1.15f), c);
        ImGui::Dummy(ImVec2(s, s));
        ImGui::SameLine(0, 4);
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine();
    };
    key(IM_COL32(64, 100, 160, 255), "segmentation");
    key(IM_COL32(122, 78, 150, 255), "prediction");
    key(IM_COL32(50, 132, 122, 255), "image data");
    key(IM_COL32(230, 80, 70, 255), "changed");
    ImGui::NewLine();
}

} // namespace

void drawCorruptTab(App& app) {
    glic::CorruptionSettings& c = app.editCorruption();
    bool changed = false;

    changed |= ImGui::Checkbox("Corrupt the encoded data  (C)", &c.enabled);
    ImGui::SetItemTooltip("Randomly replace bytes of the .glic file before decoding");

    if (ImGui::BeginTable("##corrupt", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.5f);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Seed");
        ImGui::TableSetColumnIndex(1);
        const float reroll = ImGui::CalcTextSize("Reroll (N)").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetNextItemWidth(-(reroll + ImGui::GetStyle().ItemSpacing.x));
        changed |= ImGui::InputScalar("##cseed", ImGuiDataType_S64, &c.seed, nullptr, nullptr, "%lld");
        ImGui::SameLine();
        if (ImGui::Button("Reroll (N)")) app.rerollCorruptionSeed();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Rate");
        ImGui::SetItemTooltip("Chance that each byte outside a region is replaced");
        ImGui::TableSetColumnIndex(1);
        changed |= rateSlider("##rate", c.rate);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Header");
        ImGui::TableSetColumnIndex(1);
        changed |= ImGui::Checkbox("Protect the header", &c.protectHeader);
        ImGui::SetItemTooltip("Leave the first 237 bytes (size, colour space, per-channel settings) intact.\n"
                              "Unprotected, the decoder may read a different size or settings, or fail.");
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(!c.enabled);
    changed |= byteMap(app, c);
    legend();

    const ShownRender& shown = app.shown();
    if (shown.corrupted) {
        ImGui::Text("%zu bytes changed of %zu", shown.corruption.bytesChanged, shown.corruption.fileSize);
        if (shown.corruption.headerBytesChanged)
            ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.3f, 1), "%zu in the header", shown.corruption.headerBytesChanged);
        if (shown.proxy) ImGui::TextDisabled("(preview file; the full render's is larger)");
    }

    if (!c.regions.empty()) {
        ImGui::SeparatorText("Regions");
        if (ImGui::BeginTable("##regions", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("span", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("rate", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("##x", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();
            int remove = -1;
            for (size_t i = 0; i < c.regions.size(); i++) {
                auto& r = c.regions[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                float lo = static_cast<float>(std::min(r.start, r.end) * 100), hi = static_cast<float>(std::max(r.start, r.end) * 100);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloatRange2("##span", &lo, &hi, 0.1f, 0.0f, 100.0f, "%.1f%%", "%.1f%%", ImGuiSliderFlags_AlwaysClamp)) {
                    r.start = lo / 100.0;
                    r.end = hi / 100.0;
                    changed = true;
                }
                ImGui::TableSetColumnIndex(1);
                changed |= rateSlider("##rrate", r.rate);
                ImGui::TableSetColumnIndex(2);
                if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
                ImGui::SetItemTooltip("Remove this region");
                ImGui::PopID();
            }
            ImGui::EndTable();
            if (remove >= 0) {
                c.regions.erase(c.regions.begin() + remove);
                changed = true;
            }
        }
        if (ImGui::Button("Clear regions")) {
            c.regions.clear();
            changed = true;
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Drag across the byte map to corrupt one stretch harder (or protect it with rate 0).");
        ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();

    if (changed) app.settingsChanged();
}

} // namespace studio
