// glic: command-line GLIC encoder/decoder on top of libglic.

#include <glic/glic.hpp>
#include <glic/image_io.hpp>

#include "png_meta.hpp"
#include "preset.hpp"
#include "spec.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>

using namespace glic;

namespace {

void usage() {
    std::cout <<
        R"(glic - GLIC (GLitch Image Codec) encoder/decoder

Usage:
  glic render <in.png> <out.png> --preset <preset.json>   encode + decode, preset embedded in the PNG
  glic encode <in.png> <out.glic> [options]
  glic decode <in.glic> <out.png> [--seed N] [--override "<spec>"] [--preset <preset.json>]

A preset (glic-studio JSON, or a PNG exported by glic-studio) gives exactly the
GUI's output. With encode, --preset sets the encode settings (later --spec/--seed
apply on top); with decode, it sets the decode override and seed.

Encode options (per-channel values: one value, or three comma-separated):
  --spec "<key=value ...>"   settings as key=value pairs (see below)
  --glic-defaults            start from GLIC's CodecConfig defaults instead of its GUI defaults
  --seed N                   random seed (segmentation sampling, REF, RANDOM)

Spec keys and GLIC ids:
  colorspace  0 OHTA 1 RGB 2 CMY 3 HSB 4 XYZ 5 YXY 6 HCL 7 LUV 8 LAB 9 HWB
              10 R-GGB-G 11 YPbPr 12 YCbCr 13 YDbDr 14 Greyscale 15 YUV
  border      r,g,b (colour outside the image)
  min max     block sizes (powers of two, e.g. min=4 max=256)
  thr         segmentation threshold (5..250)
  pred        -1 SAD -2 BSAD -3 RANDOM 0 NONE 1 CORNER 2 H 3 V 4 DC 5 DCMEDIAN
              6 MEDIAN 7 AVG 8 TRUEMOTION 9 PAETH 10 LDIAG 11 HV 12 JPEGLS
              13 DIFF 14 REF 15 ANGLE
  quant       quantization 0..255
  clamp       0 none, 1 mod256
  wavelet     0 none, 1..67 JWave wavelet, -1 random (67 = Haar, 46 = Daubechies 4)
  ttype       0 FWT, 1 WPT, -1 random
  compress    0..255
  scale       transform scale (GLIC's GUI uses 2^2..2^24; default 2^20)
  enc         0 raw, 1 packed, 2 rle

Decode overrides use the same keys for the decode-side values (colorspace,
border, pred, quant, clamp, wavelet, ttype, scale, enc); unspecified values
come from the file's header.

Example:
  glic encode photo.png out.glic --spec "colorspace=9 pred=-1 wavelet=67 quant=40"
  glic decode out.glic out.png --override "pred=4"
)";
}

// A preset from a .json file, or embedded in a PNG exported by glic-studio.
std::optional<preset::Preset> loadAnyPreset(const std::string& path, std::string& err) {
    auto bytes = readFile(path);
    if (!bytes) {
        err = "cannot read " + path;
        return std::nullopt;
    }
    if (png::isPng(*bytes)) return preset::fromPng(*bytes, &err);
    return preset::parse(std::string(bytes->begin(), bytes->end()), &err);
}

int renderCommand(const std::string& input, const std::string& output, int argc, char* argv[]) {
    std::optional<preset::Preset> p;
    for (int i = 4; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--preset" && i + 1 < argc) {
            std::string err;
            p = loadAnyPreset(argv[++i], err);
            if (!p) {
                std::cerr << "Error: " << err << "\n";
                return 1;
            }
        } else {
            std::cerr << "Unknown option: " << a << "\n";
            return 1;
        }
    }
    if (!p) {
        std::cerr << "Error: render needs --preset\n";
        return 1;
    }
    std::string loadError;
    auto image = loadImage(input, &loadError);
    if (!image) {
        std::cerr << "Error: failed to load " << input << ": " << loadError << "\n";
        return 1;
    }
    auto result = preset::render(*image, *p);
    if (!result.ok) {
        std::cerr << "Error: " << result.error << "\n";
        return 1;
    }
    // Same as the GUI's export: cropped to the source size, preset embedded.
    glic::Image out = result.image;
    if (out.width != image->width || out.height != image->height) {
        glic::Image c;
        c.width = std::min(image->width, out.width);
        c.height = std::min(image->height, out.height);
        for (int y = 0; y < c.height; y++)
            c.pixels.insert(c.pixels.end(), out.pixels.begin() + static_cast<size_t>(y) * out.width,
                            out.pixels.begin() + static_cast<size_t>(y) * out.width + c.width);
        out = std::move(c);
    }
    if (!writeFile(output, preset::exportPng(out, *p))) {
        std::cerr << "Error: failed to write " << output << "\n";
        return 1;
    }
    std::cout << "Rendered " << out.width << "x" << out.height << " to " << output << "\n";
    return 0;
}

int encodeCommand(const std::string& input, const std::string& output, int argc, char* argv[]) {
    EncodeSettings settings;
    std::string spec;
    for (int i = 4; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--preset" && i + 1 < argc) {
            std::string err;
            auto p = loadAnyPreset(argv[++i], err);
            if (!p) {
                std::cerr << "Error: " << err << "\n";
                return 1;
            }
            settings = p->encode;
        } else if (a == "--glic-defaults") {
            int64_t seed = settings.seed;
            settings = glicCodecConfigDefaults();
            settings.seed = seed;
        } else if (a == "--spec" && i + 1 < argc) {
            spec += std::string(" ") + argv[++i];
        } else if (a == "--seed" && i + 1 < argc) {
            settings.seed = std::stoll(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << a << "\n";
            return 1;
        }
    }
    if (auto err = applySpec(settings, spec); !err.empty()) {
        std::cerr << "Error: " << err << "\n";
        return 1;
    }
    std::string loadError;
    auto image = loadImage(input, &loadError);
    if (!image) {
        std::cerr << "Error: failed to load " << input << ": " << loadError << "\n";
        return 1;
    }
    auto result = glic::encode(*image, settings);
    if (!result.ok) {
        std::cerr << "Error: " << result.error << "\n";
        return 1;
    }
    if (!writeFile(output, result.data.bytes)) {
        std::cerr << "Error: failed to write " << output << "\n";
        return 1;
    }
    std::cout << "Encoded " << image->width << "x" << image->height << " to " << output << "\n";
    return 0;
}

int decodeCommand(const std::string& input, const std::string& output, int argc, char* argv[]) {
    auto bytes = readFile(input);
    if (!bytes) {
        std::cerr << "Error: failed to read " << input << "\n";
        return 1;
    }
    GlicData data{std::move(*bytes)};
    DecodeSettings ds;
    for (int i = 4; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--seed" && i + 1 < argc) {
            ds.seed = std::stoll(argv[++i]);
        } else if (a == "--override" && i + 1 < argc) {
            EncodeSettings s = settingsFromHeader(data.layout().params);
            if (auto err = applySpec(s, argv[++i]); !err.empty()) {
                std::cerr << "Error: " << err << "\n";
                return 1;
            }
            ds.override = paramsFromSettings(s);
        } else if (a == "--preset" && i + 1 < argc) {
            std::string err;
            auto p = loadAnyPreset(argv[++i], err);
            if (!p) {
                std::cerr << "Error: " << err << "\n";
                return 1;
            }
            ds = preset::decodeSettings(*p);
        } else {
            std::cerr << "Unknown option: " << a << "\n";
            return 1;
        }
    }
    auto result = glic::decode(data, ds);
    if (!result.ok) {
        std::cerr << "Error: " << result.error << "\n";
        return 1;
    }
    if (!saveImage(output, result.image)) {
        std::cerr << "Error: failed to write " << output << "\n";
        return 1;
    }
    std::cout << "Decoded " << result.image.width << "x" << result.image.height << " to " << output << "\n";
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        usage();
        return 1;
    }
    const std::string command = argv[1];
    if (command == "render") return renderCommand(argv[2], argv[3], argc, argv);
    if (command == "encode") return encodeCommand(argv[2], argv[3], argc, argv);
    if (command == "decode") return decodeCommand(argv[2], argv[3], argc, argv);
    usage();
    return 1;
}
