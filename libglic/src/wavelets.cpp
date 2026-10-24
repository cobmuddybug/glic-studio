#include "wavelets.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace glic {

namespace {

#include "wavelet_tables.inc"

struct NotBinary {};

// MathToolKit.getExponent / isBinary, as JWave computes them.
int getExponent(double f) {
    return static_cast<int>(std::log(f) / std::log(2.));
}

bool isBinary(int number) {
    int power = static_cast<int>(std::log(static_cast<double>(number)) / std::log(2.));
    double result = 1. * std::pow(2., power);
    return result == number;
}

int calcExponent(int number) {
    if (!isBinary(number)) throw NotBinary{};
    return getExponent(number);
}

// Wavelet.forward(arrTime, arrTimeLength)
std::vector<double> waveletForward(const WaveletDef& w, const std::vector<double>& arrTime, int length) {
    std::vector<double> arrHilb(length);
    int h = length >> 1;
    for (int i = 0; i < h; i++) {
        arrHilb[i] = arrHilb[i + h] = 0.;
        for (int j = 0; j < w.motherWavelength; j++) {
            int k = (i << 1) + j;
            while (k >= length) k -= length;
            arrHilb[i] += arrTime[k] * w.scalingDeCom[j];
            arrHilb[i + h] += arrTime[k] * w.waveletDeCom[j];
        }
    }
    return arrHilb;
}

// Wavelet.reverse, or Haar1Orthogonal.reverse when halveReverse.
std::vector<double> waveletReverse(const WaveletDef& w, const std::vector<double>& arrHilb, int length) {
    std::vector<double> arrTime(length, 0.);
    int h = length >> 1;
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w.motherWavelength; j++) {
            int k = (i << 1) + j;
            while (k >= length) k -= length;
            if (w.halveReverse) {
                arrTime[k] += .5 * ((arrHilb[i] * w.scalingReCon[j]) + (arrHilb[i + h] * w.waveletReCon[j]));
            } else {
                arrTime[k] += (arrHilb[i] * w.scalingReCon[j]) + (arrHilb[i + h] * w.waveletReCon[j]);
            }
        }
    }
    return arrTime;
}

} // namespace

const WaveletDef* findWavelet(int id) {
    if (id < 1 || id > 67) return nullptr;
    return &kWavelets[id];
}

std::vector<double> WaveletTransform::forward1d(const std::vector<double>& arrTime, int level) const {
    const int length = static_cast<int>(arrTime.size());
    if (!isBinary(length)) throw NotBinary{};
    int noOfLevels = calcExponent(length);
    if (level < 0 || level > noOfLevels) throw NotBinary{};

    std::vector<double> arrHilb = arrTime;
    const int tw = w_.transformWavelength;

    if (!packet_) {
        int l = 0;
        int h = length;
        while (h >= tw && l < level) {
            auto part = waveletForward(w_, arrHilb, h);
            std::copy(part.begin(), part.begin() + h, arrHilb.begin());
            h >>= 1;
            l++;
        }
    } else {
        int k = length;
        int h = length;
        int l = 0;
        while (h >= tw && l < level) {
            int g = k / h;
            for (int p = 0; p < g; p++) {
                std::vector<double> iBuf(arrHilb.begin() + p * h, arrHilb.begin() + (p + 1) * h);
                auto oBuf = waveletForward(w_, iBuf, h);
                std::copy(oBuf.begin(), oBuf.end(), arrHilb.begin() + p * h);
            }
            h >>= 1;
            l++;
        }
    }
    return arrHilb;
}

std::vector<double> WaveletTransform::reverse1d(const std::vector<double>& arrHilb, int level) const {
    const int length = static_cast<int>(arrHilb.size());
    if (!isBinary(length)) throw NotBinary{};
    int noOfLevels = calcExponent(length);
    if (level < 0 || level > noOfLevels) throw NotBinary{};

    std::vector<double> arrTime = arrHilb;
    const int tw = w_.transformWavelength;
    int h = tw;
    int steps = calcExponent(length);
    for (int l = level; l < steps; l++) h <<= 1;

    if (!packet_) {
        while (h <= length && h >= tw) {
            auto part = waveletReverse(w_, arrTime, h);
            std::copy(part.begin(), part.begin() + h, arrTime.begin());
            h <<= 1;
        }
    } else {
        int k = length;
        while (h <= length && h >= tw) {
            int g = k / h;
            for (int p = 0; p < g; p++) {
                std::vector<double> iBuf(arrTime.begin() + p * h, arrTime.begin() + (p + 1) * h);
                auto oBuf = waveletReverse(w_, iBuf, h);
                std::copy(oBuf.begin(), oBuf.end(), arrTime.begin() + p * h);
            }
            h <<= 1;
        }
    }
    return arrTime;
}

// BasicTransform.forward(double[][]): rows (first index) with lvlN, then columns with lvlM.
bool WaveletTransform::forward(Matrix& mat) const {
    try {
        const int rows = static_cast<int>(mat.size());
        const int cols = static_cast<int>(mat[0].size());
        const int lvlM = getExponent(rows);
        const int lvlN = getExponent(cols);
        Matrix hilb(rows, std::vector<double>(cols));
        for (int i = 0; i < rows; i++) hilb[i] = forward1d(mat[i], lvlN);
        std::vector<double> col(rows);
        for (int j = 0; j < cols; j++) {
            for (int i = 0; i < rows; i++) col[i] = hilb[i][j];
            auto out = forward1d(col, lvlM);
            for (int i = 0; i < rows; i++) hilb[i][j] = out[i];
        }
        mat = std::move(hilb);
        return true;
    } catch (const NotBinary&) {
        return false;
    }
}

// BasicTransform.reverse(double[][]): columns first, then rows.
bool WaveletTransform::reverse(Matrix& freq) const {
    try {
        const int rows = static_cast<int>(freq.size());
        const int cols = static_cast<int>(freq[0].size());
        const int lvlM = getExponent(rows);
        const int lvlN = getExponent(cols);
        Matrix time(rows, std::vector<double>(cols));
        std::vector<double> col(rows);
        for (int j = 0; j < cols; j++) {
            for (int i = 0; i < rows; i++) col[i] = freq[i][j];
            auto out = reverse1d(col, lvlM);
            for (int i = 0; i < rows; i++) time[i][j] = out[i];
        }
        for (int i = 0; i < rows; i++) time[i] = reverse1d(time[i], lvlN);
        freq = std::move(time);
        return true;
    } catch (const NotBinary&) {
        return false;
    }
}

Matrix compressMagnitude(const Matrix& m, double threshold) {
    // Compressor(double): a threshold <= 0 falls back to 1.
    if (threshold <= 0.) threshold = 1.;
    const int rows = static_cast<int>(m.size());
    const int cols = static_cast<int>(m[0].size());
    double magnitude = 0.;
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++) magnitude += std::abs(m[i][j]);
    magnitude /= static_cast<double>(rows) * static_cast<double>(cols);

    Matrix out(rows, std::vector<double>(cols));
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            out[i][j] = std::abs(m[i][j]) >= magnitude * threshold ? m[i][j] : 0.;
    return out;
}

} // namespace glic
