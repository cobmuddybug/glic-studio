#include "overlay.hpp"

#include <algorithm>
#include <cmath>

namespace studio {

namespace {

bool validChannel(const glic::Capture& c, int ch) { return ch >= 0 && ch < 3 && c.width > 0 && c.height > 0; }

// Visits the part of each block inside the image.
template <typename F>
void forEachClippedBlock(const glic::Capture& c, int ch, F&& f) {
    for (const auto& b : c.blocks[ch]) {
        const int x0 = std::max(0, b.x), y0 = std::max(0, b.y);
        const int x1 = std::min(c.width, b.x + b.size), y1 = std::min(c.height, b.y + b.size);
        if (x0 < x1 && y0 < y1) f(b, x0, y0, x1, y1);
    }
}

} // namespace

int blockDepth(int rootSize, int size) {
    int d = 0;
    for (int s = size; s > 0 && s < rootSize; s <<= 1) d++;
    return d;
}

glic::Color depthColor(int depth) {
    // Cool (large blocks) to warm (small blocks), roughly viridis -> yellow -> red.
    static constexpr glic::Color kPalette[kMaxDepthColors] = {
        0xFF3B4CC0, 0xFF4C72D8, 0xFF4E98D8, 0xFF3FBFBF, 0xFF4CD08A,
        0xFF9AD84C, 0xFFE8D640, 0xFFF5A23A, 0xFFEE6A32, 0xFFD7302A,
    };
    return kPalette[std::clamp(depth, 0, kMaxDepthColors - 1)];
}

glic::Color predictorColor(int method) {
    // Sasha Trubetskoy's 20 distinct colours, first 16 (NONE gets the grey).
    static constexpr glic::Color kPalette[glic::kPredictionCount] = {
        0xFF808080,  // NONE
        0xFFE6194B,  // CORNER
        0xFF3CB44B,  // H
        0xFFFFE119,  // V
        0xFF0082C8,  // DC
        0xFFF58230,  // DCMEDIAN
        0xFF911EB4,  // MEDIAN
        0xFF46F0F0,  // AVG
        0xFFF032E6,  // TRUEMOTION
        0xFFD2F53C,  // PAETH
        0xFFFABED4,  // LDIAG
        0xFF008080,  // HV
        0xFFDCBEFF,  // JPEGLS
        0xFFAA6E28,  // DIFF
        0xFFFFFAC8,  // REF
        0xFF800000,  // ANGLE
    };
    if (method < 0 || method >= glic::kPredictionCount) return 0xFFFFFFFF;
    return kPalette[method];
}

glic::Image predictorHeatmap(const glic::Capture& c, int ch, float opacity) {
    glic::Image img;
    if (!validChannel(c, ch) || c.blocks[ch].empty()) return img;
    img.width = c.width;
    img.height = c.height;
    img.pixels.assign(static_cast<size_t>(c.width) * c.height, 0);
    const uint32_t alpha = static_cast<uint32_t>(std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255)) << 24;
    forEachClippedBlock(c, ch, [&](const glic::SegmentInfo& b, int x0, int y0, int x1, int y1) {
        const glic::Color col = (predictorColor(b.method) & 0x00FFFFFF) | alpha;
        for (int y = y0; y < y1; y++)
            std::fill(img.pixels.begin() + static_cast<size_t>(y) * c.width + x0,
                      img.pixels.begin() + static_cast<size_t>(y) * c.width + x1, col);
    });
    return img;
}

glic::Image coefficientImage(const glic::Capture& c, int ch, int64_t* maxMagnitude) {
    glic::Image img;
    if (maxMagnitude) *maxMagnitude = 0;
    if (!validChannel(c, ch)) return img;
    const auto& v = c.planeValues[ch];
    if (v.size() != static_cast<size_t>(c.width) * c.height) return img;
    int64_t peak = 0;
    for (int32_t x : v) peak = std::max(peak, std::abs(static_cast<int64_t>(x)));
    if (maxMagnitude) *maxMagnitude = peak;
    img.width = c.width;
    img.height = c.height;
    img.pixels.resize(v.size());
    const double norm = peak > 0 ? 255.0 / std::log1p(static_cast<double>(peak)) : 0;
    for (size_t i = 0; i < v.size(); i++) {
        const double m = std::log1p(static_cast<double>(std::abs(static_cast<int64_t>(v[i]))));
        const auto g = static_cast<uint8_t>(std::clamp(std::lround(m * norm), 0L, 255L));
        img.pixels[i] = glic::makeColor(g, g, g);
    }
    return img;
}

std::array<double, glic::kPredictionCount> predictorShares(const glic::Capture& c, int ch) {
    std::array<double, glic::kPredictionCount> share{};
    if (!validChannel(c, ch)) return share;
    double total = 0;
    forEachClippedBlock(c, ch, [&](const glic::SegmentInfo& b, int x0, int y0, int x1, int y1) {
        const double area = static_cast<double>(x1 - x0) * (y1 - y0);
        if (b.method >= 0 && b.method < glic::kPredictionCount) share[b.method] += area;
        total += area;
    });
    if (total > 0)
        for (auto& s : share) s /= total;
    return share;
}

} // namespace studio
