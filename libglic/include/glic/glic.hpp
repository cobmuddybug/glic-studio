#pragma once

// libglic: the original GLIC (GlitchCodec/GLIC, Java/Processing) ported to C++.
//
// Output is byte-identical to GLIC run headless on Processing 3.5.4's Java 8
// (tests/baseline, made by tools/java-harness). See docs/glic-internals.md.

#include <glic/config.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace glic {

// ARGB pixels, row-major.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<Color> pixels;

    bool empty() const { return pixels.empty(); }
};

// The per-channel values a .glic header stores and the decoder reads, as the
// raw numbers GLIC's reader produces (unsigned bytes, so possibly outside the
// enums' named values).
struct ChannelHeader {
    int prediction = 0;
    int quantization = 0;
    int clamp = 0;
    int wavelet = 0;
    int transformType = 0;
    int scale = 0;
    int encoding = 0;

    bool operator==(const ChannelHeader&) const = default;
};

struct DecodeParams {
    int colorSpace = 1;
    std::array<uint8_t, 3> border = {128, 128, 128};
    std::array<ChannelHeader, 3> channels;

    bool operator==(const DecodeParams&) const = default;
};

// The decode parameters an encode with these settings writes into the header.
DecodeParams decodeParamsFor(const EncodeSettings& settings);

// Decode as the header says (linked), or with overridden parameters. Overrides
// follow GLIC's own "skip header" mode: meta predictors (SAD, BSAD, RANDOM)
// become NONE, as they cannot be applied at decode time.
struct DecodeSettings {
    std::optional<DecodeParams> override;
    int64_t seed = 0;          // only consumed by random wavelets/transforms in corrupt files
    int64_t maxPixels = 0;     // refuse larger decodes (0 = no limit; GLIC caps sides at 8191)
};

// Where things are in a .glic, as GLIC's reader walks it.
struct GlicLayout {
    int storedWidth = 0, storedHeight = 0;  // as written
    int width = 0, height = 0;              // as the decoder uses them: max(64, |v| % 8192)
    DecodeParams params;
    struct Section {
        size_t offset = 0;  // byte offset in the file
        size_t size = 0;    // size declared in the header (may run past the end)
    };
    std::array<Section, 3> segmentation, prediction, data;
    size_t payloadOffset = 0;  // first byte after the headers (segmentation mark)
};

// An encoded image: exactly the bytes of a .glic file.
struct GlicData {
    std::vector<uint8_t> bytes;

    // Header and section offsets, read as GLIC's decoder reads them. Never
    // fails: a truncated or corrupt file yields whatever the reader would see.
    GlicLayout layout() const;
};

// What one block looked like. `method` is the predictor actually applied,
// which for RANDOM/SAD/BSAD differs from the configured one.
struct SegmentInfo {
    int x = 0;
    int y = 0;
    int size = 0;
    int method = 0;
};

// Capture targets for overlays. Only the parts whose flag is set get filled.
struct Capture {
    bool segments = false;  // per-block geometry and predictor
    bool values = false;    // the integers that get encoded (wavelet coefficients or residuals)

    int width = 0;
    int height = 0;
    int rootSize = 0;       // side of the quadtree root; depth = log2(rootSize / size)
    std::array<std::vector<SegmentInfo>, 3> blocks;
    std::array<std::vector<int32_t>, 3> planeValues;  // row-major width*height
};

struct Hooks {
    Capture* capture = nullptr;
    // Checked between blocks and stages; when it becomes true the call
    // returns with `cancelled` set.
    const std::atomic<bool>* cancel = nullptr;
};

struct EncodeResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    GlicData data;
};

struct DecodeResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    Image image;
};

// Returns an error for settings that cannot be encoded (e.g. an empty image
// dimension), otherwise an empty string. Odd-but-valid GLIC settings, such as
// a transform scale that rounds every coefficient to zero, are allowed.
std::string validate(const EncodeSettings& settings);

EncodeResult encode(const Image& image, const EncodeSettings& settings, const Hooks* hooks = nullptr);
DecodeResult decode(const GlicData& data, const DecodeSettings& settings = {}, const Hooks* hooks = nullptr);

} // namespace glic
