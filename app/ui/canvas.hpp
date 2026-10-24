#pragma once

#include "app.hpp"

namespace studio {

struct CanvasImages {
    const Texture* original = nullptr;
    const Texture* glitched = nullptr;   // may be a proxy (smaller), drawn stretched
    const Texture* difference = nullptr;
    int imageWidth = 0, imageHeight = 0; // source size: the canvas's coordinate space
    float glitchedU = 1, glitchedV = 1;  // UV extent of the useful part (GLIC pads small images)
    float differenceU = 1, differenceV = 1;
    float framebufferScale = 1;          // physical pixels per point, for "100%"
    bool loading = false;
};

// Draws the canvas into the current window: zoom (wheel, keys), pan
// (middle-drag or Space+drag), view modes, split slider, hold-Space original.
void drawCanvas(CanvasView& view, const CanvasImages& images, bool holdOriginal);

void zoomBy(CanvasView& view, float factor);
void zoomTo100(CanvasView& view, float framebufferScale);

} // namespace studio
