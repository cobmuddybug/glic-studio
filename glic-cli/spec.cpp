#include "spec.hpp"

#include <sstream>
#include <vector>

namespace glic {

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream ss(s);
    while (std::getline(ss, cur, sep)) out.push_back(cur);
    return out;
}

} // namespace

EncodeSettings glicCodecConfigDefaults() {
    EncodeSettings s;
    s.colorSpace = ColorSpace::HWB;
    for (auto& c : s.channels) {
        c.minBlockSize = 2;
        c.maxBlockSize = 256;
        c.threshold = 15;
        c.encoding = Encoding::RAW;
        c.prediction = Prediction::PAETH;
        c.quantization = 110;
        c.clamp = ClampMethod::NONE;
        c.transformType = TransformType::FWT;
        c.wavelet = 29;  // Symlet 9
        c.compress = 0;
        c.scale = 20;
    }
    return s;
}

std::string applySpec(EncodeSettings& s, const std::string& spec) {
    std::istringstream words(spec);
    for (std::string kv; words >> kv;) {
        if (kv == "-") continue;
        auto eq = kv.find('=');
        if (eq == std::string::npos) return "expected key=value: " + kv;
        std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
        try {
            if (k == "colorspace") { s.colorSpace = static_cast<ColorSpace>(std::stoi(v)); continue; }
            if (k == "seed") { s.seed = std::stoll(v); continue; }
            if (k == "border") {
                auto rgb = split(v, ',');
                if (rgb.size() != 3) return "border needs r,g,b";
                for (int i = 0; i < 3; i++) s.border[i] = static_cast<uint8_t>(std::stoi(rgb[i]));
                continue;
            }
            auto vals = split(v, ',');
            if (vals.size() != 1 && vals.size() != 3) return "expected 1 or 3 values for " + k;
            for (int ch = 0; ch < 3; ch++) {
                const std::string& x = vals[vals.size() == 3 ? ch : 0];
                auto& c = s.channels[ch];
                if (k == "min") c.minBlockSize = std::stoi(x);
                else if (k == "max") c.maxBlockSize = std::stoi(x);
                else if (k == "thr") c.threshold = std::stof(x);
                else if (k == "enc") c.encoding = static_cast<Encoding>(std::stoi(x));
                else if (k == "pred") c.prediction = static_cast<Prediction>(std::stoi(x));
                else if (k == "quant") c.quantization = std::stoi(x);
                else if (k == "clamp") c.clamp = static_cast<ClampMethod>(std::stoi(x));
                else if (k == "ttype") c.transformType = static_cast<TransformType>(std::stoi(x));
                else if (k == "wavelet") c.wavelet = std::stoi(x);
                else if (k == "compress") c.compress = std::stof(x);
                else if (k == "scale") c.scale = std::stoi(x);
                else return "unknown key " + k;
            }
        } catch (const std::exception&) {
            return "bad value in " + kv;
        }
    }
    return {};
}

EncodeSettings settingsFromHeader(const DecodeParams& params) {
    EncodeSettings s;
    s.colorSpace = static_cast<ColorSpace>(params.colorSpace);
    s.border = params.border;
    for (int p = 0; p < 3; p++) {
        const auto& h = params.channels[p];
        auto& c = s.channels[p];
        c.prediction = static_cast<Prediction>(h.prediction);
        c.quantization = h.quantization;
        c.clamp = static_cast<ClampMethod>(h.clamp);
        c.wavelet = h.wavelet;
        c.transformType = static_cast<TransformType>(h.transformType);
        c.scale = h.scale;
        c.encoding = static_cast<Encoding>(h.encoding);
    }
    return s;
}

DecodeParams paramsFromSettings(const EncodeSettings& s) {
    DecodeParams params;
    params.colorSpace = static_cast<int>(s.colorSpace);
    params.border = s.border;
    for (int p = 0; p < 3; p++) {
        const auto& c = s.channels[p];
        auto& h = params.channels[p];
        h.prediction = static_cast<int>(c.prediction);
        h.quantization = c.quantization;
        h.clamp = static_cast<int>(c.clamp);
        h.wavelet = c.wavelet;
        h.transformType = static_cast<int>(c.transformType);
        h.scale = c.scale;
        h.encoding = static_cast<int>(c.encoding);
    }
    return params;
}

} // namespace glic
