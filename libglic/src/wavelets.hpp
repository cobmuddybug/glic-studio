#pragma once

// Port of the parts of JWave (MIT, (c) Christian Scheiblich) that GLIC uses:
// Wavelet.forward/reverse, Haar1Orthogonal's reverse, FastWaveletTransform,
// WaveletPacketTransform, the 2-D row/column driver and CompressorMagnitude.
// Revision: JWave @ 3afd122 (2016-01-09), matching the jar GLIC ships.

#include <vector>

namespace glic {

struct WaveletDef {
    int id;
    const char* name;
    int motherWavelength;
    int transformWavelength;
    bool halveReverse;  // Haar1Orthogonal scales its reconstruction by 0.5
    const double* scalingDeCom;
    const double* waveletDeCom;
    const double* scalingReCon;
    const double* waveletReCon;
};

// nullptr for ids outside 1..67.
const WaveletDef* findWavelet(int id);

using Matrix = std::vector<std::vector<double>>;

// JWave throws JWaveFailure for non-power-of-two sizes; GLIC catches it and
// skips the transform. transform2d returns false in that case.
class WaveletTransform {
public:
    WaveletTransform(const WaveletDef& wavelet, bool packet) : w_(wavelet), packet_(packet) {}

    bool forward(Matrix& m) const;
    bool reverse(Matrix& m) const;

private:
    std::vector<double> forward1d(const std::vector<double>& arr, int level) const;
    std::vector<double> reverse1d(const std::vector<double>& arr, int level) const;

    const WaveletDef& w_;
    bool packet_;
};

// CompressorMagnitude(threshold).compress(matrix)
Matrix compressMagnitude(const Matrix& m, double threshold);

} // namespace glic
