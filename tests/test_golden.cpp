// Golden test: every case in tests/baseline/cases.tsv, run through libglic in
// one process, must reproduce the original GLIC's output exactly:
//   - encode: the .glic bytes are identical;
//   - decode (of GLIC's own .glic, linked or with skip-header overrides): the
//     pixels are identical;
//   - where GLIC's encode fails, ours fails too.

#include <doctest/doctest.h>

#include <glic/glic.hpp>
#include <glic/image_io.hpp>

#include "spec.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

struct Case {
    std::string name, images, encodeSpec, decodeSpec;
    int64_t seed = 0;
};

std::vector<Case> loadCases(const fs::path& tsv) {
    std::vector<Case> cases;
    std::ifstream in(tsv);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        Case c;
        std::string seed;
        std::getline(ss, c.name, '\t');
        std::getline(ss, c.images, '\t');
        std::getline(ss, seed, '\t');
        std::getline(ss, c.encodeSpec, '\t');
        std::getline(ss, c.decodeSpec, '\t');
        c.seed = std::stoll(seed);
        cases.push_back(c);
    }
    return cases;
}

bool appliesTo(const Case& c, const std::string& stem) {
    return c.images == "all" || ("," + c.images + ",").find("," + stem + ",") != std::string::npos;
}

} // namespace

TEST_CASE("libglic reproduces the original GLIC on every baseline case") {
    const fs::path src = GLIC_SOURCE_DIR;
    const fs::path expected = src / "tests/baseline";
    const fs::path glics = GLIC_BASELINE_DIR;  // unpacked glic.tar.xz
    auto cases = loadCases(expected / "cases.tsv");
    REQUIRE(cases.size() > 100);

    std::vector<fs::path> images;
    for (const auto& e : fs::directory_iterator(src / "tests/images"))
        if (e.path().extension() == ".png") images.push_back(e.path());
    std::sort(images.begin(), images.end());

    int encodes = 0, decodes = 0, failures = 0;
    for (const auto& path : images) {
        const std::string stem = path.stem().string();
        auto image = glic::loadImage(path.string());
        REQUIRE(image);

        for (const auto& c : cases) {
            if (!appliesTo(c, stem)) continue;
            CAPTURE(stem);
            CAPTURE(c.name);

            glic::EncodeSettings settings = glic::glicCodecConfigDefaults();
            REQUIRE(glic::applySpec(settings, c.encodeSpec).empty());
            settings.seed = c.seed;
            auto enc = glic::encode(*image, settings);

            if (fs::exists(expected / stem / (c.name + ".encode-fails"))) {
                CHECK_FALSE(enc.ok);
                failures++;
                continue;
            }
            REQUIRE(enc.ok);
            auto javaGlic = glic::readFile((glics / stem / (c.name + ".glic")).string());
            REQUIRE(javaGlic);
            CHECK(enc.data.bytes == *javaGlic);
            encodes++;

            glic::DecodeSettings ds;
            ds.seed = c.seed;
            if (c.decodeSpec != "-") {
                glic::EncodeSettings d = settings;
                REQUIRE(glic::applySpec(d, c.decodeSpec).empty());
                ds.override = glic::paramsFromSettings(d);
            }
            auto dec = glic::decode(glic::GlicData{*javaGlic}, ds);
            if (fs::exists(expected / stem / (c.name + ".decode-fails"))) {
                CHECK_FALSE(dec.ok);
                continue;
            }
            REQUIRE(dec.ok);
            auto javaPng = glic::loadImage((expected / stem / (c.name + ".png")).string());
            REQUIRE(javaPng);
            CHECK(dec.image.width == javaPng->width);
            CHECK(dec.image.height == javaPng->height);
            CHECK(dec.image.pixels == javaPng->pixels);
            decodes++;
        }
    }
    MESSAGE("encodes " << encodes << ", decodes " << decodes << ", expected failures " << failures);
    CHECK(encodes + failures == 220);
}
