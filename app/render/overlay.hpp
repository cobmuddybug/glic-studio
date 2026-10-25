#pragma once

// Codec overlays (design §M4), built from the data libglic captures while
// decoding (glic::Capture): what the decoder actually did, so a mismatched
// decode shows its own predictors and corrupted data shows in the
// coefficient view.

#include <glic/glic.hpp>

#include <array>
#include <cstdint>

namespace studio {

// Which overlays are switched on, and for which channel. The panel (O) is the
// master switch: overlays are drawn, and captured, only while it is open.
struct OverlaySettings {
    bool panel = false;         // the overlays panel is open (O)
    bool blocks = true;         // segmentation: block outlines coloured by depth
    bool predictors = false;    // prediction-method heatmap
    bool coefficients = false;  // wavelet band view: the decoded values, log magnitude
    int channel = 0;
    float heatmapOpacity = 0.6f;

    bool needsSegments() const { return panel && (blocks || predictors); }
    bool needsValues() const { return panel && coefficients; }
};

constexpr int kMaxDepthColors = 10;

// Quadtree depth of a block: log2(rootSize / size), 0 for the root.
int blockDepth(int rootSize, int size);

// Opaque ARGB colours for legends and drawing.
glic::Color depthColor(int depth);       // depths past the palette reuse the last colour
glic::Color predictorColor(int method);  // 0..15; anything else is white

// Each block filled with its predictor's colour (alpha = opacity), at the
// capture's resolution. Empty if the channel has no blocks.
glic::Image predictorHeatmap(const glic::Capture& capture, int channel, float opacity);

// The channel's decoded values as grey: log(1 + |v|) scaled so the largest
// magnitude is white. With a wavelet these are the scaled coefficients in each
// block's own layout (LL at its top-left); without one, residuals.
glic::Image coefficientImage(const glic::Capture& capture, int channel, int64_t* maxMagnitude = nullptr);

// Share of the image area (inside width x height) predicted by each method.
std::array<double, glic::kPredictionCount> predictorShares(const glic::Capture& capture, int channel);

} // namespace studio
