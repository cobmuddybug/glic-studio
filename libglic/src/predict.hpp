#pragma once

// Port of GLIC's segmentation.pde and predictions.pde.

#include "jvm.hpp"
#include "planes.hpp"

#include <atomic>
#include <vector>

namespace glic {

constexpr int PRED_SAD = -1;
constexpr int PRED_BSAD = -2;
constexpr int PRED_RANDOM = -3;
constexpr int PRED_NONE = 0;
constexpr int PRED_REF = 14;
constexpr int PRED_ANGLE = 15;
constexpr int MAX_PRED = 16;

struct Cancelled {};

// Per-call state that the sketch kept in globals.
struct CodecContext {
    jvm::ProcessingRandom rng;
    const std::atomic<bool>* cancel = nullptr;
    int lastMethod = PRED_NONE;  // leaf predictor most recently applied (for overlays)

    void checkCancel() const {
        if (cancel && cancel->load(std::memory_order_relaxed)) throw Cancelled{};
    }
};

// Bit sink/source for the quadtree split flags.
class BitOutput;
class BitInput;

std::vector<Segment> makeSegmentation(BitOutput& out, const Planes& p, int pno, int minSize, int maxSize,
                                      float thr, CodecContext& ctx);
std::vector<Segment> readSegmentation(BitInput& in, const Planes& p);

// Returns a size*size block, index x*size + y.
std::vector<int> predict(int prediction, Planes& p, int pno, Segment& s, CodecContext& ctx);

} // namespace glic
