#include <doctest/doctest.h>

#include "preset.hpp"
#include "render/worker.hpp"

#include <glic/corrupt.hpp>
#include <glic/image_io.hpp>

#include <chrono>
#include <thread>

using namespace glic;

namespace {

Image testImage(const char* stem) {
    auto img = loadImage(std::string(GLIC_SOURCE_DIR) + "/tests/images/" + stem + ".png");
    REQUIRE(img);
    return *img;
}

GlicData encoded(const char* stem, const EncodeSettings& s = {}) {
    auto enc = encode(testImage(stem), s);
    REQUIRE(enc.ok);
    return enc.data;
}

size_t differing(const GlicData& a, const GlicData& b, size_t from = 0, size_t to = SIZE_MAX) {
    size_t n = 0;
    for (size_t i = from; i < std::min({a.bytes.size(), b.bytes.size(), to}); i++) n += a.bytes[i] != b.bytes[i];
    return n;
}

} // namespace

TEST_CASE("corruption: disabled or zero rate changes nothing") {
    const GlicData clean = encoded("small64");
    GlicData d = clean;
    CorruptionSettings s;
    s.rate = 0.5;
    CHECK(corrupt(d, s).bytesChanged == 0);  // not enabled
    s.enabled = true;
    s.rate = 0;
    CHECK(corrupt(d, s).bytesChanged == 0);
    CHECK(d.bytes == clean.bytes);
}

TEST_CASE("corruption: deterministic per seed, header protected, rate roughly honoured") {
    const GlicData clean = encoded("photo");
    CorruptionSettings s;
    s.enabled = true;
    s.rate = 0.01;
    GlicData a = clean, b = clean, c = clean;
    auto sa = corrupt(a, s, 64);
    corrupt(b, s);
    CHECK(a.bytes == b.bytes);
    CHECK(sa.bytesChanged == differing(a, clean));
    CHECK(sa.payloadOffset == 237);
    CHECK(differing(a, clean, 0, sa.payloadOffset) == 0);
    size_t inHistogram = 0;
    for (auto h : sa.histogram) inHistogram += h;
    CHECK(inHistogram == sa.bytesChanged);
    CHECK(sa.headerBytesChanged == 0);
    // ~1% of the payload, minus the 1/256 of draws that hit the same value.
    const double expected = 0.01 * (clean.bytes.size() - sa.payloadOffset) * 255 / 256;
    CHECK(sa.bytesChanged > expected * 0.8);
    CHECK(sa.bytesChanged < expected * 1.2);

    s.seed++;
    corrupt(c, s);
    CHECK(c.bytes != a.bytes);

    GlicData h = clean;
    s.protectHeader = false;
    s.rate = 0.5;
    auto sh = corrupt(h, s);
    CHECK(differing(h, clean, 0, 237) > 50);
    CHECK(sh.headerBytesChanged == differing(h, clean, 0, 237));
}

TEST_CASE("corruption: regions are payload fractions and override the base rate") {
    const GlicData clean = encoded("photo");
    const size_t payload = clean.layout().payloadOffset;
    const size_t len = clean.bytes.size() - payload;
    CorruptionSettings s;
    s.enabled = true;
    s.rate = 0;
    s.regions = {{0.5, 0.25, 0.2}, {0.3, 0.35, 0.0}};  // reversed ends are fine; the later region wins
    GlicData d = clean;
    corrupt(d, s);
    const size_t a = payload + static_cast<size_t>(0.25 * len), b = payload + static_cast<size_t>(0.5 * len);
    const size_t q0 = payload + static_cast<size_t>(0.3 * len), q1 = payload + static_cast<size_t>(0.35 * len);
    CHECK(differing(d, clean, 0, a) == 0);
    CHECK(differing(d, clean, b) == 0);
    CHECK(differing(d, clean, q0, q1) == 0);
    CHECK(differing(d, clean, a, q0) > 0);
    CHECK(differing(d, clean, q1, b) > 0);
}

TEST_CASE("corruption: presets round-trip it and render applies it like the GUI") {
    auto p = preset::parse(R"({"format": "glic-studio-preset", "version": 1,
        "encode": {"channels": [{"wavelet": "Haar", "encoding": "RLE"}]},
        "corruption": {"enabled": true, "seed": 7, "mode": "random_bytes", "rate": 0.002,
                       "regions": [{"start": 0.4, "end": 0.55, "rate": 0.05}], "protect_header": true}})");
    REQUIRE(p);
    CHECK(p->corruption.enabled);
    CHECK(p->corruption.seed == 7);
    REQUIRE(p->corruption.regions.size() == 1);
    CHECK(p->corruption.regions[0].rate == 0.05);
    auto again = preset::parse(preset::serialize(*p));
    REQUIRE(again);
    CHECK(again->sameSettings(*p));
    CHECK_FALSE(preset::parse(R"({"format": "glic-studio-preset", "corruption": {"mode": "melt"}})"));

    // The GUI's worker and `glic render` agree on corrupted output.
    auto img = testImage("photo");
    auto cli = preset::render(img, *p);
    REQUIRE(cli.ok);
    CHECK(cli.image.pixels != preset::render(img, *preset::parse(R"({"format": "glic-studio-preset",
        "encode": {"channels": [{"wavelet": "Haar", "encoding": "RLE"}]}})")).image.pixels);

    studio::RenderWorker worker;
    studio::RenderRequest req;
    req.generation = 1;
    req.source = std::make_shared<const Image>(img);
    req.encode = p->encode;
    req.decode = preset::decodeSettings(*p);
    req.corruption = p->corruption;
    worker.submit(req);
    std::vector<studio::RenderResult> got;
    for (int i = 0; i < 3000 && got.empty(); i++) {
        got = worker.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(got.size() == 1);
    REQUIRE(got[0].ok);
    CHECK(got[0].image.pixels == cli.image.pixels);
    CHECK(got[0].corruption.bytesChanged > 0);
    CHECK(got[0].corruption.histogram.size() == studio::kCorruptionBins);
    // The cached encode stays clean.
    CHECK(got[0].data->bytes == encode(img, p->encode).data.bytes);
}

// Design §8: 1,000 seeds at high rates, no crashes, hangs or memory errors.
// A separate ctest entry (corruption_fuzz); run it in the ASan build too.
TEST_CASE("fuzz: 1000 corruption seeds at high rates" * doctest::test_suite("fuzz")) {
    struct Variant {
        const char* stem;
        EncodeSettings settings;
    };
    std::vector<Variant> variants;
    for (const char* stem : {"small64", "tiny", "gradient"}) {
        EncodeSettings a;  // defaults: AVG, packed, no wavelet
        EncodeSettings b;
        for (auto& ch : b.channels) {
            ch.prediction = Prediction::SAD;
            ch.wavelet = 46;
            ch.encoding = Encoding::RLE;
            ch.quantization = 40;
        }
        EncodeSettings c;
        c.colorSpace = ColorSpace::LAB;
        c.channels[0].prediction = Prediction::REF;
        c.channels[0].encoding = Encoding::RAW;
        c.channels[1].prediction = Prediction::ANGLE;
        c.channels[1].transformType = TransformType::WPT;
        c.channels[1].wavelet = 67;
        c.channels[2].clamp = ClampMethod::MOD256;
        c.channels[2].prediction = Prediction::RANDOM;
        variants.push_back({stem, a});
        variants.push_back({stem, b});
        variants.push_back({stem, c});
    }
    std::vector<GlicData> clean;
    for (const auto& v : variants) clean.push_back(encoded(v.stem, v.settings));

    const double rates[] = {0.001, 0.01, 0.05, 0.2, 0.5};
    int decoded = 0, failed = 0;
    double slowest = 0;
    for (int seed = 0; seed < 1000; seed++) {
        const size_t v = static_cast<size_t>(seed) % variants.size();
        CorruptionSettings s;
        s.enabled = true;
        s.seed = seed;
        s.rate = rates[seed % 5];
        s.protectHeader = seed % 3 != 0;
        if (seed % 4 == 0) s.regions = {{0.1, 0.2, 0.9}};
        GlicData d = clean[v];
        corrupt(d, s);
        DecodeSettings ds;
        ds.seed = seed;
        limitCorruptDecode(ds, 64, 64);  // as the GUI and CLI bound corrupted decodes
        const auto t0 = std::chrono::steady_clock::now();
        auto r = decode(d, ds);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        slowest = std::max(slowest, ms);
        if (ms > 1000) MESSAGE("slow seed " << seed << " variant " << v << " " << ms << " ms ok=" << r.ok << " " << r.image.width << "x" << r.image.height);
        if (r.ok) {
            decoded++;
            REQUIRE(r.image.pixels.size() == static_cast<size_t>(r.image.width) * r.image.height);
        } else {
            failed++;
            REQUIRE_FALSE(r.error.empty());
        }
    }
    MESSAGE("decoded " << decoded << ", failed cleanly " << failed << ", slowest " << slowest << " ms");
    CHECK(decoded > 500);
    CHECK(slowest < 2000);  // no hangs
}

TEST_CASE("worker: a corrupted file that cannot decode fails cleanly as a decode failure") {
    studio::RenderWorker worker;
    studio::RenderRequest req;
    req.source = std::make_shared<const Image>(testImage("small64"));
    req.corruption.enabled = true;
    req.corruption.protectHeader = false;
    req.corruption.rate = 0.5;
    int failures = 0;
    for (int seed = 0; seed < 40; seed++) {
        req.generation = static_cast<uint64_t>(seed + 1);
        req.corruption.seed = seed;
        worker.submit(req);
        std::vector<studio::RenderResult> got;
        for (int i = 0; i < 3000 && got.empty(); i++) {
            got = worker.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(got.size() == 1);
        if (!got[0].ok) {
            failures++;
            CHECK(got[0].decodeFailed);
            CHECK_FALSE(got[0].error.empty());
            CHECK(got[0].corruption.headerBytesChanged > 0);
        }
    }
    CHECK(failures > 0);  // a mangled header usually claims an oversized image
}
