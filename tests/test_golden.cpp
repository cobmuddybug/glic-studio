// In-process golden test: every case in tests/baseline/cases.tsv, run through
// libglic in a single process, must match unmodified glic-cpp byte-for-byte.
// Running them all in one process is the point: upstream's hidden RNG state
// (internals D2) made the second encode in a process differ from the first.

#include <doctest/doctest.h>

#include <glic/effects.hpp>
#include <glic/glic.hpp>
#include <glic/image_io.hpp>

#include "args.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

struct Case {
    std::string name;
    std::string images;
    std::string encodeArgs;
    std::string decodeArgs;
};

std::vector<Case> loadCases(const fs::path& tsv) {
    std::vector<Case> cases;
    std::ifstream in(tsv);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        Case c;
        std::getline(ss, c.name, '\t');
        std::getline(ss, c.images, '\t');
        std::getline(ss, c.encodeArgs, '\t');
        std::getline(ss, c.decodeArgs, '\t');
        if (c.encodeArgs == "-") c.encodeArgs.clear();
        if (c.decodeArgs == "-") c.decodeArgs.clear();
        cases.push_back(c);
    }
    return cases;
}

bool appliesTo(const Case& c, const std::string& stem) {
    return c.images == "all" || ("," + c.images + ",").find("," + stem + ",") != std::string::npos;
}

// Feed "glic <command> in out <args...>" through the CLI's own parser.
void parse(const std::string& command, const std::string& args, glic::CodecConfig& config,
           glic::PostEffectsConfig& effects) {
    std::vector<std::string> words = {"glic", command, "in", "out"};
    std::istringstream ss(args);
    for (std::string w; ss >> w;) words.push_back(w);
    std::vector<char*> argv;
    for (auto& w : words) argv.push_back(w.data());
    std::string cmd, in, out;
    REQUIRE(glic::parseArgs(static_cast<int>(argv.size()), argv.data(), cmd, in, out, config, effects));
}

} // namespace

TEST_CASE("libglic matches unmodified glic-cpp on every baseline case") {
    const fs::path src = GLIC_SOURCE_DIR;
    const fs::path baseline = GLIC_BASELINE_DIR;
    auto cases = loadCases(src / "tests/baseline/cases.tsv");
    REQUIRE(cases.size() > 30);

    int checked = 0;
    for (const auto& entry : fs::directory_iterator(src / "tests/images")) {
        if (entry.path().extension() != ".png") continue;
        const std::string stem = entry.path().stem().string();
        auto image = glic::loadImage(entry.path().string());
        REQUIRE(image);

        for (const auto& c : cases) {
            if (!appliesTo(c, stem)) continue;
            CAPTURE(stem);
            CAPTURE(c.name);

            glic::CodecConfig config;
            glic::PostEffectsConfig effects;
            parse("encode", c.encodeArgs, config, effects);
            auto enc = glic::encode(*image, config);
            REQUIRE(enc.ok);
            auto bytes = enc.data.serialize();
            auto expected = glic::readFile((baseline / stem / (c.name + ".glic")).string());
            REQUIRE(expected);
            CHECK(bytes == *expected);

            parse("decode", c.decodeArgs, config, effects);
            auto parsed = glic::GlicData::parse(*expected);
            REQUIRE(parsed);
            auto dec = glic::decode(*parsed);
            REQUIRE(dec.ok);
            if (effects.enabled && !effects.effects.empty()) {
                glic::applyEffects(dec.image.pixels, dec.image.width, dec.image.height, effects.effects);
            }
            auto png = glic::encodePng(dec.image);
            auto expectedPng = glic::readFile((src / "tests/baseline" / stem / (c.name + ".png")).string());
            REQUIRE(expectedPng);
            CHECK(png == *expectedPng);
            checked++;
        }
    }
    CHECK(checked == 87);
}
