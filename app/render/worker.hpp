#pragma once

// The one background thread that runs GLIC. The UI submits requests and polls
// for results; it never waits on the codec.
//
// Each request carries a generation (ticket) number. Submitting a request
// replaces anything still queued and cancels the render in progress. Every job
// that starts is reported back, cancelled ones included, so the scheduler
// always knows what is in flight; cancelled results carry no image.

#include <glic/glic.hpp>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace studio {

struct RenderRequest {
    uint64_t generation = 0;
    std::shared_ptr<const glic::Image> source;  // full-resolution source
    glic::EncodeSettings encode;
    glic::DecodeSettings decode;
    bool proxy = false;   // render a downscaled proxy
    int proxyEdge = 512;  // longest edge of the proxy
};

struct RenderResult {
    uint64_t generation = 0;
    bool proxy = false;
    bool cancelled = false;
    bool ok = false;
    std::string error;
    glic::Image image;             // decoded, at proxy or full size (GLIC pads sides < 64)
    int sourceWidth = 0, sourceHeight = 0;  // size of the image that was encoded
    double encodeMs = 0, decodeMs = 0;
    std::shared_ptr<const glic::GlicData> data;
};

class RenderWorker {
public:
    // `onResult` is called on the worker thread after a result is stored
    // (e.g. to wake the UI loop). It must not block.
    explicit RenderWorker(std::function<void()> onResult = {});
    ~RenderWorker();

    RenderWorker(const RenderWorker&) = delete;
    RenderWorker& operator=(const RenderWorker&) = delete;

    // Replaces any queued request; cancels the current render if it is older.
    void submit(RenderRequest request);

    // Takes all finished jobs, oldest first.
    std::vector<RenderResult> poll();

    bool busy() const { return busy_.load(); }

private:
    void run();

    std::function<void()> onResult_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<RenderRequest> pending_;
    std::vector<RenderResult> done_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> busy_{false};
    bool stop_ = false;

    // Proxy cache: the downscaled source for the last (source, edge) pair.
    const glic::Image* proxySourceKey_ = nullptr;
    int proxyEdgeKey_ = 0;
    std::shared_ptr<const glic::Image> proxyImage_;
    std::shared_ptr<const glic::Image> proxySourceHold_;

    std::thread thread_;
};

} // namespace studio
