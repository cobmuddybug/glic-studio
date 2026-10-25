#pragma once

// Animation (design §M7): one parameter swept from a start to an end value
// over N frames with an easing curve. Every frame is an ordinary preset, so
// any frame can be reproduced on its own (and each exported PNG embeds it).

#include "preset.hpp"

#include <string>
#include <vector>

namespace studio {

enum class AnimTarget {
    Threshold, Quantization, Compression, ScaleExp, MinBlockExp, MaxBlockExp,  // per channel, numeric
    Prediction, Wavelet,                                                       // per channel, stepped ids
    ColorSpace, EncodeSeed,                                                    // per image
    CorruptionSeed, CorruptionRate,                                            // corruption
};

enum class Easing { Linear, EaseIn, EaseOut, EaseInOut };

struct AnimationSpec {
    AnimTarget target = AnimTarget::Quantization;
    bool decodeSide = false;  // animate the decode value (decoding must be unlinked)
    int channel = -1;         // -1 = all channels
    double start = 0, end = 100;
    int frames = 30;
    Easing easing = Easing::Linear;
};

struct AnimTargetInfo {
    AnimTarget target;
    const char* name;
    double min, max;      // the range the UI offers
    bool integer;         // rounded to whole steps
    bool perChannel;
    bool decodable;       // also exists on the decode side
};
const std::vector<AnimTargetInfo>& animTargets();
const AnimTargetInfo& animTargetInfo(AnimTarget t);

const char* easingName(Easing e);
double ease(Easing e, double t);  // t and result in [0, 1]

// The swept value at `frame` (0 .. frames-1), rounded for integer targets
// and clamped to the target's range.
double frameValue(const AnimationSpec& spec, int frame);
// `base` with the target set to its frame value. Corruption targets switch
// corruption on.
glic::preset::Preset framePreset(const glic::preset::Preset& base, const AnimationSpec& spec, int frame);
// The target's value in `base` (channel 0 when all channels are animated).
double currentValue(const glic::preset::Preset& base, const AnimationSpec& spec);

} // namespace studio
