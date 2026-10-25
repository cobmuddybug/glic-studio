#include "batch.hpp"

#include <algorithm>

namespace studio {

BatchRenderer::BatchRenderer(int threads, std::function<void()> onResult) : onResult_(std::move(onResult)) {
    if (threads <= 0) threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 2, 1, 4);
    for (int i = 0; i < threads; i++) threads_.emplace_back([this] { run(); });
}

BatchRenderer::~BatchRenderer() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        if (batch_) batch_->cancelled = true;
    }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
}

void BatchRenderer::submit(uint64_t id, std::shared_ptr<const glic::Image> image, std::vector<glic::preset::Preset> presets) {
    auto b = std::make_shared<Batch>();
    b->id = id;
    b->image = std::move(image);
    b->presets = std::move(presets);
    {
        std::lock_guard lock(mutex_);
        if (batch_) batch_->cancelled = true;
        batch_ = std::move(b);
    }
    cv_.notify_all();
}

void BatchRenderer::cancel() {
    std::lock_guard lock(mutex_);
    if (batch_) batch_->cancelled = true;
    batch_.reset();
}

std::vector<BatchResult> BatchRenderer::poll() {
    std::lock_guard lock(mutex_);
    std::vector<BatchResult> r = std::move(done_);
    done_.clear();
    return r;
}

void BatchRenderer::run() {
    while (true) {
        std::shared_ptr<Batch> b;
        size_t index = 0;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || (batch_ && batch_->next < batch_->presets.size()); });
            if (stop_) return;
            b = batch_;
            index = b->next++;
        }
        glic::Hooks hooks;
        hooks.cancel = &b->cancelled;
        auto r = glic::preset::render(*b->image, b->presets[index], &hooks);
        if (r.cancelled) continue;
        BatchResult res;
        res.batch = b->id;
        res.index = static_cast<int>(index);
        res.ok = r.ok;
        res.error = std::move(r.error);
        res.image = std::move(r.image);
        {
            std::lock_guard lock(mutex_);
            if (b->cancelled) continue;
            done_.push_back(std::move(res));
        }
        if (onResult_) onResult_();
    }
}

} // namespace studio
