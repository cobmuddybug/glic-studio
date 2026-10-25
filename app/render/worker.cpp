#include "worker.hpp"

#include "resize.hpp"

#include <chrono>

namespace studio {

namespace {
double msSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}
} // namespace

RenderWorker::RenderWorker(std::function<void()> onResult)
    : onResult_(std::move(onResult)), thread_([this] { run(); }) {}

RenderWorker::~RenderWorker() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        cancel_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

void RenderWorker::submit(RenderRequest request) {
    {
        std::lock_guard lock(mutex_);
        pending_ = std::move(request);
        cancel_ = true;  // whatever is running is now older than the newest request
    }
    cv_.notify_all();
}

std::vector<RenderResult> RenderWorker::poll() {
    std::lock_guard lock(mutex_);
    std::vector<RenderResult> r = std::move(done_);
    done_.clear();
    return r;
}

void RenderWorker::run() {
    while (true) {
        RenderRequest req;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || pending_.has_value(); });
            if (stop_) return;
            req = std::move(*pending_);
            pending_.reset();
            cancel_ = false;
        }
        busy_ = true;

        RenderResult res;
        res.generation = req.generation;
        res.proxy = req.proxy;

        std::shared_ptr<const glic::Image> input = req.source;
        if (req.proxy && input) {
            if (proxySourceKey_ != input.get() || proxyEdgeKey_ != req.proxyEdge) {
                proxyImage_ = std::make_shared<const glic::Image>(downscaleToFit(*input, req.proxyEdge));
                proxySourceKey_ = input.get();
                proxySourceHold_ = input;  // keep the key alive so the pointer can't be reused
                proxyEdgeKey_ = req.proxyEdge;
            }
            input = proxyImage_;
        }

        glic::Hooks hooks;
        hooks.cancel = &cancel_;
        bool cancelled = false;
        if (!input || input->empty()) {
            res.error = "No image";
        } else {
            res.sourceWidth = input->width;
            res.sourceHeight = input->height;
            EncodeCache& cache = cache_[req.proxy ? 1 : 0];
            std::shared_ptr<const glic::GlicData> data;
            if (cache.data && cache.input == input && cache.settings == req.encode) {
                data = cache.data;
                res.encodeReused = true;
            } else {
                auto t0 = std::chrono::steady_clock::now();
                auto enc = glic::encode(*input, req.encode, &hooks);
                res.encodeMs = msSince(t0);
                if (enc.cancelled) {
                    cancelled = true;
                } else if (!enc.ok) {
                    res.error = enc.error;
                } else {
                    data = std::make_shared<const glic::GlicData>(std::move(enc.data));
                    cache = {input, req.encode, data};
                }
            }
            if (data) {
                // Overlays come from the decode, so they show what is on screen
                // (mismatched predictors, corrupted values) and survive encode reuse.
                std::shared_ptr<glic::Capture> capture;
                glic::Hooks decodeHooks = hooks;
                if (req.captureSegments || req.captureValues) {
                    capture = std::make_shared<glic::Capture>();
                    capture->segments = req.captureSegments;
                    capture->values = req.captureValues;
                    decodeHooks.capture = capture.get();
                }
                auto t1 = std::chrono::steady_clock::now();
                const glic::GlicData* toDecode = data.get();
                glic::GlicData damaged;
                glic::DecodeSettings decode = req.decode;
                if (req.corruption.enabled) {
                    damaged = *data;
                    res.corruption = glic::corrupt(damaged, req.corruption, kCorruptionBins);
                    toDecode = &damaged;
                    glic::limitCorruptDecode(decode, input->width, input->height);
                }
                auto dec = glic::decode(*toDecode, decode, &decodeHooks);
                res.decodeMs = msSince(t1);
                if (dec.cancelled) {
                    cancelled = true;
                } else if (!dec.ok) {
                    res.error = dec.error;
                    res.decodeFailed = true;
                } else {
                    res.ok = true;
                    res.image = std::move(dec.image);
                    res.data = std::move(data);
                    res.capture = std::move(capture);
                }
            }
        }

        if (cancelled) {
            res = RenderResult{};
            res.generation = req.generation;
            res.proxy = req.proxy;
            res.cancelled = true;
        }
        {
            std::lock_guard lock(mutex_);
            done_.push_back(std::move(res));
            busy_ = false;
        }
        if (onResult_) onResult_();
    }
}

} // namespace studio
