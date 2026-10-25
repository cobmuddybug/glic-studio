#pragma once

// Renders a set of presets on one small image in parallel, for the mutate
// grid (design §M6). Submitting a new batch cancels the old one; results come
// back one by one as they finish. Separate from RenderWorker so the main
// preview keeps its own thread.

#include "preset.hpp"

#include <glic/glic.hpp>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace studio {

struct BatchResult {
    uint64_t batch = 0;
    int index = 0;
    bool ok = false;
    std::string error;
    glic::Image image;  // decoded; GLIC pads sides below 64 px
};

class BatchRenderer {
public:
    explicit BatchRenderer(int threads = 0, std::function<void()> onResult = {});
    ~BatchRenderer();
    BatchRenderer(const BatchRenderer&) = delete;
    BatchRenderer& operator=(const BatchRenderer&) = delete;

    // Replaces (and cancels) any batch in progress.
    void submit(uint64_t batch, std::shared_ptr<const glic::Image> image, std::vector<glic::preset::Preset> presets);
    void cancel();
    std::vector<BatchResult> poll();

private:
    struct Batch {
        uint64_t id = 0;
        std::shared_ptr<const glic::Image> image;
        std::vector<glic::preset::Preset> presets;
        size_t next = 0;
        std::atomic<bool> cancelled{false};
    };
    void run();

    std::function<void()> onResult_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::shared_ptr<Batch> batch_;
    std::vector<BatchResult> done_;
    bool stop_ = false;
    std::vector<std::thread> threads_;
};

} // namespace studio
