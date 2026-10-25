#pragma once

// The corruption lab (design §M5): deterministic damage to .glic bytes between
// encode and decode. Not part of GLIC; the codec itself is untouched.

#include <glic/glic.hpp>

#include <cstdint>
#include <vector>

namespace glic {

// A stretch of the payload with its own rate. start/end are fractions of the
// payload (the bytes after the 237-byte headers), so they fit any image size.
struct CorruptRegion {
    double start = 0;
    double end = 0;
    double rate = 0;

    bool operator==(const CorruptRegion&) const = default;
};

enum class CorruptMode : int {
    RandomBytes = 0,  // each byte is replaced, with probability `rate`, by a random byte
};

struct CorruptionSettings {
    bool enabled = false;
    int64_t seed = 1337;
    CorruptMode mode = CorruptMode::RandomBytes;
    double rate = 0.0005;              // probability per byte outside any region
    std::vector<CorruptRegion> regions;  // later regions win where they overlap
    bool protectHeader = true;         // leave the headers (bytes before the payload) alone

    bool operator==(const CorruptionSettings&) const = default;
};

struct CorruptionStats {
    size_t bytesChanged = 0;        // in the whole file
    size_t headerBytesChanged = 0;  // of those, before the payload
    size_t payloadOffset = 0;
    size_t fileSize = 0;
    // Changed payload bytes per bin, bins spread evenly over the payload (when asked for).
    std::vector<uint32_t> histogram;
};

// Damages `data` in place. Same bytes + settings = same result: one
// std::mt19937_64 seeded from settings.seed, one draw per eligible byte (and
// one more per hit). Does nothing when !enabled.
CorruptionStats corrupt(GlicData& data, const CorruptionSettings& settings, int histogramBins = 0);

// Bounds the decode of a corrupted file whose source was w x h. A damaged
// header can claim up to 8191 x 8191, and even 8000 x 64 costs as much as
// 8192 x 8192, so the longer side may grow to twice the source's (at least 256).
void limitCorruptDecode(DecodeSettings& settings, int width, int height);

} // namespace glic
