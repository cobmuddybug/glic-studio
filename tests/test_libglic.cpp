#include <doctest/doctest.h>

#include <glic/glic.hpp>
#include <glic/image_io.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace {

glic::Image testImage(const char* stem) {
    auto img = glic::loadImage(std::string(GLIC_SOURCE_DIR) + "/tests/images/" + stem + ".png");
    REQUIRE(img);
    return *img;
}

glic::EncodeSettings with(glic::Prediction pred, glic::WaveletId wavelet = 67) {
    glic::EncodeSettings s;
    for (auto& ch : s.channels) {
        ch.prediction = pred;
        ch.wavelet = wavelet;
        ch.quantization = 30;
    }
    return s;
}

} // namespace

TEST_CASE("encode and decode are deterministic within one process") {
    auto img = testImage("small64");
    for (auto p : {glic::Prediction::PAETH, glic::Prediction::REF, glic::Prediction::SAD, glic::Prediction::BSAD,
                   glic::Prediction::RANDOM, glic::Prediction::ANGLE}) {
        CAPTURE(static_cast<int>(p));
        auto a = glic::encode(img, with(p));
        auto b = glic::encode(img, with(p));
        REQUIRE(a.ok);
        REQUIRE(b.ok);
        CHECK(a.data.bytes == b.data.bytes);
        CHECK(glic::decode(a.data).image.pixels == glic::decode(a.data).image.pixels);
    }
}

TEST_CASE("the seed changes randomised output and nothing else") {
    auto img = testImage("small64");
    auto s = with(glic::Prediction::RANDOM);
    auto a = glic::encode(img, s);
    s.seed = 1234;
    auto b = glic::encode(img, s);
    CHECK(a.data.bytes != b.data.bytes);

    auto fixed = with(glic::Prediction::PAETH, 0);
    for (auto& ch : fixed.channels) ch.minBlockSize = ch.maxBlockSize = 8;  // no sampling decisions
    auto c = glic::encode(img, fixed);
    fixed.seed = 999;
    CHECK(glic::encode(img, fixed).data.bytes == c.data.bytes);
}

TEST_CASE("layout reads the header the way the decoder does") {
    auto img = testImage("tiny");  // 37x23: the decoder works at max(64, n)
    auto settings = with(glic::Prediction::AVG, 0);
    settings.channels[1].quantization = 300;  // stored as one byte
    auto enc = glic::encode(img, settings);
    REQUIRE(enc.ok);
    auto l = enc.data.layout();
    CHECK(l.storedWidth == 37);
    CHECK(l.storedHeight == 23);
    CHECK(l.width == 64);
    CHECK(l.height == 64);
    CHECK(l.params == glic::decodeParamsFor(settings));
    CHECK(l.params.channels[1].quantization == 44);
    CHECK(l.payloadOffset == 224 + 13);
    CHECK(l.data[2].offset + l.data[2].size == enc.data.bytes.size());

    auto dec = glic::decode(enc.data);
    REQUIRE(dec.ok);
    CHECK(dec.image.width == 64);
}

TEST_CASE("an override equal to the header changes nothing; a mismatch changes the image") {
    auto img = testImage("small64");
    auto enc = glic::encode(img, with(glic::Prediction::PAETH));
    REQUIRE(enc.ok);
    glic::DecodeSettings ds;
    ds.override = enc.data.layout().params;
    auto linked = glic::decode(enc.data);
    CHECK(glic::decode(enc.data, ds).image.pixels == linked.image.pixels);

    ds.override->channels[0].prediction = static_cast<int>(glic::Prediction::DC);
    auto mismatched = glic::decode(enc.data, ds);
    REQUIRE(mismatched.ok);
    CHECK(mismatched.image.pixels != linked.image.pixels);
}

TEST_CASE("truncated files never crash and decode like GLIC's reader") {
    auto img = testImage("small64");
    auto bytes = glic::encode(img, with(glic::Prediction::PAETH)).data.bytes;
    for (size_t len : {size_t{0}, size_t{3}, size_t{10}, size_t{127}, size_t{200}, size_t{240}, size_t{1000},
                       bytes.size() / 2, bytes.size() - 1}) {
        CAPTURE(len);
        glic::GlicData cut{std::vector<uint8_t>(bytes.begin(), bytes.begin() + len)};
        (void)cut.layout();
        auto r = glic::decode(cut);
        // Up to the end of the second header GLIC fails; after that it decodes what is there.
        if (len < 224) CHECK_FALSE(r.ok);
        else CHECK(r.ok);
    }
}

TEST_CASE("hostile headers fail cleanly or decode") {
    auto img = testImage("small64");
    auto good = glic::encode(img, with(glic::Prediction::PAETH)).data.bytes;

    auto set32 = [](std::vector<uint8_t>& b, size_t at, uint32_t v) {
        for (int i = 0; i < 4; i++) b[at + i] = static_cast<uint8_t>(v >> (24 - 8 * i));
    };
    // Huge sizes everywhere: the reader zero-pads instead of reading past the end.
    auto b = good;
    for (int i = 0; i < 12; i++) set32(b, 16 + 4 * i, 0x7fffffff);
    CHECK(glic::decode(glic::GlicData{b}).ok);

    // Invalid transform scale with a wavelet: GLIC throws, we report failure.
    b = good;
    set32(b, 128 + 9, 0);
    CHECK_FALSE(glic::decode(glic::GlicData{b}).ok);

    // Out-of-range ids: unknown wavelets/transforms are picked at random.
    b = good;
    b[14] = 200;       // colour space
    b[128 + 7] = 250;  // wavelet ch0
    b[128 + 8] = 9;    // transform type ch0
    b[128 + 4] = 99;   // prediction ch0
    CHECK(glic::decode(glic::GlicData{b}).ok);

    // Width/height wrap the way GLIC's reader wraps them.
    b = good;
    set32(b, 4, 0x80000000u);
    auto r = glic::decode(glic::GlicData{b});
    REQUIRE(r.ok);
    CHECK(r.image.width == 64);

    // The pixel limit refuses big decodes before allocating.
    b = good;
    set32(b, 4, 8000);
    set32(b, 8, 8000);
    glic::DecodeSettings ds;
    ds.maxPixels = 10'000'000;
    CHECK_FALSE(glic::decode(glic::GlicData{b}, ds).ok);
}

TEST_CASE("invalid encode settings are rejected") {
    auto img = testImage("small64");
    auto s = with(glic::Prediction::PAETH);
    s.channels[0].maxBlockSize = 0;  // GLIC would recurse forever
    CHECK_FALSE(glic::encode(img, s).ok);
    CHECK_FALSE(glic::encode(glic::Image{}, glic::EncodeSettings{}).ok);
}

TEST_CASE("cancellation stops encode and decode") {
    auto img = testImage("photo");
    std::atomic<bool> cancel{true};
    glic::Hooks hooks;
    hooks.cancel = &cancel;
    auto r = glic::encode(img, glic::EncodeSettings{}, &hooks);
    CHECK(r.cancelled);

    auto enc = glic::encode(img, glic::EncodeSettings{});
    CHECK(glic::decode(enc.data, {}, &hooks).cancelled);

    cancel = false;
    std::thread t([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        cancel = true;
    });
    auto slow = glic::encode(img, with(glic::Prediction::SAD), &hooks);
    t.join();
    CHECK(slow.cancelled);
}

TEST_CASE("hooks capture without changing output") {
    auto img = testImage("flat");
    auto settings = with(glic::Prediction::SAD);
    glic::Capture cap;
    cap.segments = true;
    cap.values = true;
    glic::Hooks hooks;
    hooks.capture = &cap;

    auto plain = glic::encode(img, settings);
    auto hooked = glic::encode(img, settings, &hooks);
    REQUIRE(hooked.ok);
    CHECK(plain.data.bytes == hooked.data.bytes);
    CHECK(cap.width == img.width);
    CHECK(cap.rootSize == 512);
    for (int p = 0; p < 3; p++) {
        CHECK_FALSE(cap.blocks[p].empty());
        CHECK(cap.planeValues[p].size() == static_cast<size_t>(img.width) * img.height);
        for (const auto& b : cap.blocks[p]) CHECK(b.method >= 0);  // SAD resolved to a real predictor
    }

    // Uncorrupted: the decoder sees exactly the encoded values.
    glic::Capture dcap;
    dcap.values = true;
    glic::Hooks dhooks;
    dhooks.capture = &dcap;
    REQUIRE(glic::decode(hooked.data, {}, &dhooks).ok);
    CHECK(dcap.planeValues == cap.planeValues);
}

TEST_CASE("random corruption never crashes the decoder") {
    // Smoke version of the M5 fuzz requirement; run under ASan/UBSan in the sanitizer build.
    const char* stems[] = {"small64", "tiny"};
    glic::EncodeSettings variants[3] = {with(glic::Prediction::SAD), with(glic::Prediction::ANGLE, 46),
                                        with(glic::Prediction::RANDOM, 0)};
    variants[1].channels[1].encoding = glic::Encoding::RLE;
    variants[2].channels[0].encoding = glic::Encoding::RAW;
    variants[2].channels[2].clamp = glic::ClampMethod::MOD256;

    uint64_t state = 0x9E3779B97F4A7C15ull;
    auto next = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };

    int decoded = 0;
    for (const char* stem : stems) {
        auto img = testImage(stem);
        for (const auto& v : variants) {
            auto enc = glic::encode(img, v);
            REQUIRE(enc.ok);
            for (int iter = 0; iter < 60; iter++) {
                auto bytes = enc.data.bytes;
                const bool header = iter % 4 == 0;  // every 4th run also hits the header
                const size_t lo = header ? 0 : 224;
                int flips = 1 + static_cast<int>(next() % 64);
                for (int f = 0; f < flips; f++) bytes[lo + next() % (bytes.size() - lo)] = static_cast<uint8_t>(next());
                if (iter % 7 == 0) bytes.resize(lo + next() % (bytes.size() - lo));
                glic::DecodeSettings ds;
                ds.maxPixels = 4'000'000;
                ds.seed = static_cast<int64_t>(iter);
                auto r = glic::decode(glic::GlicData{bytes}, ds);
                if (r.ok) {
                    decoded++;
                    CHECK(r.image.pixels.size() == static_cast<size_t>(r.image.width) * r.image.height);
                }
            }
        }
    }
    CHECK(decoded > 0);
}
