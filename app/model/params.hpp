#pragma once

// Encode parameter metadata and edits, shared by the settings panel (and later
// randomize/mutate). No UI code here.

#include <glic/config.hpp>

#include <string>
#include <vector>

namespace studio {

enum class ChannelParam {
    MinBlock, MaxBlock, Threshold, Prediction, Quantization, Clamp,
    Wavelet, TransformType, Compress, Scale, Encoding,
};

struct Choice {
    int value;
    std::string label;
};

// GLIC's GUI list order: concrete predictors 0..15, then SAD, BSAD, RANDOM.
const std::vector<Choice>& predictionChoices();
// NONE, the 67 JWave wavelets, RANDOM.
const std::vector<Choice>& waveletChoices();
const std::vector<Choice>& colorSpaceChoices();
const std::vector<Choice>& encodingChoices();
const std::vector<Choice>& transformTypeChoices();

// Power-of-two controls are edited as exponents, like GLIC's sliders.
constexpr int kBlockExpMin = 1, kBlockExpMax = 9;   // 2..512
constexpr int kScaleExpMin = 2, kScaleExpMax = 24;  // GLIC GUI range
int log2Floor(int v);

void resetParam(glic::EncodeSettings& s, int channel, ChannelParam p);
void copyToAllChannels(glic::EncodeSettings& s, int channel, ChannelParam p);
// Copies every channel-0 value to channels 1 and 2 (used when linking).
void linkChannels(glic::EncodeSettings& s);
bool channelsIdentical(const glic::EncodeSettings& s);

} // namespace studio
