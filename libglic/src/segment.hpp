#pragma once

#include <glic/config.hpp>
#include "bitio.hpp"
#include <vector>
#include <limits>
#include <random>
#include <string>

namespace glic {

class Planes;
struct CodecContext;

// Segment structure representing a quad-tree node
struct Segment {
    int x = 0;
    int y = 0;
    int size = 0;

    // Prediction parameters
    PredictionMethod predType = PredictionMethod::NONE;
    float angle = -1.0f;
    int refAngle = -1;
    int16_t refX = std::numeric_limits<int16_t>::max();
    int16_t refY = std::numeric_limits<int16_t>::max();

    std::string toString() const;
};

// Create segmentation using quad-tree decomposition
std::vector<Segment> makeSegmentation(
    BitWriter& writer,
    const Planes& planes,
    int channel,
    int minSize,
    int maxSize,
    float threshold,
    CodecContext& ctx
);

// Read segmentation from bit stream
std::vector<Segment> readSegmentation(
    BitReader& reader,
    int paddedWidth,
    int paddedHeight,
    int width,
    int height
);

// Calculate standard deviation for segmentation decision
float calcStdDev(const Planes& planes, int channel, int x, int y, int size, std::mt19937& rng);

} // namespace glic
