#include "args.hpp"

#include <cstdio>
#include <string>

namespace glic {

bool parseArgs(int argc, char* argv[], std::string& command, std::string& input,
               std::string& output, CodecConfig& config, PostEffectsConfig& postEffects) {
    if (argc < 4) {
        return false;
    }

    command = argv[1];
    input = argv[2];
    output = argv[3];

    // Default config
    config = CodecConfig();
    postEffects = PostEffectsConfig();

    // Default effect config for new effects
    EffectConfig currentEffect;
    currentEffect.intensity = 50;
    currentEffect.blockSize = 8;
    currentEffect.offsetX = 2;
    currentEffect.offsetY = 0;
    currentEffect.levels = 4;

    // Parse options
    for (int i = 4; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--colorspace" && i + 1 < argc) {
            config.colorSpace = colorSpaceFromName(argv[++i]);
        }
        else if (arg == "--min-block" && i + 1 < argc) {
            int val = std::stoi(argv[++i]);
            for (auto& ch : config.channels) ch.minBlockSize = val;
        }
        else if (arg == "--max-block" && i + 1 < argc) {
            int val = std::stoi(argv[++i]);
            for (auto& ch : config.channels) ch.maxBlockSize = val;
        }
        else if (arg == "--threshold" && i + 1 < argc) {
            float val = std::stof(argv[++i]);
            for (auto& ch : config.channels) ch.segmentationPrecision = val;
        }
        else if (arg == "--prediction" && i + 1 < argc) {
            auto val = predictionFromName(argv[++i]);
            for (auto& ch : config.channels) ch.predictionMethod = val;
        }
        else if (arg == "--quantization" && i + 1 < argc) {
            int val = std::stoi(argv[++i]);
            for (auto& ch : config.channels) ch.quantizationValue = val;
        }
        else if (arg == "--clamp" && i + 1 < argc) {
            std::string val = argv[++i];
            ClampMethod cm = (val == "mod256") ? ClampMethod::MOD256 : ClampMethod::NONE;
            for (auto& ch : config.channels) ch.clampMethod = cm;
        }
        else if (arg == "--wavelet" && i + 1 < argc) {
            auto val = waveletFromName(argv[++i]);
            for (auto& ch : config.channels) ch.waveletType = val;
        }
        else if (arg == "--transform" && i + 1 < argc) {
            std::string val = argv[++i];
            TransformType tt = (val == "wpt") ? TransformType::WPT : TransformType::FWT;
            for (auto& ch : config.channels) ch.transformType = tt;
        }
        else if (arg == "--scale" && i + 1 < argc) {
            int val = std::stoi(argv[++i]);
            for (auto& ch : config.channels) ch.transformScale = val;
        }
        else if (arg == "--encoding" && i + 1 < argc) {
            auto val = encodingFromName(argv[++i]);
            for (auto& ch : config.channels) ch.encodingMethod = val;
        }
        else if (arg == "--border" && i + 1 < argc) {
            std::string colorStr = argv[++i];
            int r = 128, g = 128, b = 128;
            if (sscanf(colorStr.c_str(), "%d,%d,%d", &r, &g, &b) == 3) {
                config.borderColorR = static_cast<uint8_t>(r);
                config.borderColorG = static_cast<uint8_t>(g);
                config.borderColorB = static_cast<uint8_t>(b);
            }
        }
        // Post-effect options
        else if (arg == "--effect" && i + 1 < argc) {
            std::string effectName = argv[++i];
            EffectConfig effect = currentEffect;
            effect.type = effectFromName(effectName);
            if (effect.type != EffectType::NONE) {
                postEffects.effects.push_back(effect);
                postEffects.enabled = true;
            }
        }
        else if (arg == "--effect-intensity" && i + 1 < argc) {
            currentEffect.intensity = std::stoi(argv[++i]);
        }
        else if (arg == "--effect-blocksize" && i + 1 < argc) {
            currentEffect.blockSize = std::stoi(argv[++i]);
        }
        else if (arg == "--effect-offset" && i + 1 < argc) {
            std::string offsetStr = argv[++i];
            int x = 2, y = 0;
            if (sscanf(offsetStr.c_str(), "%d,%d", &x, &y) >= 1) {
                currentEffect.offsetX = x;
                currentEffect.offsetY = y;
            }
        }
        else if (arg == "--effect-levels" && i + 1 < argc) {
            currentEffect.levels = std::stoi(argv[++i]);
        }
        else if (arg == "--help" || arg == "-h") {
            return false;
        }
    }

    return true;
}

} // namespace glic
