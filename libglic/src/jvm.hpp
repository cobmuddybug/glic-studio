#pragma once

// Java and Processing numeric semantics, so the port computes exactly what the
// original GLIC sketch computes on the JVM.
//
//  - Float arithmetic: Java evaluates float expressions in float. C++ does too
//    on x86-64/SSE with -ffp-contract=off (set in CMake).
//  - Processing's math helpers compute in double and cast to float.
//  - Java float->int casts saturate and map NaN to 0 (C++ casts are UB there).
//  - Integer overflow wraps (libglic is built with -fwrapv).
//  - Randomness is java.util.Random behind Processing's random(), seeded
//    explicitly per call instead of from the clock.

#include <cmath>
#include <cstdint>
#include <limits>

namespace glic::jvm {

// (int) cast of a float or double, Java semantics.
inline int toInt(double v) {
    if (std::isnan(v)) return 0;
    if (v >= 2147483647.0) return std::numeric_limits<int>::max();
    if (v <= -2147483648.0) return std::numeric_limits<int>::min();
    return static_cast<int>(v);
}

// (short) / (byte) narrowing of an int.
inline int16_t toShort(int v) { return static_cast<int16_t>(static_cast<uint16_t>(v)); }

// Math.round(float) == floor(x + 0.5) computed exactly, NaN -> 0, saturating.
inline int round(float x) {
    if (std::isnan(x)) return 0;
    return toInt(std::floor(static_cast<double>(x) + 0.5));
}

// Processing PApplet helpers.
inline float log(float n) { return static_cast<float>(std::log(static_cast<double>(n))); }
inline float sqrt(float n) { return static_cast<float>(std::sqrt(static_cast<double>(n))); }
inline float pow(float n, float e) { return static_cast<float>(std::pow(static_cast<double>(n), static_cast<double>(e))); }
inline int ceil(float n) { return toInt(std::ceil(static_cast<double>(n))); }
inline int floor(float n) { return toInt(std::floor(static_cast<double>(n))); }
inline float sq(float n) { return n * n; }
inline float abs(float n) { return n < 0 ? -n : n; }
inline int abs(int n) { return n < 0 ? -n : n; }  // abs(INT_MIN) == INT_MIN, as in Java (wraps)
inline int constrain(int amt, int low, int high) { return amt < low ? low : (amt > high ? high : amt); }
inline float max(float a, float b) { return a > b ? a : b; }
inline float min(float a, float b) { return a < b ? a : b; }
inline int max(int a, int b) { return a > b ? a : b; }
inline int min(int a, int b) { return a < b ? a : b; }
inline int min(int a, int b, int c) { return a < b ? (a < c ? a : c) : (b < c ? b : c); }
inline int max(int a, int b, int c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }
inline float map(float value, float start1, float stop1, float start2, float stop2) {
    return start2 + (stop2 - start2) * ((value - start1) / (stop1 - start1));
}

// Java's float % float.
inline float fmod(float a, float b) { return std::fmod(a, b); }

// Java int shift: the count is masked to 5 bits.
inline int shl(int v, int n) { return static_cast<int>(static_cast<uint32_t>(v) << (n & 31)); }

// java.util.Random.
class JavaRandom {
public:
    explicit JavaRandom(int64_t seed = 0) { setSeed(seed); }

    void setSeed(int64_t seed) { seed_ = (static_cast<uint64_t>(seed) ^ kMultiplier) & kMask; }

    int next(int bits) {
        seed_ = (seed_ * kMultiplier + kAddend) & kMask;
        return static_cast<int>(static_cast<int64_t>(seed_) >> (48 - bits));
    }

    float nextFloat() { return next(24) / static_cast<float>(1 << 24); }

private:
    static constexpr uint64_t kMultiplier = 0x5DEECE66DULL;
    static constexpr uint64_t kAddend = 0xBULL;
    static constexpr uint64_t kMask = (1ULL << 48) - 1;
    uint64_t seed_ = 0;
};

// Processing's random(high) / random(low, high) on top of java.util.Random.
class ProcessingRandom {
public:
    explicit ProcessingRandom(int64_t seed = 0) : rng_(seed) {}

    void seed(int64_t s) { rng_.setSeed(s); }

    float random(float high) {
        if (high == 0 || high != high) return 0;
        float value;
        do {
            value = rng_.nextFloat() * high;
        } while (value == high);
        return value;
    }

    float random(float low, float high) {
        if (low >= high) return low;
        float diff = high - low;
        float value;
        do {
            value = random(diff) + low;
        } while (value == high);
        return value;
    }

private:
    JavaRandom rng_;
};

} // namespace glic::jvm
