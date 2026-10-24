#pragma once

#include <glic/config.hpp>

#include <atomic>
#include <climits>
#include <cmath>
#include <random>

namespace glic {

// Thrown internally when Hooks::cancel is set; caught at the API boundary.
struct Cancelled {};

// Per-call codec state. Upstream kept these RNGs as function-static globals that
// were never reset (internals D2); seeding them fresh per call reproduces the
// first-call-in-process behaviour every time.
struct CodecContext {
    std::mt19937 segmentationRng{42};
    std::mt19937 predictionRng{12345};

    // The leaf predictor most recently applied, so overlays can show what
    // RANDOM actually picked.
    PredictionMethod lastMethod = PredictionMethod::NONE;

    const std::atomic<bool>* cancel = nullptr;

    void checkCancel() const {
        if (cancel && cancel->load(std::memory_order_relaxed)) throw Cancelled{};
    }
};

// Float-to-int conversion with x86 cvtt semantics: truncate toward zero, and
// NaN or out-of-range gives INT_MIN. Upstream relied on this through plain
// static_casts, which are undefined behaviour for those inputs.
inline int toInt(double v) {
    if (!(v > static_cast<double>(INT_MIN) - 1.0 && v < static_cast<double>(INT_MAX) + 1.0)) {
        return INT_MIN;
    }
    return static_cast<int>(v);
}

} // namespace glic
