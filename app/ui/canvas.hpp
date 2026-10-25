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

    // Overlays (design §M4), drawn over any view mode except hold-Space.
    // They are in the decoded image's pixels, over the same area as `glitched`.
    const Texture* coefficients = nullptr;  // opaque band view
    const Texture* heatmap = nullptr;       // translucent predictor colours
    const glic::Capture* capture = nullptr; // block outlines come from here
    int overlayChannel = 0;
    bool blocks = false;
    int overlayWidth = 0, overlayHeight = 0;  // useful size of the decoded image
};

struct CanvasOutput {
    int hiddenBlocks = 0;      // outlines skipped because they would be too small to see
    bool overImage = false;    // the cursor is over the image
    int imageX = 0, imageY = 0;  // cursor position in decoded-image pixels
};

// Draws the canvas into the current window: zoom (wheel, keys), pan
// (middle-drag or Space+drag), view modes, split slider, hold-Space original.
CanvasOutput drawCanvas(CanvasView& view, const CanvasImages& images, bool holdOriginal);

void zoomBy(CanvasView& view, float factor);
void zoomTo100(CanvasView& view, float framebufferScale);

} // namespace studio
