#include "resize.hpp"

#include <algorithm>
#include <cstdlib>

namespace studio {

glic::Image downscaleToFit(const glic::Image& src, int maxEdge) {
    const int longest = std::max(src.width, src.height);
    if (longest <= maxEdge || maxEdge < 1) return src;

    glic::Image dst;
    dst.width = std::max(1, static_cast<int>(static_cast<int64_t>(src.width) * maxEdge / longest));
    dst.height = std::max(1, static_cast<int>(static_cast<int64_t>(src.height) * maxEdge / longest));
    dst.pixels.resize(static_cast<size_t>(dst.width) * dst.height);

    for (int y = 0; y < dst.height; y++) {
        const int y0 = static_cast<int>(static_cast<int64_t>(y) * src.height / dst.height);
        const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<int64_t>(y + 1) * src.height / dst.height));
        for (int x = 0; x < dst.width; x++) {
            const int x0 = static_cast<int>(static_cast<int64_t>(x) * src.width / dst.width);
            const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<int64_t>(x + 1) * src.width / dst.width));
            uint32_t a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < y1; sy++) {
                for (int sx = x0; sx < x1; sx++) {
                    glic::Color c = src.pixels[static_cast<size_t>(sy) * src.width + sx];
                    a += glic::getA(c);
                    r += glic::getR(c);
                    g += glic::getG(c);
                    b += glic::getB(c);
                    n++;
                }
            }
            dst.pixels[static_cast<size_t>(y) * dst.width + x] =
                glic::makeColor(static_cast<uint8_t>(r / n), static_cast<uint8_t>(g / n), static_cast<uint8_t>(b / n),
                                static_cast<uint8_t>(a / n));
        }
    }
    return dst;
}

glic::Image difference(const glic::Image& a, const glic::Image& b) {
    glic::Image d;
    d.width = a.width;
    d.height = a.height;
    d.pixels.resize(a.pixels.size());
    for (size_t i = 0; i < a.pixels.size() && i < b.pixels.size(); i++) {
        glic::Color p = a.pixels[i], q = b.pixels[i];
        d.pixels[i] = glic::makeColor(static_cast<uint8_t>(std::abs(glic::getR(p) - glic::getR(q))),
                                      static_cast<uint8_t>(std::abs(glic::getG(p) - glic::getG(q))),
                                      static_cast<uint8_t>(std::abs(glic::getB(p) - glic::getB(q))));
    }
    return d;
}

} // namespace studio
