#include <doctest/doctest.h>

#include <glic/glic.hpp>
#include <glic/image_io.hpp>

#include <atomic>
#include <thread>

namespace {

glic::Image testImage(const char* stem) {
    auto img = glic::loadImage(std::string(GLIC_SOURCE_DIR) + "/tests/images/" + stem + ".png");
    REQUIRE(img);
    return *img;
}

glic::EncodeSettings withPrediction(glic::PredictionMethod m) {
    glic::EncodeSettings s;
    for (auto& ch : s.channels) ch.predictionMethod = m;
    return s;
}

} // namespace

TEST_CASE("encode is deterministic within one process") {
    auto img = testImage("small64");
    for (auto m : {glic::PredictionMethod::PAETH, glic::PredictionMethod::REF, glic::PredictionMethod::SAD,
                   glic::PredictionMethod::BSAD, glic::PredictionMethod::RANDOM, glic::PredictionMethod::ANGLE}) {
        CAPTURE(static_cast<int>(m));
        auto a = glic::encode(img, withPrediction(m));
        auto b = glic::encode(img, withPrediction(m));
        REQUIRE(a.ok);
        REQUIRE(b.ok);
        CHECK(a.data.serialize() == b.data.serialize());
        auto da = glic::decode(a.data);
        auto db = glic::decode(a.data);
        REQUIRE(da.ok);
        CHECK(da.image.pixels == db.image.pixels);
    }
}

TEST_CASE("modes that crash upstream (D1) now encode and decode") {
    auto img = testImage("photo");
    for (auto m : {glic::PredictionMethod::REF, glic::PredictionMethod::SAD, glic::PredictionMethod::BSAD,
                   glic::PredictionMethod::RANDOM}) {
        CAPTURE(static_cast<int>(m));
        auto r = glic::encode(img, withPrediction(m));
        REQUIRE(r.ok);
        CHECK(glic::decode(r.data).ok);
    }
}

TEST_CASE("in-memory decode equals decode of the serialized file") {
    auto img = testImage("flat");
    glic::EncodeSettings s;
    s.colorSpace = glic::ColorSpace::YUV;
    for (auto& ch : s.channels) ch.quantizationValue = 300;  // wraps to 44 in the header
    auto enc = glic::encode(img, s);
    REQUIRE(enc.ok);
    auto parsed = glic::GlicData::parse(enc.data.serialize());
    REQUIRE(parsed);
    CHECK(parsed->params == enc.data.params);
    CHECK(parsed->serialize() == enc.data.serialize());
    CHECK(glic::decode(*parsed).image.pixels == glic::decode(enc.data).image.pixels);
}

TEST_CASE("decode override equal to the header changes nothing") {
    auto img = testImage("small64");
    auto enc = glic::encode(img, glic::EncodeSettings{});
    REQUIRE(enc.ok);
    glic::DecodeSettings ds;
    ds.override = enc.data.params;
    CHECK(glic::decode(enc.data, ds).image.pixels == glic::decode(enc.data).image.pixels);
}

// Expected to fail on the glic-cpp codec: with default settings the photo
// decodes to near-flat grey (internals D3 + D14), so changing the decode
// predictor has nothing to act on. Flips to passing once the codec is fixed.
TEST_CASE("a mismatched decode changes the image" * doctest::should_fail()) {
    auto img = testImage("small64");
    auto enc = glic::encode(img, glic::EncodeSettings{});
    REQUIRE(enc.ok);
    glic::DecodeSettings ds;
    ds.override = enc.data.params;
    ds.override->channels[0].predictionMethod = glic::PredictionMethod::SPIRAL;
    auto mismatched = glic::decode(enc.data, ds);
    REQUIRE(mismatched.ok);
    CHECK(mismatched.image.pixels != glic::decode(enc.data).image.pixels);
}

TEST_CASE("parse never reads past the buffer") {
    auto img = testImage("small64");
    auto bytes = glic::encode(img, glic::EncodeSettings{}).data.serialize();
    for (size_t len : {size_t{0}, size_t{10}, size_t{159}, size_t{160}, size_t{200}, bytes.size() / 2, bytes.size() - 1}) {
        CAPTURE(len);
        std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + len);
        auto parsed = glic::GlicData::parse(cut);
        if (len < glic::GlicData::kPayloadOffset) {
            CHECK_FALSE(parsed);
        } else {
            REQUIRE(parsed);
            CHECK(glic::decode(*parsed).ok);  // truncated payload still decodes (upstream semantics)
        }
    }
}

TEST_CASE("hostile headers fail cleanly") {
    auto img = testImage("small64");
    auto data = glic::encode(img, glic::EncodeSettings{}).data;

    auto bad = data;
    bad.width = 0;
    CHECK_FALSE(glic::decode(bad).ok);
    bad.width = 1 << 20;
    CHECK_FALSE(glic::decode(bad).ok);

    bad = data;
    bad.params.channels[1].transformScale = 0;
    CHECK_FALSE(glic::decode(bad).ok);
    bad.params.channels[1].transformScale = -5;
    CHECK_FALSE(glic::decode(bad).ok);

    bad = data;
    bad.params.colorSpace = static_cast<glic::ColorSpace>(200);
    bad.params.channels[2].waveletType = static_cast<glic::WaveletType>(99);
    bad.params.channels[2].encodingMethod = static_cast<glic::EncodingMethod>(77);
    bad.params.channels[0].predictionMethod = static_cast<glic::PredictionMethod>(-100);
    CHECK(glic::decode(bad).ok);
}

TEST_CASE("invalid encode settings are rejected, not undefined") {
    auto img = testImage("small64");
    glic::EncodeSettings s;
    s.channels[2].transformScale = 0;
    auto r = glic::encode(img, s);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());

    s = {};
    s.channels[0].maxBlockSize = 0;
    CHECK_FALSE(glic::encode(img, s).ok);

    glic::Image empty;
    CHECK_FALSE(glic::encode(empty, {}).ok);
}

TEST_CASE("cancellation stops encode and decode") {
    auto img = testImage("photo");
    std::atomic<bool> cancel{true};
    glic::Hooks hooks;
    hooks.cancel = &cancel;
    auto r = glic::encode(img, glic::EncodeSettings{}, &hooks);
    CHECK_FALSE(r.ok);
    CHECK(r.cancelled);

    auto enc = glic::encode(img, glic::EncodeSettings{});
    auto d = glic::decode(enc.data, {}, &hooks);
    CHECK(d.cancelled);

    // Cancelling mid-flight from another thread.
    cancel = false;
    std::thread t([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        cancel = true;
    });
    auto slow = glic::encode(img, withPrediction(glic::PredictionMethod::SAD), &hooks);
    t.join();
    CHECK(slow.cancelled);
}

TEST_CASE("hooks capture without changing output") {
    auto img = testImage("flat");
    auto settings = withPrediction(glic::PredictionMethod::SAD);
    glic::Capture cap;
    cap.segments = true;
    cap.values = true;
    glic::Hooks hooks;
    hooks.capture = &cap;

    auto plain = glic::encode(img, settings);
    auto hooked = glic::encode(img, settings, &hooks);
    REQUIRE(hooked.ok);
    CHECK(plain.data.serialize() == hooked.data.serialize());

    CHECK(cap.width == img.width);
    CHECK(cap.rootSize == 512);
    for (int p = 0; p < 3; p++) {
        CHECK_FALSE(cap.blocks[p].empty());
        CHECK(cap.planeValues[p].size() == static_cast<size_t>(img.width) * img.height);
        for (const auto& b : cap.blocks[p]) {
            CHECK(static_cast<int>(b.method) >= 0);  // SAD resolved to a concrete predictor
        }
    }
}

// Expected to fail on the glic-cpp codec: negative values are written into N
// bits and read back without sign extension (internals D13), so the decoder
// does not see what the encoder stored. Flips to passing once fixed.
TEST_CASE("uncorrupted decode sees exactly the encoded values" * doctest::should_fail()) {
    auto img = testImage("flat");
    glic::Capture cap;
    cap.values = true;
    glic::Hooks hooks;
    hooks.capture = &cap;
    auto enc = glic::encode(img, glic::EncodeSettings{}, &hooks);
    REQUIRE(enc.ok);

    glic::Capture dcap;
    dcap.values = true;
    glic::Hooks dhooks;
    dhooks.capture = &dcap;
    REQUIRE(glic::decode(enc.data, {}, &dhooks).ok);
    CHECK(dcap.planeValues == cap.planeValues);
}
