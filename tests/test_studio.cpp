#include <doctest/doctest.h>

#include "model/preview_scheduler.hpp"
#include "render/resize.hpp"
#include "render/worker.hpp"

#include <glic/image_io.hpp>

#include <chrono>
#include <thread>

using studio::PreviewScheduler;

TEST_CASE("scheduler: a change gives a proxy now and a full render after settling") {
    PreviewScheduler s;
    s.changed(0, false);
    auto p = s.next(0);
    REQUIRE(p);
    CHECK(p->proxy);
    CHECK_FALSE(s.next(10));  // proxy in flight
    CHECK(s.finished(p->ticket, false));
    CHECK_FALSE(s.next(100));  // not settled yet
    CHECK(s.wakeIn(100) == doctest::Approx(50));
    auto f = s.next(150);
    REQUIRE(f);
    CHECK_FALSE(f->proxy);
    CHECK(s.finished(f->ticket, false));
    CHECK(s.fullIsCurrent());
    CHECK(s.idle());
}

TEST_CASE("scheduler: small images skip the proxy") {
    PreviewScheduler s;
    s.changed(0, true);
    auto f = s.next(0);
    REQUIRE(f);
    CHECK_FALSE(f->proxy);
}

TEST_CASE("scheduler: changes during a proxy coalesce into one follow-up proxy") {
    PreviewScheduler s;
    s.changed(0, false);
    auto p1 = s.next(0);
    for (int t = 10; t < 100; t += 10) {
        s.changed(t, false);
        CHECK_FALSE(s.next(t));  // the running proxy is allowed to finish
    }
    CHECK(s.finished(p1->ticket, false));  // stale but newest on screen: shown
    auto p2 = s.next(100);
    REQUIRE(p2);
    CHECK(p2->proxy);
    CHECK(p2->version == s.version());
}

TEST_CASE("scheduler: a change abandons a full render in flight") {
    PreviewScheduler s;
    s.changed(0, false);
    auto p = s.next(0);
    s.finished(p->ticket, false);
    auto f = s.next(200);
    REQUIRE(f);
    CHECK_FALSE(f->proxy);
    s.changed(210, false);
    auto p2 = s.next(210);  // submitting this cancels the full render
    REQUIRE(p2);
    CHECK(p2->proxy);
    CHECK_FALSE(s.finished(f->ticket, true));  // the abandoned job is ignored
    CHECK(s.finished(p2->ticket, false));
}

TEST_CASE("scheduler: an older result never replaces a newer one; a proxy never replaces a full") {
    PreviewScheduler s;
    s.changed(0, true);
    auto f = s.next(0);
    CHECK(s.finished(f->ticket, false));
    CHECK_FALSE(s.finished(f->ticket, false));  // unknown ticket now
    CHECK(s.fullIsCurrent());
}

TEST_CASE("downscale keeps aspect and averages") {
    glic::Image img;
    img.width = 1000;
    img.height = 500;
    img.pixels.assign(1000 * 500, glic::makeColor(10, 20, 30));
    auto d = studio::downscaleToFit(img, 512);
    CHECK(d.width == 512);
    CHECK(d.height == 256);
    CHECK(d.pixels[1234] == glic::makeColor(10, 20, 30));
    CHECK(studio::downscaleToFit(d, 512).pixels == d.pixels);  // already fits
}

TEST_CASE("worker renders, reports every job, and cancels superseded work") {
    auto img = glic::loadImage(std::string(GLIC_SOURCE_DIR) + "/tests/images/photo.png");
    REQUIRE(img);
    auto source = std::make_shared<const glic::Image>(*img);

    studio::RenderWorker worker;
    auto waitFor = [&](size_t n) {
        std::vector<studio::RenderResult> all;
        for (int i = 0; i < 2000 && all.size() < n; i++) {
            auto r = worker.poll();
            all.insert(all.end(), std::make_move_iterator(r.begin()), std::make_move_iterator(r.end()));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return all;
    };

    studio::RenderRequest slow;
    slow.generation = 1;
    slow.source = source;
    for (auto& ch : slow.encode.channels) ch.prediction = glic::Prediction::SAD;
    worker.submit(slow);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    studio::RenderRequest fast;
    fast.generation = 2;
    fast.source = source;
    fast.proxy = true;
    fast.proxyEdge = 128;
    worker.submit(fast);

    auto results = waitFor(2);
    REQUIRE(results.size() == 2);
    CHECK(results[0].generation == 1);
    CHECK(results[0].cancelled);
    CHECK(results[1].generation == 2);
    CHECK(results[1].ok);
    CHECK(results[1].proxy);
    CHECK(std::max(results[1].sourceWidth, results[1].sourceHeight) == 128);

    // The proxy path is the same deterministic codec on a downscaled image.
    auto direct = glic::encode(studio::downscaleToFit(*img, 128), fast.encode);
    CHECK(results[1].data->bytes == direct.data.bytes);
}

#include "model/params.hpp"

TEST_CASE("params: reset, copy to all, link") {
    glic::EncodeSettings s;
    s.channels[1].quantization = 77;
    s.channels[2].wavelet = 46;
    CHECK_FALSE(studio::channelsIdentical(s));
    studio::copyToAllChannels(s, 1, studio::ChannelParam::Quantization);
    CHECK(s.channels[0].quantization == 77);
    CHECK(s.channels[2].quantization == 77);
    studio::resetParam(s, 2, studio::ChannelParam::Wavelet);
    CHECK(s.channels[2].wavelet == glic::ChannelSettings{}.wavelet);
    studio::linkChannels(s);
    CHECK(studio::channelsIdentical(s));
    CHECK(studio::predictionChoices().size() == 19);
    CHECK(studio::waveletChoices().size() == 69);
    CHECK(studio::waveletChoices()[67].label == "Haar");
    CHECK(studio::log2Floor(1 << 20) == 20);
}

TEST_CASE("worker reuses the encoded data when only decode settings change") {
    auto img = glic::loadImage(std::string(GLIC_SOURCE_DIR) + "/tests/images/small64.png");
    REQUIRE(img);
    auto source = std::make_shared<const glic::Image>(*img);
    studio::RenderWorker worker;
    auto waitOne = [&] {
        for (int i = 0; i < 2000; i++) {
            auto r = worker.poll();
            if (!r.empty()) return r.back();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return studio::RenderResult{};
    };

    studio::RenderRequest req;
    req.generation = 1;
    req.source = source;
    for (auto& ch : req.encode.channels) ch.wavelet = 67;
    worker.submit(req);
    auto first = waitOne();
    REQUIRE(first.ok);
    CHECK_FALSE(first.encodeReused);

    req.generation = 2;
    auto params = glic::decodeParamsFor(req.encode);
    params.channels[0].prediction = static_cast<int>(glic::Prediction::DC);
    req.decode.override = params;
    worker.submit(req);
    auto second = waitOne();
    REQUIRE(second.ok);
    CHECK(second.encodeReused);
    CHECK(second.data == first.data);  // the very same encoded buffer
    CHECK(second.image.pixels == glic::decode(*first.data, req.decode).image.pixels);
    CHECK(second.image.pixels != first.image.pixels);

    req.generation = 3;
    req.encode.channels[1].quantization = 50;  // an encode change encodes again
    worker.submit(req);
    auto third = waitOne();
    REQUIRE(third.ok);
    CHECK_FALSE(third.encodeReused);
}
