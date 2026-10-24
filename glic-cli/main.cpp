// glic-cli: glic-cpp's command-line interface on top of libglic.
// Argument handling is upstream's, unchanged (including D5: effect names are
// matched in upper case only, so "--effect scanline" is a no-op).

#include <glic/effects.hpp>
#include <glic/glic.hpp>
#include <glic/image_io.hpp>

#include "args.hpp"

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

using namespace glic;

void printUsage(const char* programName) {
    std::cout << "GLIC - GLitch Image Codec (C++ Version)\n\n";
    std::cout << "Usage:\n";
    std::cout << "  " << programName << " encode <input.png> <output.glic> [options]\n";
    std::cout << "  " << programName << " decode <input.glic> <output.png> [options]\n\n";
    std::cout << "Encode Options:\n";
    std::cout << "  --colorspace <name>      Color space (default: HWB)\n";
    std::cout << "                           Options: RGB, HSB, HWB, OHTA, CMY, XYZ, YXY, LAB, LUV,\n";
    std::cout << "                                    HCL, YUV, YPbPr, YCbCr, YDbDr, GS, R-GGB-G\n";
    std::cout << "  --min-block <size>       Min block size (default: 2)\n";
    std::cout << "  --max-block <size>       Max block size (default: 256)\n";
    std::cout << "  --threshold <value>      Segmentation threshold (default: 15)\n";
    std::cout << "  --prediction <method>    Prediction method (default: PAETH)\n";
    std::cout << "                           Options: NONE, CORNER, H, V, DC, DCMEDIAN, MEDIAN, AVG,\n";
    std::cout << "                                    TRUEMOTION, PAETH, LDIAG, HV, JPEGLS, DIFF,\n";
    std::cout << "                                    REF, ANGLE, SAD, BSAD, RANDOM,\n";
    std::cout << "                                    SPIRAL, NOISE, GRADIENT, MIRROR, WAVE,\n";
    std::cout << "                                    CHECKERBOARD, RADIAL, EDGE\n";
    std::cout << "  --quantization <value>   Quantization value 0-255 (default: 110)\n";
    std::cout << "  --clamp <method>         Clamp method: none, mod256 (default: none)\n";
    std::cout << "  --wavelet <name>         Wavelet type (default: SYMLET8)\n";
    std::cout << "                           Options: NONE, HAAR, DB2-DB10, SYM2-SYM10, COIF1-COIF5\n";
    std::cout << "  --transform <type>       Transform type: fwt, wpt (default: fwt)\n";
    std::cout << "  --scale <value>          Transform scale (default: 20)\n";
    std::cout << "  --encoding <method>      Encoding method (default: packed)\n";
    std::cout << "                           Options: raw, packed, rle, delta, xor, zigzag\n";
    std::cout << "  --border <r,g,b>         Border color RGB (default: 128,128,128)\n";
    std::cout << "\nDecode Options (Post-Effects):\n";
    std::cout << "  --effect <name>          Apply post effect (can be used multiple times)\n";
    std::cout << "                           Options: pixelate, scanline, chromatic, dither,\n";
    std::cout << "                                    posterize, glitch\n";
    std::cout << "  --effect-intensity <n>   Effect intensity 0-100 (default: 50)\n";
    std::cout << "  --effect-blocksize <n>   Effect block size for pixelate/glitch (default: 8)\n";
    std::cout << "  --effect-offset <x,y>    Chromatic aberration offset (default: 2,0)\n";
    std::cout << "  --effect-levels <n>      Posterize levels (default: 4)\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << programName << " encode photo.png glitched.glic\n";
    std::cout << "  " << programName << " encode photo.png glitched.glic --colorspace HWB --prediction SPIRAL\n";
    std::cout << "  " << programName << " decode glitched.glic result.png --effect scanline --effect chromatic\n";
}

int main(int argc, char* argv[]) {
    std::string command, input, output;
    CodecConfig config;
    PostEffectsConfig postEffects;

    if (!parseArgs(argc, argv, command, input, output, config, postEffects)) {
        printUsage(argv[0]);
        return 1;
    }

    std::transform(command.begin(), command.end(), command.begin(), ::tolower);

    if (command == "encode") {
        std::string loadError;
        auto image = loadImage(input, &loadError);
        if (!image) {
            std::cerr << "Failed to load image: " << input << std::endl;
            std::cerr << "Error: Failed to load image: " << input << std::endl;
            return 1;
        }

        std::cout << "Loaded image: " << image->width << "x" << image->height << std::endl;

        auto result = glic::encode(*image, config);
        if (!result.ok) {
            std::cerr << "Error: " << result.error << std::endl;
            return 1;
        }
        if (!writeFile(output, result.data.serialize())) {
            std::cerr << "Error: Failed to open output file" << std::endl;
            return 1;
        }

        std::cout << "Encoded to: " << output << std::endl;
    }
    else if (command == "decode") {
        auto bytes = readFile(input);
        if (!bytes) {
            std::cerr << "Error: Failed to open input file" << std::endl;
            return 1;
        }

        std::string parseError;
        auto data = GlicData::parse(*bytes, &parseError);
        if (!data) {
            std::cerr << "Error: " << parseError << std::endl;
            return 1;
        }

        auto result = glic::decode(*data);
        if (!result.ok) {
            std::cerr << "Error: " << result.error << std::endl;
            return 1;
        }

        if (postEffects.enabled && !postEffects.effects.empty()) {
            applyEffects(result.image.pixels, result.image.width, result.image.height, postEffects.effects);
        }

        if (!saveImage(output, result.image)) {
            std::cerr << "Error: Failed to save image: " << output << std::endl;
            return 1;
        }

        std::cout << "Decoded to: " << output << " (" << result.image.width << "x" << result.image.height << ")" << std::endl;
    }
    else {
        std::cerr << "Error: Unknown command: " << command << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}
