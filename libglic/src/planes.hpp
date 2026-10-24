#pragma once

// Port of GLIC's planes.pde and segmentation.pde data structures.

#include "wavelets.hpp"

#include <cstdint>
#include <vector>

namespace glic {

constexpr int CLAMP_NONE = 0;
constexpr int CLAMP_MOD256 = 1;

int clampIn(int method, int x);
int clampOut(int method, int x);
int clampValue(int method, int x);

struct Segment {
    int x = 0, y = 0, size = 0;
    int predType = 0;  // PRED_NONE
    float angle = -1;
    int refa = -1;
    int refx = 32767;  // Short.MAX_VALUE
    int refy = 32767;
};

// Reference (outside) colour, already converted to the working colour space.
struct RefColor {
    int c[4];
    RefColor(int color, int cs);
};

class Planes {
public:
    Planes(int w, int h, int cs, const RefColor& ref);
    Planes(const uint32_t* argb, int w, int h, int cs, const RefColor& ref);

    Planes cloneEmpty() const { return Planes(w, h, cs, ref); }

    int get(int p, int x, int y) const {
        if (x < 0 || x >= w || y < 0 || y >= h) return ref.c[p];
        return ch[p][static_cast<size_t>(x) * h + y];
    }
    void set(int p, int x, int y, int v) {
        if (x >= 0 && x < w && y >= 0 && y < h) ch[p][static_cast<size_t>(x) * h + y] = v;
    }

    Matrix getSegment(int p, const Segment& s) const;
    void setSegment(int p, const Segment& s, const Matrix& values, int clampMethod);
    void subtract(int p, const Segment& s, const std::vector<int>& values, int clampMethod);
    void add(int p, const Segment& s, const std::vector<int>& values, int clampMethod);

    // Planes.toPixels with no source image loaded (alpha 0xff).
    std::vector<uint32_t> toPixels() const;

    int w, h, ww, hh, cs;
    RefColor ref;

private:
    std::vector<int> ch[3];
};

} // namespace glic
