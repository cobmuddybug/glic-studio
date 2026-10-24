#pragma once

// Codec parameters, numbered exactly as the original GLIC numbers them (these
// values are written into .glic files and GLIC presets).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace glic {

// ARGB, 0xAARRGGBB.
using Color = uint32_t;

inline uint8_t getA(Color c) { return (c >> 24) & 0xFF; }
inline uint8_t getR(Color c) { return (c >> 16) & 0xFF; }
inline uint8_t getG(Color c) { return (c >> 8) & 0xFF; }
inline uint8_t getB(Color c) { return c & 0xFF; }
inline Color makeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (Color(a) << 24) | (Color(r) << 16) | (Color(g) << 8) | Color(b);
}

enum class ColorSpace : int {
    OHTA = 0, RGB = 1, CMY = 2, HSB = 3, XYZ = 4, YXY = 5, HCL = 6, LUV = 7,
    LAB = 8, HWB = 9, RGGBG = 10, YPbPr = 11, YCbCr = 12, YDbDr = 13, GS = 14, YUV = 15,
};
constexpr int kColorSpaceCount = 16;

enum class Prediction : int {
    SAD = -1, BSAD = -2, RANDOM = -3,
    NONE = 0, CORNER = 1, H = 2, V = 3, DC = 4, DCMEDIAN = 5, MEDIAN = 6, AVG = 7,
    TRUEMOTION = 8, PAETH = 9, LDIAG = 10, HV = 11, JPEGLS = 12, DIFF = 13, REF = 14, ANGLE = 15,
};
constexpr int kPredictionCount = 16;  // concrete predictors 0..15

enum class ClampMethod : int { NONE = 0, MOD256 = 1 };

enum class TransformType : int { RANDOM = -1, FWT = 0, WPT = 1 };

// Wavelets are JWave's, identified by GLIC's ids: 0 = none, 1..67, -1 = random.
// See waveletName() for the list.
using WaveletId = int;
constexpr WaveletId kWaveletNone = 0;
constexpr WaveletId kWaveletRandom = -1;
constexpr WaveletId kWaveletHaar = 67;
constexpr int kWaveletCount = 68;  // ids 0..67

enum class Encoding : int { RAW = 0, PACKED = 1, RLE = 2 };

struct ChannelSettings {
    int minBlockSize = 4;          // GLIC GUI: 1 << slider(1..9)
    int maxBlockSize = 256;
    float threshold = 15;          // segmentation precision, 5..250 in the GUI
    Prediction prediction = Prediction::AVG;
    int quantization = 0;          // 0..255
    ClampMethod clamp = ClampMethod::NONE;
    TransformType transformType = TransformType::FWT;
    WaveletId wavelet = kWaveletNone;
    float compress = 0;            // 0..255
    int scale = 1 << 20;           // GLIC GUI: 2^slider(2..24)
    Encoding encoding = Encoding::PACKED;

    bool operator==(const ChannelSettings&) const = default;
};

// Defaults are GLIC's GUI defaults (not its CodecConfig literals, whose
// transform_scale of 20 rounds every wavelet coefficient to zero).
struct EncodeSettings {
    ColorSpace colorSpace = ColorSpace::RGB;
    std::array<uint8_t, 3> border = {128, 128, 128};  // RGB, colour outside the image
    std::array<ChannelSettings, 3> channels;
    // Seeds GLIC's random() (segmentation sampling, REF, RANDOM, random
    // wavelet/transform). Same image + settings + seed = same bytes.
    int64_t seed = 0;

    bool operator==(const EncodeSettings&) const = default;
};

std::string_view colorSpaceName(ColorSpace cs);
std::string_view predictionName(Prediction p);
std::string_view waveletName(WaveletId id);   // JWave's name, e.g. "Daubechies 4"
std::string_view encodingName(Encoding e);

} // namespace glic
