#include <glic/config.hpp>

#include "wavelets.hpp"

namespace glic {

std::string_view colorSpaceName(ColorSpace cs) {
    switch (cs) {
    case ColorSpace::OHTA: return "OHTA";
    case ColorSpace::RGB: return "RGB";
    case ColorSpace::CMY: return "CMY";
    case ColorSpace::HSB: return "HSB";
    case ColorSpace::XYZ: return "XYZ";
    case ColorSpace::YXY: return "YXY";
    case ColorSpace::HCL: return "HCL";
    case ColorSpace::LUV: return "LUV";
    case ColorSpace::LAB: return "LAB";
    case ColorSpace::HWB: return "HWB";
    case ColorSpace::RGGBG: return "R-GGB-G";
    case ColorSpace::YPbPr: return "YPbPr";
    case ColorSpace::YCbCr: return "YCbCr";
    case ColorSpace::YDbDr: return "YDbDr";
    case ColorSpace::GS: return "Greyscale";
    case ColorSpace::YUV: return "YUV";
    }
    return "RGB";
}

std::string_view predictionName(Prediction p) {
    static constexpr std::string_view names[] = {"NONE", "CORNER", "H", "V", "DC", "DCMEDIAN", "MEDIAN", "AVG",
                                                 "TRUEMOTION", "PAETH", "LDIAG", "HV", "JPEGLS", "DIFF", "REF", "ANGLE"};
    switch (p) {
    case Prediction::SAD: return "SAD";
    case Prediction::BSAD: return "BSAD";
    case Prediction::RANDOM: return "RANDOM";
    default: break;
    }
    int i = static_cast<int>(p);
    return i >= 0 && i < kPredictionCount ? names[i] : "NONE";
}

std::string_view waveletName(WaveletId id) {
    if (id == kWaveletNone) return "NONE";
    if (id == kWaveletRandom) return "RANDOM";
    if (const WaveletDef* w = findWavelet(id)) return w->name;
    return "NONE";
}

std::string_view encodingName(Encoding e) {
    switch (e) {
    case Encoding::RAW: return "RAW";
    case Encoding::PACKED: return "PACKED";
    case Encoding::RLE: return "RLE";
    }
    return "RAW";
}

} // namespace glic
