#include <doctest/doctest.h>

#include "model/explore.hpp"
#include "model/history.hpp"
#include "model/params.hpp"
#include "render/batch.hpp"
#include "render/resize.hpp"

#include <glic/image_io.hpp>

#include <chrono>
#include <thread>

using glic::preset::Preset;

namespace {

Preset withQuant(int q) {
    Preset p;
    for (auto& ch : p.encode.channels) ch.quantization = q;
    return p;
}

} // namespace

TEST_CASE("explore: the generator is deterministic and in range") {
    studio::ExploreRng a(7), b(7);
    for (int i = 0; i < 100; i++) CHECK(a.next() == b.next());
    studio::ExploreRng r;
    for (int i = 0; i < 1000; i++) {
        const double u = r.uniform();
        CHECK(u >= 0);
        CHECK(u < 1);
        const int v = r.range(-3, 5);
        CHECK(v >= -3);
        CHECK(v <= 5);
    }
}

TEST_CASE("explore: randomize stays inside the documented ranges and is reproducible") {
    studio::ExploreRng r1(1), r2(1);
    for (int i = 0; i < 300; i++) {
        Preset p, q;
        p.decodeLinked = q.decodeLinked = i % 2 == 0;
        studio::randomize(p, r1);
        studio::randomize(q, r2);
        CHECK(p.sameSettings(q));
        CHECK(glic::validate(p.encode).empty());
        for (const auto& c : p.encode.channels) {
            CHECK(c.minBlockSize >= 2);
            CHECK(c.minBlockSize <= 16);
            CHECK(c.maxBlockSize >= c.minBlockSize);
            CHECK(c.maxBlockSize <= 256);
            CHECK(c.threshold >= 5);
            CHECK(c.threshold <= 60);
            CHECK(c.quantization <= 120);
            CHECK(c.compress <= 80);
            if (c.wavelet != glic::kWaveletNone) CHECK(c.scale >= (1 << 8));
            CHECK(c.wavelet != glic::kWaveletRandom);
        }
        if (!p.decodeLinked)
            for (const auto& c : p.decode.channels) CHECK(static_cast<int>(c.prediction) >= 0);
    }
}

TEST_CASE("explore: mutate changes something, more with strength, keeps linked channels linked") {
    Preset base;
    base.encode.channels[0].wavelet = base.encode.channels[1].wavelet = base.encode.channels[2].wavelet = 67;
    CHECK(studio::channelsIdentical(base.encode));
    studio::ExploreRng rng(3);
    CHECK(studio::mutate(base, 0, rng).sameSettings(base));

    auto changes = [](const Preset& a, const Preset& b) {
        int n = a.encode.colorSpace != b.encode.colorSpace;
        const auto &x = a.encode.channels[0], &y = b.encode.channels[0];
        n += x.minBlockSize != y.minBlockSize;
        n += x.maxBlockSize != y.maxBlockSize;
        n += x.threshold != y.threshold;
        n += x.prediction != y.prediction;
        n += x.quantization != y.quantization;
        n += x.clamp != y.clamp;
        n += x.wavelet != y.wavelet;
        n += x.transformType != y.transformType;
        n += x.compress != y.compress;
        n += x.scale != y.scale;
        n += x.encoding != y.encoding;
        return n;
    };
    int weak = 0, strong = 0;
    for (int i = 0; i < 200; i++) {
        Preset w = studio::mutate(base, 0.1f, rng);
        Preset s = studio::mutate(base, 1.0f, rng);
        CHECK_FALSE(w.sameSettings(base));
        CHECK(studio::channelsIdentical(w.encode));
        CHECK(glic::validate(s.encode).empty());
        weak += changes(base, w);
        strong += changes(base, s);
    }
    CHECK(strong > weak * 2);
}

TEST_CASE("history: branches, undo/redo along the branch, jump") {
    studio::History h;
    CHECK(h.record(withQuant(0), "open"));
    CHECK_FALSE(h.record(withQuant(0), "same"));  // no-op change
    h.record(withQuant(10), "edit");               // node 1
    h.record(withQuant(20), "edit");               // node 2
    REQUIRE(h.size() == 3);
    CHECK(h.undo()->encode.channels[0].quantization == 10);
    CHECK(h.undo()->encode.channels[0].quantization == 0);
    CHECK_FALSE(h.undo());
    CHECK(h.redo()->encode.channels[0].quantization == 10);
    // A change after an undo starts a branch; the old one survives.
    h.record(withQuant(30), "edit");               // node 3, sibling of node 2
    CHECK(h.node(1).children.size() == 2);
    CHECK(h.mainLine() == std::vector<int>{0, 1, 3});
    CHECK_FALSE(h.canRedo());
    CHECK(h.jumpTo(2)->encode.channels[0].quantization == 20);
    CHECK(h.mainLine() == std::vector<int>{0, 1, 2});
    h.undo();
    CHECK(h.redo()->encode.channels[0].quantization == 20);  // redo retraces the jumped-to branch
    h.undo();
    h.undo();
    CHECK(h.mainLine() == std::vector<int>{0, 1, 2});  // preferred children stay
}

TEST_CASE("batch renderer: renders every preset, matching preset::render, and cancels") {
    auto img = glic::loadImage(std::string(GLIC_SOURCE_DIR) + "/tests/images/photo.png");
    REQUIRE(img);
    auto small = std::make_shared<const glic::Image>(studio::downscaleToFit(*img, 96));
    std::vector<Preset> presets;
    studio::ExploreRng rng(9);
    for (int i = 0; i < 9; i++) {
        Preset p;
        studio::randomize(p, rng);
        presets.push_back(p);
    }
    studio::BatchRenderer batch(3);
    batch.submit(1, small, presets);
    std::vector<studio::BatchResult> got;
    for (int i = 0; i < 5000 && got.size() < 9; i++) {
        auto r = batch.poll();
        got.insert(got.end(), r.begin(), r.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(got.size() == 9);
    for (const auto& r : got) {
        CHECK(r.batch == 1);
        auto direct = glic::preset::render(*small, presets[static_cast<size_t>(r.index)]);
        CHECK(r.ok == direct.ok);
        CHECK(r.image.pixels == direct.image.pixels);
    }

    // A slow batch replaced by another: only the new batch reports.
    std::vector<Preset> slow(9);
    for (auto& p : slow)
        for (auto& ch : p.encode.channels) ch.prediction = glic::Prediction::SAD;
    batch.submit(2, std::make_shared<const glic::Image>(*img), slow);
    batch.submit(3, small, {presets[0]});
    std::vector<studio::BatchResult> after;
    for (int i = 0; i < 5000; i++) {
        auto r = batch.poll();
        after.insert(after.end(), r.begin(), r.end());
        if (std::any_of(after.begin(), after.end(), [](const auto& x) { return x.batch == 3; })) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    for (const auto& r : after) CHECK(r.batch == 3);
}
