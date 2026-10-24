#include "canvas.hpp"

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

void drawCanvas(CanvasView& view, const CanvasImages& img, bool holdOriginal) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 4 || avail.y < 4) return;
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
        return;
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
    for (const Texture* t : {img.original, img.glitched, img.difference})
        if (t && t->valid()) const_cast<Texture*>(t)->setFiltering(nearest);

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
    dl->PopClipRect();

    if (holdOriginal) {
        dl->AddText(ImVec2(origin.x + 8, origin.y + 6), IM_COL32(255, 255, 255, 220), "Original");
    }
}

} // namespace studio
