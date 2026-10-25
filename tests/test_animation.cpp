#include <doctest/doctest.h>

#include "model/animation.hpp"
#include "png_meta.hpp"
#include "render/animation_export.hpp"
#include "render/resize.hpp"

#include <glic/image_io.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using glic::preset::Preset;
using studio::AnimTarget;

namespace {

fs::path freshDir(const std::string& name) {
    fs::path d = fs::temp_directory_path() / ("glic-studio-test-" + name + "-" + std::to_string(::getpid()));
    fs::remove_all(d);
    return d;
}

studio::AnimationProgress waitDone(studio::AnimationExporter& ex) {
    for (int i = 0; i < 6000; i++) {
        auto p = ex.progress();
        if (p.done) return p;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    FAIL("export did not finish");
    return {};
}

std::vector<uint8_t> head(const fs::path& p, size_t n) {
    auto b = glic::readFile(p.string());
    REQUIRE(b);
    return std::vector<uint8_t>(b->begin(), b->begin() + static_cast<std::ptrdiff_t>(std::min(n, b->size())));
}

} // namespace

TEST_CASE("animation: easing curves hit their ends and are monotonic") {
    for (auto e : {studio::Easing::Linear, studio::Easing::EaseIn, studio::Easing::EaseOut, studio::Easing::EaseInOut}) {
        CHECK(studio::ease(e, 0) == doctest::Approx(0));
        CHECK(studio::ease(e, 1) == doctest::Approx(1));
        double prev = 0;
        for (int i = 1; i <= 100; i++) {
            const double v = studio::ease(e, i / 100.0);
            CHECK(v >= prev);
            prev = v;
        }
    }
    CHECK(studio::ease(studio::Easing::EaseIn, 0.5) < 0.5);
    CHECK(studio::ease(studio::Easing::EaseOut, 0.5) > 0.5);
}

TEST_CASE("animation: frame values, rounding, clamping, and frame presets") {
    studio::AnimationSpec s;
    s.target = AnimTarget::Quantization;
    s.start = 0;
    s.end = 300;  // past the range: clamped to 255
    s.frames = 4;
    CHECK(studio::frameValue(s, 0) == 0);
    CHECK(studio::frameValue(s, 1) == 100);
    CHECK(studio::frameValue(s, 3) == 255);

    Preset base;
    s.channel = 1;
    s.end = 90;
    Preset f = studio::framePreset(base, s, 3);
    CHECK(f.encode.channels[1].quantization == 90);
    CHECK(f.encode.channels[0].quantization == base.encode.channels[0].quantization);
    s.channel = -1;
    f = studio::framePreset(base, s, 3);
    for (const auto& c : f.encode.channels) CHECK(c.quantization == 90);

    // Decode side only applies when decoding is unlinked.
    s.decodeSide = true;
    f = studio::framePreset(base, s, 3);
    CHECK(f.encode.channels[0].quantization == 90);
    base.decodeLinked = false;
    f = studio::framePreset(base, s, 3);
    CHECK(f.decode.channels[0].quantization == 90);
    CHECK(f.encode.channels[0].quantization == base.encode.channels[0].quantization);
    CHECK(studio::currentValue(f, s) == 90);

    s = {};
    s.target = AnimTarget::ScaleExp;
    s.start = 10;
    s.end = 20;
    s.frames = 2;
    CHECK(studio::framePreset(Preset{}, s, 1).encode.channels[2].scale == 1 << 20);
    s.target = AnimTarget::CorruptionSeed;
    s.start = 5;
    s.end = 9;
    s.frames = 5;
    f = studio::framePreset(Preset{}, s, 2);
    CHECK(f.corruption.enabled);
    CHECK(f.corruption.seed == 7);
    s.target = AnimTarget::MaxBlockExp;
    s.start = s.end = 1;  // below the default min block: min follows
    f = studio::framePreset(Preset{}, s, 0);
    CHECK(f.encode.channels[0].maxBlockSize == 2);
    CHECK(f.encode.channels[0].minBlockSize == 2);
    CHECK(glic::validate(f.encode).empty());
}

TEST_CASE("animation export: PNG frames embed their own presets") {
    auto img = glic::loadImage(std::string(GLIC_SOURCE_DIR) + "/tests/images/tiny.png");
    REQUIRE(img);
    studio::AnimationJob job;
    job.source = std::make_shared<const glic::Image>(*img);
    job.spec.target = AnimTarget::Quantization;
    job.spec.start = 0;
    job.spec.end = 120;
    job.spec.frames = 5;
    job.format = studio::AnimFormat::PngFrames;
    const fs::path dir = freshDir("png");
    job.output = dir.string();
    studio::AnimationExporter ex;
    REQUIRE(ex.start(job));
    auto p = waitDone(ex);
    REQUIRE_MESSAGE(p.ok, p.message);
    CHECK(p.framesDone == 5);
    for (int i = 0; i < 5; i++) {
        auto bytes = glic::readFile((dir / studio::frameFileName(i)).string());
        REQUIRE(bytes);
        auto embedded = glic::preset::fromPng(*bytes);
        REQUIRE(embedded);
        CHECK(embedded->sameSettings(studio::framePreset(job.base, job.spec, i)));
        // The frame is exactly what that preset renders (cropped to the source).
        auto png = glic::loadImageFromMemory(bytes->data(), bytes->size());
        REQUIRE(png);
        auto direct = glic::preset::render(*img, studio::framePreset(job.base, job.spec, i));
        CHECK(png->pixels == studio::cropTo(direct.image, img->width, img->height).pixels);
    }
    fs::remove_all(dir);
}

TEST_CASE("animation export: ffmpeg arguments, and real MP4/GIF when ffmpeg is installed") {
    auto args = studio::ffmpegArgs("/usr/bin/ffmpeg", studio::AnimFormat::Mp4, "/f", 12, "/o.mp4");
    CHECK(args.front() == "/usr/bin/ffmpeg");
    CHECK(args.back() == "/o.mp4");
    CHECK(std::find(args.begin(), args.end(), "libx264") != args.end());
    CHECK(std::find(args.begin(), args.end(), "/f/frame_%04d.png") != args.end());

    auto ffmpeg = studio::findFfmpeg();
    if (!ffmpeg) {
        MESSAGE("ffmpeg not on PATH; skipping the video export");
        return;
    }
    auto img = glic::loadImage(std::string(GLIC_SOURCE_DIR) + "/tests/images/tiny.png");  // odd sides: 37x23
    REQUIRE(img);
    const fs::path dir = freshDir("video");
    fs::create_directories(dir);
    for (auto format : {studio::AnimFormat::Gif, studio::AnimFormat::Mp4}) {
        studio::AnimationJob job;
        job.source = std::make_shared<const glic::Image>(*img);
        job.spec.target = AnimTarget::CorruptionSeed;
        job.spec.start = 1;
        job.spec.end = 6;
        job.spec.frames = 6;
        job.format = format;
        job.ffmpeg = *ffmpeg;
        job.output = (dir / (format == studio::AnimFormat::Gif ? "a.gif" : "a.mp4")).string();
        studio::AnimationExporter ex;
        REQUIRE(ex.start(job));
        auto p = waitDone(ex);
        REQUIRE_MESSAGE(p.ok, p.message);
        auto h = head(job.output, 12);
        if (format == studio::AnimFormat::Gif) CHECK(std::string(h.begin(), h.begin() + 3) == "GIF");
        else CHECK(std::string(h.begin() + 4, h.begin() + 8) == "ftyp");
    }
    fs::remove_all(dir);
}

TEST_CASE("animation export: cancel stops it and cleans up") {
    auto img = glic::loadImage(std::string(GLIC_SOURCE_DIR) + "/tests/images/photo.png");
    REQUIRE(img);
    studio::AnimationJob job;
    job.source = std::make_shared<const glic::Image>(*img);
    for (auto& ch : job.base.encode.channels) ch.prediction = glic::Prediction::SAD;  // slow
    job.spec.frames = 200;
    job.format = studio::AnimFormat::PngFrames;
    const fs::path dir = freshDir("cancel");
    job.output = dir.string();
    studio::AnimationExporter ex;
    REQUIRE(ex.start(job));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ex.cancel();
    auto p = waitDone(ex);
    CHECK_FALSE(p.ok);
    CHECK(p.cancelled);
    CHECK(p.framesDone < 200);
    CHECK(ex.start(job));  // a new export can start afterwards
    ex.cancel();
    waitDone(ex);
    fs::remove_all(dir);
}
