#include "predict.hpp"

#include "bitio.hpp"

#include <climits>

namespace glic {

using namespace jvm;

// ---------------------------------------------------------------- segmentation

namespace {

float calcStdDev(const Planes& planes, int pno, int x, int y, int size, CodecContext& ctx) {
    int limit = toInt(max(0.1f * sq(static_cast<float>(size)), 4.0f));
    float A = 0;
    float Q = 0;
    for (int k = 1; k <= limit; k++) {
        int posx = toInt(ctx.rng.random(static_cast<float>(size)));
        int posy = toInt(ctx.rng.random(static_cast<float>(size)));
        int xk = planes.get(pno, x + posx, y + posy);
        float oldA = A;
        A += (xk - A) / k;
        Q += (xk - oldA) * (xk - A);
    }
    return sqrt(Q / (limit - 1));
}

void segment(BitOutput& out, std::vector<Segment>& s, const Planes& p, int pno, int x, int y, int size,
             int minSize, int maxSize, float thr, CodecContext& ctx) {
    if (x >= p.w || y >= p.h) return;
    ctx.checkCancel();
    float currStdDev = calcStdDev(p, pno, x, y, size, ctx);
    if (size > maxSize || (size > minSize && currStdDev > thr)) {
        out.writeBoolean(true);
        int mid = size / 2;
        segment(out, s, p, pno, x, y, mid, minSize, maxSize, thr, ctx);
        segment(out, s, p, pno, x + mid, y, mid, minSize, maxSize, thr, ctx);
        segment(out, s, p, pno, x, y + mid, mid, minSize, maxSize, thr, ctx);
        segment(out, s, p, pno, x + mid, y + mid, mid, minSize, maxSize, thr, ctx);
    } else {
        out.writeBoolean(false);
        Segment segm;
        segm.x = x;
        segm.y = y;
        segm.size = size;
        s.push_back(segm);
    }
}

void segment(BitInput& in, std::vector<Segment>& s, const Planes& p, int x, int y, int size) {
    if (x >= p.w || y >= p.h) return;
    bool decision;
    try {
        decision = in.readBoolean();
    } catch (...) {
        decision = false;
    }
    if (decision && size > 2) {
        int mid = size / 2;
        segment(in, s, p, x, y, mid);
        segment(in, s, p, x + mid, y, mid);
        segment(in, s, p, x, y + mid, mid);
        segment(in, s, p, x + mid, y + mid, mid);
    } else {
        Segment segm;
        segm.x = x;
        segm.y = y;
        segm.size = size;
        s.push_back(segm);
    }
}

} // namespace

std::vector<Segment> makeSegmentation(BitOutput& out, const Planes& p, int pno, int minSize, int maxSize, float thr,
                                      CodecContext& ctx) {
    std::vector<Segment> s;
    segment(out, s, p, pno, 0, 0, max(p.ww, p.hh), max(1, minSize), min(512, maxSize), thr, ctx);
    return s;
}

std::vector<Segment> readSegmentation(BitInput& in, const Planes& p) {
    std::vector<Segment> s;
    segment(in, s, p, 0, 0, max(p.ww, p.hh));
    return s;
}

// ----------------------------------------------------------------- predictions

namespace {

using Block = std::vector<int>;

inline Block newBlock(const Segment& s) { return Block(static_cast<size_t>(s.size) * s.size, 0); }
inline int& at(Block& b, const Segment& s, int x, int y) { return b[static_cast<size_t>(x) * s.size + y]; }

int getSAD(const Block& pred, const Planes& p, int pno, const Segment& s) {
    int sum = 0;
    for (int x = 0; x < s.size; x++)
        for (int y = 0; y < s.size; y++)
            sum += abs(p.get(pno, s.x + x, s.y + y) - pred[static_cast<size_t>(x) * s.size + y]);
    return sum;
}

Block predGen(const Planes& p, int pno, Segment& s, int type) {
    Block res = newBlock(s);
    for (int x = 0; x < s.size; x++) {
        for (int y = 0; y < s.size; y++) {
            switch (type) {
            case 0: at(res, s, x, y) = p.get(pno, s.x - 1, s.y - 1); break;
            case 1: at(res, s, x, y) = p.get(pno, s.x - 1, s.y + y); break;
            case 2: at(res, s, x, y) = p.get(pno, s.x + x, s.y - 1); break;
            }
        }
    }
    s.predType = type + 1;  // CORNER, H, V
    return res;
}

int getDC(const Planes& p, int pno, const Segment& s) {
    int v = 0;
    for (int i = 0; i < s.size; i++) {
        v += p.get(pno, s.x - 1, s.y + i);
        v += p.get(pno, s.x + i, s.y - 1);
    }
    v += p.get(pno, s.x - 1, s.y - 1);
    v /= (s.size + s.size + 1);
    return v;
}

int getMedian(int a, int b, int c) { return max(min(a, b), min(max(a, b), c)); }

Block predDC(const Planes& p, int pno, Segment& s) {
    Block res = newBlock(s);
    int c = getDC(p, pno, s);
    for (auto& v : res) v = c;
    s.predType = 4;
    return res;
}

Block predDCMedian(const Planes& p, int pno, Segment& s) {
    Block res = newBlock(s);
    int c = getDC(p, pno, s);
    for (int x = 0; x < s.size; x++) {
        int v1 = p.get(pno, s.x + x, s.y - 1);
        for (int y = 0; y < s.size; y++) at(res, s, x, y) = getMedian(c, v1, p.get(pno, s.x - 1, s.y + y));
    }
    s.predType = 5;
    return res;
}

Block predMedian(const Planes& p, int pno, Segment& s) {
    Block res = newBlock(s);
    int c = p.get(pno, s.x - 1, s.y - 1);
    for (int x = 0; x < s.size; x++) {
        int v1 = p.get(pno, s.x + x, s.y - 1);
        for (int y = 0; y < s.size; y++) at(res, s, x, y) = getMedian(c, v1, p.get(pno, s.x - 1, s.y + y));
    }
    s.predType = 6;
    return res;
}

Block predTrueMotion(const Planes& p, int pno, Segment& s) {
    Block res = newBlock(s);
    int c = p.get(pno, s.x - 1, s.y - 1);
    for (int x = 0; x < s.size; x++) {
        int v1 = p.get(pno, s.x + x, s.y - 1);
        for (int y = 0; y < s.size; y++) at(res, s, x, y) = constrain(v1 + p.get(pno, s.x - 1, s.y + y) - c, 0, 255);
    }
    s.predType = 8;
    return res;
}

Block predPaeth(const Planes& p, int pno, Segment& s) {
    Block res = newBlock(s);
    int c = p.get(pno, s.x - 1, s.y - 1);
    for (int x = 0; x < s.size; x++) {
        int v1 = p.get(pno, s.x + x, s.y - 1);
        for (int y = 0; y < s.size; y++) {
            int v2 = p.get(pno, s.x - 1, s.y + y);
            int pp = v1 + v2 - c;
            int pa = abs(pp - v2);
            int pb = abs(pp - v1);
            int pc = abs(pp - c);
            int v = ((pa <= pb) && (pa <= pc)) ? v2 : (pb <= pc ? v1 : c);
            at(res, s, x, y) = constrain(v, 0, 255);
        }
    }
    s.predType = 9;
    return res;
}

Block predAvg(const Planes& p, int pno, Segment& s) {
    Block res = newBlock(s);
    for (int x = 0; x < s.size; x++) {
        int v1 = p.get(pno, s.x + x, s.y - 1);
        for (int y = 0; y < s.size; y++) at(res, s, x, y) = (v1 + p.get(pno, s.x - 1, s.y + y)) >> 1;
    }
    s.predType = 7;
    return res;
}

Block predLDiag(const Planes& p, int pno, Segment& s) {
    Block res = newBlock(s);
    for (int x = 0; x < s.size; x++) {
        for (int y = 0; y < s.size; y++) {
            int ss = x + y;
            int xx = p.get(pno, s.x + (ss + 1 < s.size ? ss + 1 : s.size - 1), s.y - 1);
            int yy = p.get(pno, s.x - 1, s.y + (ss < s.size ? ss : s.size - 1));
            at(res, s, x, y) = ((x + 1) * xx + (y + 1) * yy) / (x + y + 2);
        }
    }
    s.predType = 10;
    return res;
}

Block predHV(const Planes& p, int pno, Segment& s) {
    Block res = newBlock(s);
    for (int x = 0; x < s.size; x++) {
        for (int y = 0; y < s.size; y++) {
            int c;
            if (x > y) c = p.get(pno, s.x + x, s.y - 1);
            else if (y > x) c = p.get(pno, s.x - 1, s.y + y);
            else c = (p.get(pno, s.x + x, s.y - 1) + p.get(pno, s.x - 1, s.y + y)) >> 1;
            at(res, s, x, y) = c;
        }
    }
    s.predType = 11;
    return res;
}

Block predJpegLS(const Planes& p, int pno, Segment& s) {
    Block res = newBlock(s);
    for (int x = 0; x < s.size; x++) {
        int c = p.get(pno, s.x + x - 1, s.y - 1);
        int a = p.get(pno, s.x + x, s.y - 1);
        for (int y = 0; y < s.size; y++) {
            int b = p.get(pno, s.x - 1, s.y + y);
            int v;
            if (c >= max(a, b)) v = min(a, b);
            else if (c <= min(a, b)) v = max(a, b);
            else v = a + b - c;
            at(res, s, x, y) = v;
        }
    }
    s.predType = 12;
    return res;
}

Block predDiff(const Planes& p, int pno, Segment& s) {
    Block res = newBlock(s);
    for (int x = 0; x < s.size; x++) {
        int x1 = p.get(pno, s.x + x, s.y - 1);
        int x2 = p.get(pno, s.x + x, s.y - 2);
        for (int y = 0; y < s.size; y++) {
            int y1 = p.get(pno, s.x - 1, s.y + y);
            int y2 = p.get(pno, s.x - 2, s.y + y);
            at(res, s, x, y) = constrain((y2 + y2 - y1 + x2 + x2 - x1) >> 1, 0, 255);
        }
    }
    s.predType = 13;
    return res;
}

Block findBestRef(const Planes& p, int pno, Segment& s, CodecContext& ctx) {
    int currsad = INT_MAX;
    Block currres;
    for (int i = 0; i < 45; i++) {
        Block res = newBlock(s);
        int xx = toInt(ctx.rng.random(static_cast<float>(-s.size), static_cast<float>(s.x)));
        int yy;
        if (xx < s.x - s.size) yy = toInt(ctx.rng.random(static_cast<float>(-s.size), static_cast<float>(s.y)));
        else yy = toInt(ctx.rng.random(static_cast<float>(-s.size), static_cast<float>(s.y - s.size)));
        for (int x = 0; x < s.size; x++)
            for (int y = 0; y < s.size; y++) at(res, s, x, y) = p.get(pno, xx + x, yy + y);
        int sad = getSAD(res, p, pno, s);
        if (sad < currsad) {
            currres = std::move(res);
            currsad = sad;
            s.refx = xx;
            s.refy = yy;
        }
    }
    return currres;
}

Block predRef(const Planes& p, int pno, Segment& s, CodecContext& ctx) {
    s.predType = PRED_REF;
    if (s.refx == 32767 || s.refy == 32767) return findBestRef(p, pno, s, ctx);
    Block res = newBlock(s);
    for (int x = 0; x < s.size; x++)
        for (int y = 0; y < s.size; y++) at(res, s, x, y) = p.get(pno, s.refx + x, s.refy + y);
    return res;
}

// getAngleRef returns a PVector of round()ed floats; callers cast back to int.
struct AngleRef {
    float x, y;
};

AngleRef getAngleRef(int i, int x, int y, float a, int w) {
    float xx = -1;
    float yy = -1;
    switch (i % 3) {
    case 0: {
        float v = (w - y - 1) + x * a;
        xx = (v - w) / a;
        yy = (w - 1 - a - v);
        break;
    }
    case 1: {
        float v = (w - x - 1) + y * a;
        yy = (v - w) / a;
        xx = (w - 1 - a - v);
        break;
    }
    case 2: {
        float v = x + y * a;
        yy = -1.0f;
        xx = v + a;
        break;
    }
    }
    if (xx > yy) return {static_cast<float>(round(xx)), -1};
    return {-1, static_cast<float>(round(yy))};
}

Block angleBlock(const Planes& p, int pno, const Segment& s, int refa, float a) {
    Block res = newBlock(s);
    for (int x = 0; x < s.size; x++) {
        for (int y = 0; y < s.size; y++) {
            AngleRef r = getAngleRef(refa, x, y, a, s.size);
            int xx = r.x >= s.size ? s.size - 1 : toInt(r.x);
            at(res, s, x, y) = p.get(pno, xx + s.x, toInt(r.y) + s.y);
        }
    }
    return res;
}

Block findBestAngle(const Planes& p, int pno, Segment& s) {
    float stepa = 1.0f / min(16, s.size);
    Block currres;
    int currsad = INT_MAX;
    for (int i = 0; i < 3; i++) {
        for (float a = 0; a < 1.0f; a += stepa) {
            float aa = toInt(a * 0x8000) / static_cast<float>(0x8000);
            Block res = angleBlock(p, pno, s, i, aa);
            int sad = getSAD(res, p, pno, s);
            if (sad < currsad) {
                currres = std::move(res);
                currsad = sad;
                s.angle = a;
                s.refa = i;
            }
        }
    }
    return currres;
}

Block predAngle(const Planes& p, int pno, Segment& s) {
    s.predType = PRED_ANGLE;
    if (s.angle < 0 || s.refa < 0) return findBestAngle(p, pno, s);
    return angleBlock(p, pno, s, s.refa, s.angle);
}

Block predSad(Planes& p, int pno, Segment& s, bool doSad, CodecContext& ctx) {
    Block currres;
    int currsad = doSad ? INT_MAX : INT_MIN;
    int currtype = -1;
    for (int i = 0; i < MAX_PRED; i++) {
        Block res = predict(i, p, pno, s, ctx);
        int sad = getSAD(res, p, pno, s);
        if ((doSad && sad < currsad) || (!doSad && sad > currsad)) {
            currsad = sad;
            currtype = s.predType;
            currres = std::move(res);
        }
    }
    s.predType = currtype;
    return currres;
}

} // namespace

std::vector<int> predict(int prediction, Planes& p, int pno, Segment& s, CodecContext& ctx) {
    if (prediction >= 0) ctx.lastMethod = prediction;
    switch (prediction) {
    case 1: return predGen(p, pno, s, 0);
    case 2: return predGen(p, pno, s, 1);
    case 3: return predGen(p, pno, s, 2);
    case 4: return predDC(p, pno, s);
    case 5: return predDCMedian(p, pno, s);
    case 6: return predMedian(p, pno, s);
    case 7: return predAvg(p, pno, s);
    case 8: return predTrueMotion(p, pno, s);
    case 9: return predPaeth(p, pno, s);
    case 10: return predLDiag(p, pno, s);
    case 11: return predHV(p, pno, s);
    case 12: return predJpegLS(p, pno, s);
    case 13: return predDiff(p, pno, s);
    case 14: return predRef(p, pno, s, ctx);
    case 15: return predAngle(p, pno, s);
    case PRED_RANDOM: return predict(toInt(ctx.rng.random(static_cast<float>(MAX_PRED))), p, pno, s, ctx);
    case PRED_SAD: return predSad(p, pno, s, true, ctx);
    case PRED_BSAD: return predSad(p, pno, s, false, ctx);
    default: return newBlock(s);
    }
}

} // namespace glic
