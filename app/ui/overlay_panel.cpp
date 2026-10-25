#include "overlay_panel.hpp"

#include "model/params.hpp"
#include "render/overlay.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace studio {

namespace {

ImVec4 toVec4(glic::Color c) {
    return ImVec4(glic::getR(c) / 255.0f, glic::getG(c) / 255.0f, glic::getB(c) / 255.0f, 1.0f);
}

void swatch(const char* id, glic::Color c) {
    const float s = ImGui::GetTextLineHeight();
    ImGui::ColorButton(id, toVec4(c), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoBorder,
                       ImVec2(s, s));
}

std::string predictorLabel(int method) {
    return std::string(glic::predictionName(static_cast<glic::Prediction>(method)));
}

// The wavelet the shown image was decoded with, for this channel.
int decodedWavelet(App& app, int ch) {
    if (!app.decodeLinked()) return app.editDecode().channels[ch].wavelet;
    if (app.shown().data) return app.shown().data->layout().params.channels[ch].wavelet;
    return app.settings().channels[ch].wavelet;
}

void depthLegend(const glic::Capture* cap) {
    const int root = cap && cap->rootSize > 0 ? cap->rootSize : 512;
    int maxDepth = 0;
    if (cap)
        for (const auto& b : cap->blocks[0]) maxDepth = std::max(maxDepth, blockDepth(root, b.size));
    for (int ch = 1; cap && ch < 3; ch++)
        for (const auto& b : cap->blocks[ch]) maxDepth = std::max(maxDepth, blockDepth(root, b.size));
    maxDepth = std::min(maxDepth, kMaxDepthColors - 1);
    ImGui::TextDisabled("%d px", root);
    for (int d = 0; d <= maxDepth; d++) {
        ImGui::SameLine(0, 2);
        char id[16];
        std::snprintf(id, sizeof id, "##d%d", d);
        swatch(id, depthColor(d));
        ImGui::SetItemTooltip("%d px blocks (depth %d)", root >> d, d);
    }
    ImGui::SameLine(0, 4);
    ImGui::TextDisabled("%d px", std::max(1, root >> maxDepth));
}

void predictorLegend(const std::array<double, glic::kPredictionCount>& shares) {
    std::vector<int> order;
    for (int m = 0; m < glic::kPredictionCount; m++)
        if (shares[m] > 0) order.push_back(m);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return shares[a] > shares[b]; });
    if (order.empty()) {
        ImGui::TextDisabled("(no blocks)");
        return;
    }
    // Two columns keep all 16 predictors short enough for small windows.
    if (!ImGui::BeginTable("##legend", 2, ImGuiTableFlags_SizingStretchSame)) return;
    for (int m : order) {
        ImGui::TableNextColumn();
        char id[16];
        std::snprintf(id, sizeof id, "##p%d", m);
        swatch(id, predictorColor(m));
        ImGui::SameLine(0, 4);
        ImGui::Text("%s %.0f%%", predictorLabel(m).c_str(), shares[m] * 100);
        ImGui::SetItemTooltip("%s: %.1f%% of the image", predictorLabel(m).c_str(), shares[m] * 100);
    }
    ImGui::EndTable();
}

// The block containing (x, y) in channel `ch`, if any.
const glic::SegmentInfo* blockAt(const glic::Capture& cap, int ch, int x, int y) {
    for (const auto& b : cap.blocks[ch])
        if (x >= b.x && y >= b.y && x < b.x + b.size && y < b.y + b.size) return &b;
    return nullptr;
}

void cursorReadout(const glic::Capture* cap, int ch, const CanvasOutput& co) {
    if (!co.overImage) {
        ImGui::TextDisabled("Point at the image for details");
        return;
    }
    ImGui::Text("x %d, y %d", co.imageX, co.imageY);
    if (!cap) return;
    if (cap->segments) {
        if (const auto* b = blockAt(*cap, ch, co.imageX, co.imageY))
            ImGui::Text("%d px block at %d,%d \xC2\xB7 %s", b->size, b->x, b->y, predictorLabel(b->method).c_str());
    }
    if (cap->values) {
        const auto& v = cap->planeValues[ch];
        const size_t i = static_cast<size_t>(co.imageY) * cap->width + co.imageX;
        if (i < v.size()) ImGui::Text("value %d", v[i]);
    }
}

} // namespace

void drawOverlayPanel(App& app, const CanvasOutput& canvas) {
    OverlaySettings& ov = app.overlays();
    const glic::Capture* cap = app.shown().capture.get();
    const float width = ImGui::GetFontSize() * 18;
    const float pad = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(at.x + std::max(pad, avail.x - width - pad), at.y + pad));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
    ImGui::BeginChild("##overlays", ImVec2(width, 0),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
    bool changed = false;

    ImGui::TextUnformatted("Codec overlays");
    ImGui::SameLine();
    ImGui::TextDisabled("(O hides)");

    const glic::ColorSpace cs = app.decodedColorSpace();
    for (int ch = 0; ch < 3; ch++) {
        if (ch) ImGui::SameLine();
        char label[32];
        std::snprintf(label, sizeof label, "%s##ch%d", channelName(cs, ch), ch);
        changed |= ImGui::RadioButton(label, &ov.channel, ch);
    }
    ImGui::SetItemTooltip("Channel the overlays show");

    ImGui::Separator();
    changed |= ImGui::Checkbox("Blocks", &ov.blocks);
    ImGui::SetItemTooltip("Segmentation: quadtree block outlines, coloured by size");
    if (ov.blocks) {
        depthLegend(cap);
        if (canvas.hiddenBlocks > 0) ImGui::TextDisabled("%d small blocks hidden; zoom in", canvas.hiddenBlocks);
    }

    changed |= ImGui::Checkbox("Predictors", &ov.predictors);
    ImGui::SetItemTooltip("The prediction method each block was decoded with");
    if (ov.predictors) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= ImGui::SliderFloat("##opacity", &ov.heatmapOpacity, 0.1f, 1.0f, "opacity %.2f");
        predictorLegend(app.predictorShares());
    }

    changed |= ImGui::Checkbox("Coefficients", &ov.coefficients);
    ImGui::SetItemTooltip("Wavelet bands: the decoded values per block (LL top-left), log |value|");
    if (ov.coefficients) {
        if (decodedWavelet(app, ov.channel) == glic::kWaveletNone) ImGui::TextDisabled("No wavelet: prediction residuals");
        ImGui::TextDisabled("black 0 .. white %lld", static_cast<long long>(app.coefficientPeak()));
    }

    ImGui::Separator();
    cursorReadout(cap, ov.channel, canvas);

    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (changed) app.overlaysChanged();
}

} // namespace studio
