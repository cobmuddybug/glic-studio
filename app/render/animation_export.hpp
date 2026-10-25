#pragma once

// Animation export (design §M7): renders every frame (in parallel, at full
// or reduced size) to PNGs that each embed their frame's preset, then, for
// MP4/GIF, runs ffmpeg on them. ffmpeg is an optional external binary, found
// on PATH at runtime and never linked.

#include "model/animation.hpp"

#include <glic/glic.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace studio {

enum class AnimFormat { PngFrames, Mp4, Gif };
const char* animFormatName(AnimFormat f);

struct AnimationJob {
    std::shared_ptr<const glic::Image> source;
    glic::preset::Preset base;
    AnimationSpec spec;
    int maxEdge = 0;  // render at most this long an edge (0 = full size)
    int fps = 24;
    AnimFormat format = AnimFormat::Mp4;
    // PngFrames: the directory to write frame_0001.png... into.
    // Mp4/Gif: the video file; frames go to a temporary directory first.
    std::string output;
    std::string ffmpeg;  // path to ffmpeg (Mp4/Gif)
    int threads = 0;     // 0 = choose
};

struct AnimationProgress {
    bool running = false;
    bool done = false;
    bool ok = false;
    bool cancelled = false;
    int framesDone = 0;
    int frames = 0;
    bool encoding = false;  // ffmpeg is running
    std::string message;    // result or error, for the status bar
};

// ffmpeg's absolute path if it is on PATH.
std::optional<std::string> findFfmpeg();
// The ffmpeg command line (argv) that turns frame_%04d.png in `framesDir` into `output`.
std::vector<std::string> ffmpegArgs(const std::string& ffmpeg, AnimFormat format, const std::string& framesDir,
                                     int fps, const std::string& output);
// The file name of frame `i` (0-based): frame_0001.png, ...
std::string frameFileName(int i);

class AnimationExporter {
public:
    AnimationExporter() = default;
    ~AnimationExporter();
    AnimationExporter(const AnimationExporter&) = delete;
    AnimationExporter& operator=(const AnimationExporter&) = delete;

    // Starts in the background. Returns false if an export is already running.
    bool start(AnimationJob job);
    void cancel();
    AnimationProgress progress();
    bool running();

private:
    void run(AnimationJob job);
    void finish(bool ok, std::string message);

    std::mutex mutex_;
    AnimationProgress progress_;
    std::atomic<bool> cancel_{false};
    std::atomic<int> framesDone_{0};
    std::atomic<int> childPid_{0};
    std::thread thread_;
};

} // namespace studio
