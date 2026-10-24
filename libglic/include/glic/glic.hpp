#pragma once

// libglic: glic-cpp's codec as a library.
//
// Output is byte-identical to unmodified glic-cpp for every input it could
// handle (tests/baseline). See docs/glic-internals.md for the pipeline and
// for the upstream defects that were fixed or deliberately preserved.

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

using EncodeSettings = CodecConfig;

// Per-channel parameters that the .glic header stores and the decoder reads.
// These are exactly the values a mismatched decode can override.
struct ChannelHeader {
    PredictionMethod predictionMethod = PredictionMethod::PAETH;
    int quantizationValue = 110;
    ClampMethod clampMethod = ClampMethod::NONE;
    WaveletType waveletType = WaveletType::SYMLET8;
    TransformType transformType = TransformType::FWT;
    int transformScale = 20;
    EncodingMethod encodingMethod = EncodingMethod::PACKED;

    bool operator==(const ChannelHeader&) const = default;
};

struct DecodeParams {
    ColorSpace colorSpace = ColorSpace::HWB;
    std::array<uint8_t, 3> border = {128, 128, 128};
    std::array<ChannelHeader, 3> channels;

    bool operator==(const DecodeParams&) const = default;
};

// Decode with the header's parameters (linked), or override them all (mismatch).
struct DecodeSettings {
    std::optional<DecodeParams> override;
};

// In-memory .glic: header fields plus the nine payload sections.
struct GlicData {
    static constexpr uint32_t kMagic = 0x474C4332;  // "GLC2"
    static constexpr size_t kHeaderSize = 64;
    static constexpr size_t kChannelHeaderSize = 32;
    static constexpr size_t kPayloadOffset = kHeaderSize + 3 * kChannelHeaderSize;  // 160

    uint16_t version = 1;
    int width = 0;
    int height = 0;
    DecodeParams params;
    std::array<std::vector<uint8_t>, 3> segmentation;
    std::array<std::vector<uint8_t>, 3> prediction;
    std::array<std::vector<uint8_t>, 3> data;

    std::vector<uint8_t> serialize() const;

    // Never throws and never reads out of bounds. Sections that run past the end
    // of `bytes` are truncated to what is present (upstream read out of bounds).
    static std::optional<GlicData> parse(std::span<const uint8_t> bytes, std::string* error = nullptr);
};

// What one encoded block looked like. `method` is the predictor actually used,
// which for RANDOM/SAD/BSAD differs from the configured one.
struct SegmentInfo {
    int x = 0;
    int y = 0;
    int size = 0;
    PredictionMethod method = PredictionMethod::NONE;
};

// Capture targets for overlays. Only the parts whose flag is set get filled.
struct Capture {
    bool segments = false;      // per-block geometry and predictor
    bool values = false;        // the integers that get bit-packed (wavelet coefficients or residuals)

    int width = 0;
    int height = 0;
    int rootSize = 0;           // side of the quadtree root; depth = log2(rootSize / size)
    std::array<std::vector<SegmentInfo>, 3> blocks;
    std::array<std::vector<int32_t>, 3> planeValues;  // row-major width*height
};

struct Hooks {
    Capture* capture = nullptr;
    // Checked between blocks and stages. When it becomes true the call returns
    // with `cancelled` set.
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

// Returns an error message when the settings would hit undefined behaviour
// (e.g. transformScale <= 0), otherwise an empty string.
std::string validate(const EncodeSettings& settings);

EncodeResult encode(const Image& image, const EncodeSettings& settings, const Hooks* hooks = nullptr);
DecodeResult decode(const GlicData& data, const DecodeSettings& settings = {}, const Hooks* hooks = nullptr);

// The decode parameters an encode with these settings would store.
DecodeParams decodeParamsFor(const EncodeSettings& settings);

} // namespace glic
