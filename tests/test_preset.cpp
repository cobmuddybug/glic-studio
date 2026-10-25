#include <doctest/doctest.h>

#include "png_meta.hpp"
#include "preset.hpp"
#include "render/worker.hpp"

#include <glic/image_io.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;
using namespace glic;

namespace {

const char* kSample = R"({
  "format": "glic-studio-preset",
  "version": 1,
  "name": "sample",
  "future_field": {"keep": [1, 2, 3]},
  "encode": {
    "colorspace": "hwb",
    "border": [10, 20, 30],
    "seed": 42,
    "channels": [
      {"prediction": "SAD", "wavelet": "Haar", "quantization": 40, "scale": 65536, "encoding": "RLE", "my_note": "x"},
      {"prediction": 15, "wavelet": 46, "transform": "WPT", "clamp": "mod256", "min_block": 8, "max_block": 64,
       "threshold": 22.5, "compression": 10},
      {"prediction": "RANDOM", "wavelet": "RANDOM", "transform": "RANDOM"}
    ]
  },
  "decode": {
    "linked": false,
    "colorspace": "LAB",
    "channels": [{"prediction": "DC", "wavelet": "Daubechies 4"}]
  }
})";

} // namespace

TEST_CASE("preset: parse names and ids, defaults, decode section") {
    std::string err;
    auto p = preset::parse(kSample, &err);
    REQUIRE_MESSAGE(p, err);
    CHECK(p->name == "sample");
    CHECK(p->encode.colorSpace == ColorSpace::HWB);
    CHECK(p->encode.border == std::array<uint8_t, 3>{10, 20, 30});
    CHECK(p->encode.seed == 42);
    CHECK(p->encode.channels[0].prediction == Prediction::SAD);
    CHECK(p->encode.channels[0].wavelet == 67);
    CHECK(p->encode.channels[0].encoding == Encoding::RLE);
    CHECK(p->encode.channels[0].minBlockSize == ChannelSettings{}.minBlockSize);  // unspecified: default
    CHECK(p->encode.channels[1].prediction == Prediction::ANGLE);
    CHECK(p->encode.channels[1].transformType == TransformType::WPT);
    CHECK(p->encode.channels[1].clamp == ClampMethod::MOD256);
    CHECK(p->encode.channels[1].threshold == 22.5f);
    CHECK(p->encode.channels[2].wavelet == kWaveletRandom);
    CHECK_FALSE(p->decodeLinked);
    CHECK(p->decode.colorSpace == ColorSpace::LAB);
    for (int c = 0; c < 3; c++) {
        CHECK(p->decode.channels[c].prediction == Prediction::DC);  // one entry applies to all
        CHECK(p->decode.channels[c].wavelet == 46);
    }
    auto ds = preset::decodeSettings(*p);
    REQUIRE(ds.override);
    CHECK(ds.seed == 42);
}

TEST_CASE("preset: load -> save -> load is identical and keeps unknown fields") {
    auto a = preset::parse(kSample);
    REQUIRE(a);
    std::string text = preset::serialize(*a);
    auto b = preset::parse(text);
    REQUIRE(b);
    CHECK(a->sameSettings(*b));
    CHECK(b->name == a->name);
    CHECK(preset::serialize(*b) == text);  // stable
    auto doc = nlohmann::json::parse(text);
    CHECK(doc["future_field"]["keep"] == nlohmann::json({1, 2, 3}));
    CHECK(doc["encode"]["channels"][0]["my_note"] == "x");
    CHECK(doc["encode"]["channels"][1]["wavelet"] == "Daubechies 4");  // ids become names
}

TEST_CASE("preset: rejects bad documents with a reason") {
    std::string err;
    CHECK_FALSE(preset::parse("[]", &err));
    CHECK_FALSE(preset::parse(R"({"format": "something-else"})", &err));
    CHECK_FALSE(preset::parse(R"({"format": "glic-studio-preset", "version": 99})", &err));
    CHECK(err.find("newer") != std::string::npos);
    CHECK_FALSE(preset::parse(R"({"format": "glic-studio-preset", "encode": {"channels": [{"wavelet": "Nope"}]}})", &err));
    CHECK(err.find("Nope") != std::string::npos);
    CHECK_FALSE(preset::parse(R"({"format": "glic-studio-preset", "encode": {"channels": [{}, {}]}})", &err));
    // Wrongly typed fields are errors, not exceptions.
    CHECK_FALSE(preset::parse(R"({"format": 5})", &err));
    CHECK_FALSE(preset::parse(R"({"format": "glic-studio-preset", "version": "1"})", &err));
    CHECK_FALSE(preset::parse(R"({"format": "glic-studio-preset", "name": 3})", &err));
    CHECK_FALSE(preset::parse(R"({"format": "glic-studio-preset", "decode": {"linked": "yes"}})", &err));
    CHECK_FALSE(preset::parse(R"({"format": "glic-studio-preset", "encode": {"border": ["a", 0, 0]}})", &err));
}

TEST_CASE("preset: a minimal document means GUI defaults, decode linked") {
    auto p = preset::parse(R"({"format": "glic-studio-preset", "version": 1})");
    REQUIRE(p);
    CHECK(p->encode == EncodeSettings{});
    CHECK(p->decodeLinked);
    CHECK_FALSE(preset::decodeSettings(*p).override);
}

TEST_CASE("png metadata: embed, read back, image still decodes") {
    auto img = loadImage(std::string(GLIC_SOURCE_DIR) + "/tests/images/small64.png");
    REQUIRE(img);
    auto p = preset::parse(kSample);
    REQUIRE(p);
    auto bytes = preset::exportPng(*img, *p);
    REQUIRE(png::isPng(bytes));
    auto back = preset::fromPng(bytes);
    REQUIRE(back);
    CHECK(back->sameSettings(*p));
    auto decoded = loadImageFromMemory(bytes.data(), bytes.size());
    REQUIRE(decoded);
    CHECK(decoded->pixels == img->pixels);

    CHECK_FALSE(preset::fromPng(encodePng(*img)));  // plain PNG: no preset
    CHECK_FALSE(png::readText({1, 2, 3}, "x"));
    auto truncated = std::vector<uint8_t>(bytes.begin(), bytes.begin() + bytes.size() / 2);
    (void)png::readText(truncated, preset::kPngKeyword);  // must not crash
}

TEST_CASE("every bundled preset parses and renders") {
    auto img = loadImage(std::string(GLIC_SOURCE_DIR) + "/tests/images/tiny.png");
    REQUIRE(img);
    int n = 0;
    for (const auto& dir : {"presets/examples", "presets/glic"}) {
        for (const auto& e : fs::directory_iterator(fs::path(GLIC_SOURCE_DIR) / dir)) {
            if (e.path().extension() != ".json") continue;
            CAPTURE(e.path().string());
            std::string err;
            auto p = preset::load(e.path().string(), &err);
            REQUIRE_MESSAGE(p, err);
            auto r = preset::render(*img, *p);
            CHECK_MESSAGE(r.ok, r.error);
            n++;
        }
    }
    CHECK(n == 150);
}

TEST_CASE("parity: the GUI's render path and `glic render` give identical bytes") {
    // The GUI renders with RenderWorker (encode settings + preset::decodeSettings)
    // and exports with preset::exportPng; the CLI uses preset::render. Same bytes.
    auto img = loadImage(std::string(GLIC_SOURCE_DIR) + "/tests/images/photo.png");
    REQUIRE(img);
    auto source = std::make_shared<const Image>(*img);
    studio::RenderWorker worker;
    for (const char* name : {"mismatch-dc", "mod256-split", "random-angles"}) {
        CAPTURE(name);
        auto p = preset::load(std::string(GLIC_SOURCE_DIR) + "/presets/examples/" + name + ".json");
        REQUIRE(p);
        studio::RenderRequest req;
        req.generation = 1;
        req.source = source;
        req.encode = p->encode;
        req.decode = preset::decodeSettings(*p);
        worker.submit(req);
        std::vector<studio::RenderResult> got;
        for (int i = 0; i < 3000 && got.empty(); i++) {
            got = worker.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(got.size() == 1);
        REQUIRE(got[0].ok);
        auto cli = preset::render(*img, *p);
        REQUIRE(cli.ok);
        CHECK(preset::exportPng(got[0].image, *p) == preset::exportPng(cli.image, *p));
    }
}
