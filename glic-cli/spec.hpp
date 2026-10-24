#pragma once

#include <glic/config.hpp>
#include <glic/glic.hpp>

#include <string>

namespace glic {

// Applies "key=value key=value ..." to settings, in the same format as the Java
// harness (tools/java-harness): colorspace border min max thr enc pred quant
// clamp ttype wavelet compress scale seed. Per-channel keys take one value or
// three comma-separated. "-" means no changes. Returns an error or "".
std::string applySpec(EncodeSettings& settings, const std::string& spec);

// GLIC's raw CodecConfig defaults (not the GUI defaults EncodeSettings uses):
// HWB, RAW encoding, PAETH, quantization 110, Symlet 9, scale 20, min block 2.
EncodeSettings glicCodecConfigDefaults();

// Header values as editable settings, and back. Unlike decodeParamsFor() these
// keep raw values (e.g. a random wavelet -1, a quantization above 255), which
// is what GLIC's skip-header decode uses.
EncodeSettings settingsFromHeader(const DecodeParams& params);
DecodeParams paramsFromSettings(const EncodeSettings& settings);

} // namespace glic
