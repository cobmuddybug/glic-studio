#include "animation.hpp"

#include "params.hpp"

#include <algorithm>
#include <cmath>

namespace studio {

const std::vector<AnimTargetInfo>& animTargets() {
    // name, range, integer, per channel, decode side
    static const std::vector<AnimTargetInfo> t = {
        {AnimTarget::Quantization, "Quantization", 0, 255, true, true, true},
        {AnimTarget::Threshold, "Threshold", 5, 250, false, true, false},
        {AnimTarget::Compression, "Compression", 0, 255, false, true, false},
        {AnimTarget::ScaleExp, "Scale (2^n)", kScaleExpMin, kScaleExpMax, true, true, true},
        {AnimTarget::MinBlockExp, "Min block (2^n)", kBlockExpMin, kBlockExpMax, true, true, false},
        {AnimTarget::MaxBlockExp, "Max block (2^n)", kBlockExpMin, kBlockExpMax, true, true, false},
        {AnimTarget::Prediction, "Prediction (id)", 0, glic::kPredictionCount - 1, true, true, true},
        {AnimTarget::Wavelet, "Wavelet (id)", 0, glic::kWaveletCount - 1, true, true, true},
        {AnimTarget::ColorSpace, "Colour space (id)", 0, glic::kColorSpaceCount - 1, true, false, true},
        {AnimTarget::EncodeSeed, "Encode seed", 0, 1e9, true, false, false},
        {AnimTarget::CorruptionSeed, "Corruption seed", 0, 1e9, true, false, false},
        {AnimTarget::CorruptionRate, "Corruption rate", 0, 0.5, false, false, false},
    };
    return t;
}

const AnimTargetInfo& animTargetInfo(AnimTarget target) {
    for (const auto& i : animTargets())
        if (i.target == target) return i;
    return animTargets().front();
}

const char* easingName(Easing e) {
    switch (e) {
    case Easing::Linear: return "Linear";
    case Easing::EaseIn: return "Ease in";
    case Easing::EaseOut: return "Ease out";
    case Easing::EaseInOut: return "Ease in-out";
    }
    return "";
}

double ease(Easing e, double t) {
    t = std::clamp(t, 0.0, 1.0);
    switch (e) {
    case Easing::Linear: return t;
    case Easing::EaseIn: return t * t * t;
    case Easing::EaseOut: return 1 - std::pow(1 - t, 3);
    case Easing::EaseInOut: return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
    }
    return t;
}

double frameValue(const AnimationSpec& spec, int frame) {
    const AnimTargetInfo& info = animTargetInfo(spec.target);
    const double t = spec.frames > 1 ? static_cast<double>(std::clamp(frame, 0, spec.frames - 1)) / (spec.frames - 1) : 0.0;
    double v = spec.start + (spec.end - spec.start) * ease(spec.easing, t);
    if (info.integer) v = std::round(v);
    return std::clamp(v, info.min, info.max);
}

namespace {

void setChannel(glic::ChannelSettings& c, AnimTarget t, double v) {
    const int i = static_cast<int>(v);
    switch (t) {
    case AnimTarget::Quantization: c.quantization = i; break;
    case AnimTarget::Threshold: c.threshold = static_cast<float>(v); break;
    case AnimTarget::Compression: c.compress = static_cast<float>(v); break;
    case AnimTarget::ScaleExp: c.scale = 1 << i; break;
    case AnimTarget::MinBlockExp:
        c.minBlockSize = 1 << i;
        c.maxBlockSize = std::max(c.maxBlockSize, c.minBlockSize);
        break;
    case AnimTarget::MaxBlockExp:
        c.maxBlockSize = 1 << i;
        c.minBlockSize = std::min(c.minBlockSize, c.maxBlockSize);
        break;
    case AnimTarget::Prediction: c.prediction = static_cast<glic::Prediction>(i); break;
    case AnimTarget::Wavelet: c.wavelet = i; break;
    default: break;
    }
}

double getChannel(const glic::ChannelSettings& c, AnimTarget t) {
    switch (t) {
    case AnimTarget::Quantization: return c.quantization;
    case AnimTarget::Threshold: return c.threshold;
    case AnimTarget::Compression: return c.compress;
    case AnimTarget::ScaleExp: return log2Floor(std::max(c.scale, 1));
    case AnimTarget::MinBlockExp: return log2Floor(std::max(c.minBlockSize, 1));
    case AnimTarget::MaxBlockExp: return log2Floor(std::max(c.maxBlockSize, 1));
    case AnimTarget::Prediction: return std::max(0, static_cast<int>(c.prediction));
    case AnimTarget::Wavelet: return std::max(0, c.wavelet);
    default: return 0;
    }
}

} // namespace

glic::preset::Preset framePreset(const glic::preset::Preset& base, const AnimationSpec& spec, int frame) {
    glic::preset::Preset p = base;
    const double v = frameValue(spec, frame);
    const AnimTargetInfo& info = animTargetInfo(spec.target);
    glic::EncodeSettings& s = spec.decodeSide && info.decodable && !p.decodeLinked ? p.decode : p.encode;
    if (info.perChannel) {
        for (int ch = 0; ch < 3; ch++)
            if (spec.channel < 0 || spec.channel == ch) setChannel(s.channels[static_cast<size_t>(ch)], spec.target, v);
    } else {
        switch (spec.target) {
        case AnimTarget::ColorSpace: s.colorSpace = static_cast<glic::ColorSpace>(static_cast<int>(v)); break;
        case AnimTarget::EncodeSeed:
            p.encode.seed = static_cast<int64_t>(v);
            p.decode.seed = p.encode.seed;
            break;
        case AnimTarget::CorruptionSeed:
            p.corruption.enabled = true;
            p.corruption.seed = static_cast<int64_t>(v);
            break;
        case AnimTarget::CorruptionRate:
            p.corruption.enabled = true;
            p.corruption.rate = v;
            break;
        default: break;
        }
    }
    return p;
}

double currentValue(const glic::preset::Preset& base, const AnimationSpec& spec) {
    const AnimTargetInfo& info = animTargetInfo(spec.target);
    const glic::EncodeSettings& s = spec.decodeSide && info.decodable && !base.decodeLinked ? base.decode : base.encode;
    if (info.perChannel) return getChannel(s.channels[static_cast<size_t>(std::max(0, spec.channel))], spec.target);
    switch (spec.target) {
    case AnimTarget::ColorSpace: return static_cast<int>(s.colorSpace);
    case AnimTarget::EncodeSeed: return static_cast<double>(base.encode.seed);
    case AnimTarget::CorruptionSeed: return static_cast<double>(base.corruption.seed);
    case AnimTarget::CorruptionRate: return base.corruption.rate;
    default: return 0;
    }
}

} // namespace studio
