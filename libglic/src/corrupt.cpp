#include <glic/corrupt.hpp>

#include <algorithm>
#include <random>

namespace glic {

namespace {

// Probability -> threshold on a 53-bit draw. Exact IEEE arithmetic, so the
// same on every machine.
uint64_t threshold(double rate) {
    if (!(rate > 0)) return 0;
    if (rate >= 1) return uint64_t(1) << 53;
    return static_cast<uint64_t>(rate * 9007199254740992.0);  // 2^53
}

struct Run {
    size_t from, to;
    uint64_t thr;
};

// Paints [from, to) with `thr` over a sorted, gap-free list of runs.
void paint(std::vector<Run>& runs, size_t from, size_t to, uint64_t thr) {
    if (from >= to) return;
    std::vector<Run> out;
    for (const Run& r : runs) {
        if (r.to <= from || r.from >= to) {
            out.push_back(r);
            continue;
        }
        if (r.from < from) out.push_back({r.from, from, r.thr});
        if (r.to > to) out.push_back({to, r.to, r.thr});
    }
    out.push_back({from, to, thr});
    std::sort(out.begin(), out.end(), [](const Run& a, const Run& b) { return a.from < b.from; });
    runs = std::move(out);
}

} // namespace

CorruptionStats corrupt(GlicData& data, const CorruptionSettings& s, int histogramBins) {
    CorruptionStats stats;
    std::vector<uint8_t>& b = data.bytes;
    stats.payloadOffset = std::min(data.layout().payloadOffset, b.size());
    stats.fileSize = b.size();
    if (histogramBins > 0) stats.histogram.assign(static_cast<size_t>(histogramBins), 0);
    if (!s.enabled || b.empty()) return stats;

    // Thresholds as runs: the base rate, the header protected, regions painted over.
    const size_t payload = stats.payloadOffset;
    const size_t payloadLen = b.size() - payload;
    std::vector<Run> runs = {{0, b.size(), threshold(s.rate)}};
    if (s.protectHeader) paint(runs, 0, payload, 0);
    for (const CorruptRegion& r : s.regions) {
        const double lo = std::clamp(std::min(r.start, r.end), 0.0, 1.0);
        const double hi = std::clamp(std::max(r.start, r.end), 0.0, 1.0);
        paint(runs, payload + static_cast<size_t>(lo * static_cast<double>(payloadLen)),
              payload + static_cast<size_t>(hi * static_cast<double>(payloadLen)), threshold(r.rate));
    }

    std::mt19937_64 rng(static_cast<uint64_t>(s.seed));
    for (const Run& run : runs) {
        if (run.thr == 0) continue;
        for (size_t i = run.from; i < run.to; i++) {
            if ((rng() >> 11) >= run.thr) continue;
            const auto v = static_cast<uint8_t>(rng() >> 56);
            if (v == b[i]) continue;
            b[i] = v;
            stats.bytesChanged++;
            if (i < payload) stats.headerBytesChanged++;
            else if (histogramBins > 0) stats.histogram[(i - payload) * static_cast<size_t>(histogramBins) / payloadLen]++;
        }
    }
    return stats;
}

void limitCorruptDecode(DecodeSettings& settings, int width, int height) {
    const int side = std::max(256, 2 * std::max(width, height));
    settings.maxSide = side;
    settings.maxPixels = static_cast<int64_t>(side) * side;
}

} // namespace glic
