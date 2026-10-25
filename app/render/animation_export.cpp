#include "animation_export.hpp"

#include "resize.hpp"

#include <glic/image_io.hpp>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

namespace studio {

namespace fs = std::filesystem;

const char* animFormatName(AnimFormat f) {
    switch (f) {
    case AnimFormat::PngFrames: return "PNG frames";
    case AnimFormat::Mp4: return "MP4";
    case AnimFormat::Gif: return "GIF";
    }
    return "";
}

std::optional<std::string> findFfmpeg() {
    const char* path = std::getenv("PATH");
    if (!path) return std::nullopt;
    std::string p = path;
    for (size_t at = 0; at <= p.size();) {
        size_t colon = p.find(':', at);
        if (colon == std::string::npos) colon = p.size();
        const fs::path candidate = fs::path(p.substr(at, colon - at)) / "ffmpeg";
        if (colon > at && ::access(candidate.c_str(), X_OK) == 0) return candidate.string();
        at = colon + 1;
    }
    return std::nullopt;
}

std::string frameFileName(int i) {
    char name[32];
    std::snprintf(name, sizeof name, "frame_%04d.png", i + 1);
    return name;
}

std::vector<std::string> ffmpegArgs(const std::string& ffmpeg, AnimFormat format, const std::string& framesDir, int fps,
                                    const std::string& output) {
    std::vector<std::string> a = {ffmpeg, "-y", "-hide_banner", "-loglevel", "error", "-framerate", std::to_string(fps),
                                  "-i", (fs::path(framesDir) / "frame_%04d.png").string()};
    if (format == AnimFormat::Gif) {
        // Two-pass palette for decent colours; loops forever.
        a.insert(a.end(), {"-vf", "split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer", "-loop", "0"});
    } else {
        // H.264 needs even sides and 4:2:0 for players to accept it.
        a.insert(a.end(), {"-vf", "pad=ceil(iw/2)*2:ceil(ih/2)*2", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "16",
                           "-movflags", "+faststart"});
    }
    a.push_back(output);
    return a;
}

AnimationExporter::~AnimationExporter() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

bool AnimationExporter::start(AnimationJob job) {
    {
        std::lock_guard lock(mutex_);
        if (progress_.running) return false;
        progress_ = AnimationProgress{};
        progress_.running = true;
        progress_.frames = job.spec.frames;
    }
    if (thread_.joinable()) thread_.join();
    cancel_ = false;
    framesDone_ = 0;
    thread_ = std::thread([this, job = std::move(job)]() mutable { run(std::move(job)); });
    return true;
}

void AnimationExporter::cancel() {
    cancel_ = true;
    if (int pid = childPid_.load(); pid > 0) ::kill(pid, SIGTERM);
}

AnimationProgress AnimationExporter::progress() {
    std::lock_guard lock(mutex_);
    AnimationProgress p = progress_;
    p.framesDone = framesDone_.load();
    return p;
}

bool AnimationExporter::running() {
    std::lock_guard lock(mutex_);
    return progress_.running;
}

void AnimationExporter::finish(bool ok, std::string message) {
    std::lock_guard lock(mutex_);
    progress_.running = false;
    progress_.done = true;
    progress_.ok = ok;
    progress_.cancelled = cancel_.load();
    progress_.encoding = false;
    progress_.message = std::move(message);
}

namespace {

// Runs argv without a shell; ffmpeg's output goes to `log`. Returns the exit
// status (-1 if it could not start).
int runProcess(const std::vector<std::string>& args, const std::string& log, std::atomic<int>& pidOut) {
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    const pid_t pid = ::fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        const int fd = ::open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            ::dup2(fd, 1);
            ::dup2(fd, 2);
            ::close(fd);
        }
        const int in = ::open("/dev/null", O_RDONLY);
        if (in >= 0) ::dup2(in, 0);
        ::execv(argv[0], argv.data());
        ::_exit(127);
    }
    pidOut = pid;
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    pidOut = 0;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string lastLine(const std::string& path) {
    std::string text;
    if (auto bytes = glic::readFile(path)) text.assign(bytes->begin(), bytes->end());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    const size_t nl = text.rfind('\n');
    return nl == std::string::npos ? text : text.substr(nl + 1);
}

} // namespace

void AnimationExporter::run(AnimationJob job) {
    const int frames = std::max(1, job.spec.frames);
    const bool video = job.format != AnimFormat::PngFrames;
    std::error_code ec;
    fs::path dir;
    if (video) {
        char tmpl[] = "/tmp/glic-studio-frames-XXXXXX";
        if (const char* t = std::getenv("TMPDIR")) {
            const std::string s = (fs::path(t) / "glic-studio-frames-XXXXXX").string();
            std::vector<char> buf(s.begin(), s.end());
            buf.push_back('\0');
            if (::mkdtemp(buf.data())) dir = buf.data();
        }
        if (dir.empty() && ::mkdtemp(tmpl)) dir = tmpl;
        if (dir.empty()) return finish(false, "Could not create a temporary directory for the frames");
    } else {
        dir = job.output;
        fs::create_directories(dir, ec);
        if (!fs::is_directory(dir)) return finish(false, "Could not create " + dir.string());
    }

    // Frames in parallel; each thread takes the next frame number.
    std::shared_ptr<const glic::Image> image = job.source;
    if (job.maxEdge > 0) image = std::make_shared<const glic::Image>(downscaleToFit(*job.source, job.maxEdge));
    std::atomic<int> next{0};
    std::atomic<bool> failed{false};
    std::mutex errorMutex;
    std::string error;
    const int threads = job.threads > 0 ? job.threads : std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, 6);
    glic::Hooks hooks;
    hooks.cancel = &cancel_;
    auto worker = [&] {
        for (int i; (i = next++) < frames && !cancel_ && !failed;) {
            const glic::preset::Preset p = framePreset(job.base, job.spec, i);
            auto r = glic::preset::render(*image, p, &hooks);
            if (r.cancelled) return;
            if (!r.ok) {
                std::lock_guard lock(errorMutex);
                error = "Frame " + std::to_string(i + 1) + ": " + r.error;
                failed = true;
                return;
            }
            const auto png = glic::preset::exportPng(cropTo(r.image, image->width, image->height), p);
            if (!glic::writeFile((dir / frameFileName(i)).string(), png)) {
                std::lock_guard lock(errorMutex);
                error = "Could not write " + (dir / frameFileName(i)).string();
                failed = true;
                return;
            }
            framesDone_++;
        }
    };
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    auto cleanup = [&] {
        if (video) fs::remove_all(dir, ec);
    };
    if (cancel_) {
        cleanup();
        return finish(false, "Animation export cancelled");
    }
    if (failed) {
        cleanup();
        return finish(false, "Animation export failed: " + error);
    }
    if (!video) return finish(true, "Wrote " + std::to_string(frames) + " frames to " + dir.string());

    {
        std::lock_guard lock(mutex_);
        progress_.encoding = true;
    }
    const std::string log = (dir / "ffmpeg.log").string();
    const int status = runProcess(ffmpegArgs(job.ffmpeg, job.format, dir.string(), job.fps, job.output), log, childPid_);
    if (cancel_) {
        cleanup();
        fs::remove(job.output, ec);
        return finish(false, "Animation export cancelled");
    }
    if (status != 0) {
        // Keep the frames: they are the work, and the log says what went wrong.
        return finish(false, "ffmpeg failed (" + lastLine(log) + "); frames kept in " + dir.string());
    }
    cleanup();
    finish(true, "Exported " + fs::path(job.output).filename().string() + " (" + std::to_string(frames) + " frames)");
}

} // namespace studio
