#pragma once

// The one background thread that runs GLIC. The UI submits requests and polls
// for results; it never waits on the codec.
//
// Each request carries a generation (ticket) number. Submitting a request
// replaces anything still queued and cancels the render in progress. Every job
// that starts is reported back, cancelled ones included, so the scheduler
// always knows what is in flight; cancelled results carry no image.

#include <glic/corrupt.hpp>
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
    glic::CorruptionSettings corruption;  // applied to a copy of the encoded bytes
    bool proxy = false;   // render a downscaled proxy
    int proxyEdge = 512;  // longest edge of the proxy
    // Overlay data to capture from the decode (design §M4). Off = no cost.
    bool captureSegments = false;
    bool captureValues = false;
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
    bool encodeReused = false;
    bool decodeFailed = false;  // encoding worked; decoding (e.g. corrupted data) did not  // decode-only change: the cached GlicData was decoded again
    std::shared_ptr<const glic::GlicData> data;    // as encoded (never corrupted)
    std::shared_ptr<const glic::Capture> capture;
    glic::CorruptionStats corruption;  // histogram has kCorruptionBins bins when corruption is on  // set when the request asked for any
};

constexpr int kCorruptionBins = 512;

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

    // Last encode, for proxy and full size separately: a request whose encode
    // inputs match decodes the cached data instead of encoding again (§4.3).
    struct EncodeCache {
        std::shared_ptr<const glic::Image> input;
        glic::EncodeSettings settings;
        std::shared_ptr<const glic::GlicData> data;
    };
    EncodeCache cache_[2];  // [0] full, [1] proxy

    std::thread thread_;
};

} // namespace studio
