#include "planes.hpp"

#include "colorspaces.hpp"
#include "jvm.hpp"

namespace glic {

using namespace jvm;

namespace {
const float LOG2 = log(2.0f);
}

int clampIn(int method, int x) {
    if (method == CLAMP_MOD256) return x < 0 ? x + 256 : x > 255 ? x - 256 : x;
    return x;
}

int clampOut(int method, int x) {
    if (method == CLAMP_MOD256) return x < 0 ? x + 256 : x > 255 ? x - 256 : x;
    return constrain(x, 0, 255);
}

int clampValue(int method, int x) {
    if (method == CLAMP_MOD256) return constrain(x, 0, 255);
    return constrain(x, -255, 255);
}

RefColor::RefColor(int color, int cs) {
    int cc = toColorspace(color, cs);
    c[2] = cc & 0xff;
    c[1] = (cc >> 8) & 0xff;
    c[0] = (cc >> 16) & 0xff;
    c[3] = (cc >> 24) & 0xff;
}

Planes::Planes(int w_, int h_, int cs_, const RefColor& ref_) : w(w_), h(h_), cs(cs_), ref(ref_) {
    ww = shl(1, ceil(log(static_cast<float>(w)) / LOG2));
    hh = shl(1, ceil(log(static_cast<float>(h)) / LOG2));
    for (int p = 0; p < 3; p++) ch[p].assign(static_cast<size_t>(w) * h, ref.c[p]);
}

Planes::Planes(const uint32_t* argb, int w_, int h_, int cs_, const RefColor& ref_) : Planes(w_, h_, cs_, ref_) {
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            int c = toColorspace(static_cast<int>(argb[static_cast<size_t>(y) * w + x]), cs);
            size_t i = static_cast<size_t>(x) * h + y;
            ch[2][i] = c & 0xff;
            ch[1][i] = (c >> 8) & 0xff;
            ch[0][i] = (c >> 16) & 0xff;
        }
    }
}

std::vector<uint32_t> Planes::toPixels() const {
    std::vector<uint32_t> px(static_cast<size_t>(w) * h);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            size_t i = static_cast<size_t>(x) * h + y;
            // Unclamped channels OR'd together, as in the original.
            int c = ch[2][i] | (ch[1][i] << 8) | (ch[0][i] << 16) | static_cast<int>(0xff000000u);
            px[static_cast<size_t>(y) * w + x] = static_cast<uint32_t>(fromColorspace(c, cs));
        }
    }
    return px;
}

Matrix Planes::getSegment(int p, const Segment& s) const {
    Matrix res(s.size, std::vector<double>(s.size));
    for (int x = 0; x < s.size; x++)
        for (int y = 0; y < s.size; y++) res[x][y] = get(p, x + s.x, y + s.y) / 255.0f;  // float division (Processing literal)
    return res;
}

void Planes::setSegment(int p, const Segment& s, const Matrix& values, int clampMethod) {
    for (int x = 0; x < s.size; x++)
        for (int y = 0; y < s.size; y++)
            set(p, x + s.x, y + s.y, clampValue(clampMethod, round(static_cast<float>(values[x][y] * 255.0f))));
}

void Planes::subtract(int p, const Segment& s, const std::vector<int>& values, int clampMethod) {
    for (int x = 0; x < s.size; x++)
        for (int y = 0; y < s.size; y++)
            set(p, x + s.x, y + s.y, clampIn(clampMethod, get(p, x + s.x, y + s.y) - values[x * s.size + y]));
}

void Planes::add(int p, const Segment& s, const std::vector<int>& values, int clampMethod) {
    for (int x = 0; x < s.size; x++)
        for (int y = 0; y < s.size; y++)
            set(p, x + s.x, y + s.y, clampOut(clampMethod, get(p, x + s.x, y + s.y) + values[x * s.size + y]));
}

} // namespace glic
