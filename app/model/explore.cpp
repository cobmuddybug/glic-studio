#include "explore.hpp"

#include "params.hpp"

#include <algorithm>
#include <cmath>

namespace studio {

uint64_t ExploreRng::next() {
    uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double ExploreRng::uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }

int ExploreRng::range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + static_cast<int>(next() % static_cast<uint64_t>(hi - lo + 1));
}

namespace {

using glic::ChannelSettings;
using glic::EncodeSettings;

// ---- Randomize ranges. Each one keeps the result looking like an image:
//
//  colour space   any of the 16
//  min block      2..16       smaller is very slow with SAD; larger hides the quadtree
//  max block      32..256     >= min block; 512 gives one block for typical images
//  threshold      5..60       skewed low; above ~60 most images become a few big blocks
//  prediction     any; SAD/BSAD weighted down (they try all 16 predictors per block)
//  quantization   0 (30%) or 10..120; above ~150 everything flattens
//  clamp          none 80%, mod256 20%
//  wavelet        none 30%, else any of the 67 JWave filters
//  transform      FWT 75%, WPT 25%
//  compression    0 (70%) or 5..80
//  scale          2^8..2^20 with a wavelet (below 2^8 most coefficients round to 0: grey);
//                 2^20 without (unused then)
//  encoding       packed 60%, RLE 30%, raw 10% (raw and RLE react differently to corruption)
//  channels       identical 50% of the time, otherwise independent

glic::Prediction randomPrediction(ExploreRng& rng) {
    if (rng.chance(0.06)) return rng.chance(0.5) ? glic::Prediction::SAD : glic::Prediction::BSAD;
    if (rng.chance(0.06)) return glic::Prediction::RANDOM;
    return static_cast<glic::Prediction>(rng.range(0, glic::kPredictionCount - 1));
}

void randomizeChannel(ChannelSettings& c, ExploreRng& rng) {
    const int minExp = rng.range(1, 4);
    c.minBlockSize = 1 << minExp;
    c.maxBlockSize = 1 << rng.range(std::max(5, minExp), 8);
    c.threshold = static_cast<float>(5 + std::floor(55 * std::pow(rng.uniform(), 1.8)));
    c.prediction = randomPrediction(rng);
    c.quantization = rng.chance(0.3) ? 0 : rng.range(10, 120);
    c.clamp = rng.chance(0.2) ? glic::ClampMethod::MOD256 : glic::ClampMethod::NONE;
    c.wavelet = rng.chance(0.3) ? glic::kWaveletNone : rng.range(1, glic::kWaveletCount - 1);
    c.transformType = rng.chance(0.25) ? glic::TransformType::WPT : glic::TransformType::FWT;
    c.compress = rng.chance(0.7) ? 0.0f : static_cast<float>(rng.range(5, 80));
    c.scale = c.wavelet == glic::kWaveletNone ? 1 << 20 : 1 << rng.range(8, 20);
    const double e = rng.uniform();
    c.encoding = e < 0.6 ? glic::Encoding::PACKED : e < 0.9 ? glic::Encoding::RLE : glic::Encoding::RAW;
}

// ---- Mutation.

struct Mutator {
    float strength;
    ExploreRng& rng;

    // Each parameter is touched with this probability; choices switch less often.
    double touch() const { return 0.15 + 0.6 * strength; }
    bool numeric() { return rng.chance(touch()); }
    bool choice() { return rng.chance(touch() * (0.15 + 0.85 * strength)); }
    double signedUnit() { return 2 * rng.uniform() - 1; }

    int stepExp(int value, int lo, int hi, int maxStep) {
        const int step = std::max(1, static_cast<int>(std::lround(maxStep * strength)));
        int e = log2Floor(std::max(value, 1)) + rng.range(-step, step);
        return 1 << std::clamp(e, lo, hi);
    }
    int nudge(int value, int lo, int hi, double span) {
        return std::clamp(value + static_cast<int>(std::lround(signedUnit() * span * strength)), lo, hi);
    }

    void encodeChannel(ChannelSettings& c) {
        if (numeric()) c.minBlockSize = stepExp(c.minBlockSize, kBlockExpMin, kBlockExpMax, 2);
        if (numeric()) c.maxBlockSize = stepExp(c.maxBlockSize, kBlockExpMin, kBlockExpMax, 2);
        if (c.maxBlockSize < c.minBlockSize) std::swap(c.maxBlockSize, c.minBlockSize);
        if (numeric())
            c.threshold = std::clamp(static_cast<float>(c.threshold * std::exp(signedUnit() * 1.5 * strength)), 5.0f, 250.0f);
        if (numeric()) c.compress = static_cast<float>(nudge(static_cast<int>(c.compress), 0, 255, 80));
        if (choice()) c.prediction = randomPrediction(rng);
        decodeSide(c, false);
    }

    // The values the decoder uses (the decode tab edits only these).
    void decodeSide(ChannelSettings& c, bool decodeOnly) {
        if (numeric()) c.quantization = nudge(c.quantization, 0, 255, 100);
        if (c.wavelet != glic::kWaveletNone && numeric()) c.scale = stepExp(c.scale, kScaleExpMin, kScaleExpMax, 4);
        if (decodeOnly && choice()) c.prediction = static_cast<glic::Prediction>(rng.range(0, glic::kPredictionCount - 1));
        if (choice()) {
            c.wavelet = rng.chance(0.2) ? glic::kWaveletNone : rng.range(1, glic::kWaveletCount - 1);
            if (c.wavelet != glic::kWaveletNone && c.scale < (1 << 8)) c.scale = 1 << 16;
        }
        if (choice()) c.transformType = c.transformType == glic::TransformType::WPT ? glic::TransformType::FWT : glic::TransformType::WPT;
        if (choice()) c.clamp = c.clamp == glic::ClampMethod::MOD256 ? glic::ClampMethod::NONE : glic::ClampMethod::MOD256;
        if (choice()) c.encoding = static_cast<glic::Encoding>((static_cast<int>(c.encoding) + rng.range(1, 2)) % 3);
    }

    void settings(EncodeSettings& s, bool decodeOnly) {
        if (choice()) s.colorSpace = static_cast<glic::ColorSpace>(rng.range(0, glic::kColorSpaceCount - 1));
        const bool identical = channelsIdentical(s);
        for (int ch = 0; ch < (identical ? 1 : 3); ch++) {
            if (decodeOnly) decodeSide(s.channels[ch], true);
            else encodeChannel(s.channels[ch]);
        }
        if (identical) s.channels[1] = s.channels[2] = s.channels[0];
    }

    void corruption(glic::CorruptionSettings& c) {
        if (!c.enabled) return;
        if (numeric()) c.rate = std::clamp(c.rate * std::exp(signedUnit() * 2.0 * strength), 1e-6, 0.5);
        if (choice()) c.seed = static_cast<int64_t>(rng.next() >> 1);
        for (auto& r : c.regions)
            if (numeric()) r.rate = std::clamp(r.rate * std::exp(signedUnit() * 2.0 * strength), 0.0, 0.5);
    }
};

} // namespace

void randomizeEncode(EncodeSettings& s, ExploreRng& rng) {
    s.colorSpace = static_cast<glic::ColorSpace>(rng.range(0, glic::kColorSpaceCount - 1));
    const bool identical = rng.chance(0.5);
    for (int ch = 0; ch < 3; ch++) {
        if (identical && ch > 0) s.channels[ch] = s.channels[0];
        else randomizeChannel(s.channels[ch], rng);
    }
}

void randomize(glic::preset::Preset& p, ExploreRng& rng) {
    randomizeEncode(p.encode, rng);
    if (!p.decodeLinked) {
        // A mismatch of the new encode, not unrelated noise.
        p.decode = p.encode;
        for (auto& ch : p.decode.channels)
            if (static_cast<int>(ch.prediction) < 0) ch.prediction = glic::Prediction::NONE;
        Mutator m{0.5f, rng};
        m.settings(p.decode, true);
    }
}

glic::preset::Preset mutate(const glic::preset::Preset& p, float strength, ExploreRng& rng) {
    strength = std::clamp(strength, 0.0f, 1.0f);
    if (strength == 0) return p;
    // Retry until something changed (low strengths can roll no change).
    for (int attempt = 0; attempt < 32; attempt++) {
        glic::preset::Preset q = p;
        Mutator m{strength, rng};
        m.settings(q.encode, false);
        if (!q.decodeLinked) m.settings(q.decode, true);
        m.corruption(q.corruption);
        if (!q.sameSettings(p)) return q;
    }
    return p;
}

} // namespace studio
