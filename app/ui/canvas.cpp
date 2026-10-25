#include "canvas.hpp"

#include "render/overlay.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace studio {

namespace {

constexpr float kMinZoom = 1.0f / 64, kMaxZoom = 64;

void drawImage(ImDrawList* dl, const Texture* tex, ImVec2 p0, ImVec2 p1, ImVec2 uv0, ImVec2 uv1) {
    if (tex && tex->valid()) dl->AddImage(ImTextureRef(tex->imguiId()), p0, p1, uv0, uv1);
}

void backdrop(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
    dl->AddRectFilled(p0, p1, IM_COL32(40, 40, 40, 255));
}

ImU32 toImU32(glic::Color c, int alpha) { return IM_COL32(glic::getR(c), glic::getG(c), glic::getB(c), alpha); }

// Outlines (top and left edge of every block; the image border closes them),
// in screen space so they stay one pixel wide at any zoom. Blocks smaller than
// kMinBlockPx on screen, or off screen, are skipped. Returns how many were too small.
int drawBlockOutlines(ImDrawList* dl, const glic::Capture& cap, int ch, ImVec2 p0, float scale, ImVec2 clip0, ImVec2 clip1) {
    constexpr float kMinBlockPx = 4;
    constexpr int kMaxLines = 150000;  // bounds the draw list even for huge images
    int hidden = 0, drawn = 0;
    for (const auto& b : cap.blocks[ch]) {
        const float side = b.size * scale;
        const float x0 = p0.x + b.x * scale, y0 = p0.y + b.y * scale;
        if (x0 > clip1.x || y0 > clip1.y || x0 + side < clip0.x || y0 + side < clip0.y) continue;
        if (side < kMinBlockPx || drawn >= kMaxLines) {
            hidden++;
            continue;
        }
        const ImU32 col = toImU32(depthColor(blockDepth(cap.rootSize, b.size)), 210);
        const float x = std::floor(x0) + 0.5f, y = std::floor(y0) + 0.5f;
        dl->AddLine(ImVec2(x, y), ImVec2(x0 + side, y), col);
        dl->AddLine(ImVec2(x, y), ImVec2(x, y0 + side), col);
        drawn++;
    }
    return hidden;
}

} // namespace

void zoomBy(CanvasView& view, float factor) {
    view.fit = false;
    view.zoom = std::clamp(view.zoom * factor, kMinZoom, kMaxZoom);
}

void zoomTo100(CanvasView& view, float framebufferScale) {
    view.fit = false;
    view.zoom = 1.0f / std::max(framebufferScale, 0.01f);
    view.panX = view.panY = 0;
}

CanvasOutput drawCanvas(CanvasView& view, const CanvasImages& img, bool holdOriginal) {
    CanvasOutput out;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 4 || avail.y < 4) return out;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + avail.x, origin.y + avail.y);
    dl->AddRectFilled(origin, end, IM_COL32(24, 24, 24, 255));

    ImGui::InvisibleButton("##canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();

    if (img.imageWidth <= 0 || !img.original || !img.original->valid()) {
        const char* msg = img.loading ? "Loading..." : "Open an image (Ctrl+O) or drop one here";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(origin.x + (avail.x - ts.x) / 2, origin.y + (avail.y - ts.y) / 2),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), msg);
        return out;
    }

    const float w = static_cast<float>(img.imageWidth), h = static_cast<float>(img.imageHeight);
    if (view.fit) {
        view.zoom = std::min(avail.x / w, avail.y / h);
        view.panX = view.panY = 0;
    }

    // Zoom about the cursor.
    if (hovered && io.MouseWheel != 0) {
        const float factor = std::pow(1.2f, io.MouseWheel);
        const float old = view.zoom;
        zoomBy(view, factor);
        const float cx = origin.x + avail.x / 2 + view.panX, cy = origin.y + avail.y / 2 + view.panY;
        const float k = view.zoom / old;
        view.panX += (io.MousePos.x - cx) * (1 - k);
        view.panY += (io.MousePos.y - cy) * (1 - k);
    }

    // Pan: middle-drag, or Space + left-drag.
    const bool spaceDrag = ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0);
    const bool middleDrag = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0);
    if (spaceDrag || middleDrag) {
        view.fit = false;
        view.panX += io.MouseDelta.x;
        view.panY += io.MouseDelta.y;
    }

    const float dw = w * view.zoom, dh = h * view.zoom;
    const ImVec2 p0(std::floor(origin.x + (avail.x - dw) / 2 + view.panX), std::floor(origin.y + (avail.y - dh) / 2 + view.panY));
    const ImVec2 p1(p0.x + dw, p0.y + dh);

    // Show pixels crisply when zoomed in.
    const bool nearest = view.zoom * img.framebufferScale >= 1.5f;
    for (const Texture* t : {img.original, img.glitched, img.difference, img.coefficients})
        if (t && t->valid()) const_cast<Texture*>(t)->setFiltering(nearest);
    if (img.heatmap && img.heatmap->valid()) const_cast<Texture*>(img.heatmap)->setFiltering(true);  // crisp blocks

    dl->PushClipRect(origin, end, true);
    backdrop(dl, p0, p1);

    const bool haveGlitched = img.glitched && img.glitched->valid();
    ViewMode mode = holdOriginal ? ViewMode::Original : view.mode;
    if (!haveGlitched && mode != ViewMode::Original) mode = ViewMode::Original;
    const ImVec2 guv(img.glitchedU, img.glitchedV);

    switch (mode) {
    case ViewMode::Original:
        drawImage(dl, img.original, p0, p1, ImVec2(0, 0), ImVec2(1, 1));
        break;
    case ViewMode::Glitched:
        drawImage(dl, img.glitched, p0, p1, ImVec2(0, 0), guv);
        break;
    case ViewMode::Difference:
        if (img.difference && img.difference->valid())
            drawImage(dl, img.difference, p0, p1, ImVec2(0, 0), ImVec2(img.differenceU, img.differenceV));
        break;
    case ViewMode::Split: {
        // Left: original. Right: glitched. Drag anywhere on the image to move the divider.
        if (hovered && ImGui::IsItemActive() && !ImGui::IsKeyDown(ImGuiKey_Space) &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left))
            view.split = std::clamp((io.MousePos.x - p0.x) / dw, 0.0f, 1.0f);
        const float sx = p0.x + dw * view.split;
        drawImage(dl, img.original, p0, ImVec2(sx, p1.y), ImVec2(0, 0), ImVec2(view.split, 1));
        drawImage(dl, img.glitched, ImVec2(sx, p0.y), p1, ImVec2(guv.x * view.split, 0), guv);
        dl->AddLine(ImVec2(sx, p0.y), ImVec2(sx, p1.y), IM_COL32(255, 255, 255, 200), 1.0f);
        break;
    }
    }

    if (!holdOriginal && img.overlayWidth > 0 && img.overlayHeight > 0) {
        const ImVec2 ouv(img.glitchedU, img.glitchedV);
        if (img.coefficients && img.coefficients->valid()) drawImage(dl, img.coefficients, p0, p1, ImVec2(0, 0), ouv);
        if (img.heatmap && img.heatmap->valid()) drawImage(dl, img.heatmap, p0, p1, ImVec2(0, 0), ouv);
        if (img.blocks && img.capture && img.overlayChannel >= 0 && img.overlayChannel < 3) {
            const ImVec2 c0(std::max(origin.x, p0.x), std::max(origin.y, p0.y));
            const ImVec2 c1(std::min(end.x, p1.x), std::min(end.y, p1.y));
            dl->PushClipRect(c0, c1, true);
            out.hiddenBlocks = drawBlockOutlines(dl, *img.capture, img.overlayChannel, p0, dw / img.overlayWidth, c0, c1);
            dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 120));
            dl->PopClipRect();
        }
        if (hovered && io.MousePos.x >= p0.x && io.MousePos.y >= p0.y && io.MousePos.x < p1.x && io.MousePos.y < p1.y) {
            out.overImage = true;
            out.imageX = std::min(img.overlayWidth - 1, static_cast<int>((io.MousePos.x - p0.x) / dw * img.overlayWidth));
            out.imageY = std::min(img.overlayHeight - 1, static_cast<int>((io.MousePos.y - p0.y) / dh * img.overlayHeight));
        }
    }
    dl->PopClipRect();

    if (holdOriginal) {
        dl->AddText(ImVec2(origin.x + 8, origin.y + 6), IM_COL32(255, 255, 255, 220), "Original");
    }
    return out;
}

} // namespace studio
