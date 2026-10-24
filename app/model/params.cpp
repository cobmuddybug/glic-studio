#include "params.hpp"

namespace studio {

namespace {

std::vector<Choice> make(std::initializer_list<int> values, auto label) {
    std::vector<Choice> out;
    for (int v : values) out.push_back({v, std::string(label(v))});
    return out;
}

void copyParam(glic::ChannelSettings& dst, const glic::ChannelSettings& src, ChannelParam p) {
    switch (p) {
    case ChannelParam::MinBlock: dst.minBlockSize = src.minBlockSize; break;
    case ChannelParam::MaxBlock: dst.maxBlockSize = src.maxBlockSize; break;
    case ChannelParam::Threshold: dst.threshold = src.threshold; break;
    case ChannelParam::Prediction: dst.prediction = src.prediction; break;
    case ChannelParam::Quantization: dst.quantization = src.quantization; break;
    case ChannelParam::Clamp: dst.clamp = src.clamp; break;
    case ChannelParam::Wavelet: dst.wavelet = src.wavelet; break;
    case ChannelParam::TransformType: dst.transformType = src.transformType; break;
    case ChannelParam::Compress: dst.compress = src.compress; break;
    case ChannelParam::Scale: dst.scale = src.scale; break;
    case ChannelParam::Encoding: dst.encoding = src.encoding; break;
    }
}

} // namespace

const std::vector<Choice>& predictionChoices() {
    static const auto v = [] {
        std::vector<Choice> out;
        for (int i = 0; i < glic::kPredictionCount; i++)
            out.push_back({i, std::string(glic::predictionName(static_cast<glic::Prediction>(i)))});
        for (int i : {-1, -2, -3}) out.push_back({i, std::string(glic::predictionName(static_cast<glic::Prediction>(i)))});
        return out;
    }();
    return v;
}

const std::vector<Choice>& waveletChoices() {
    static const auto v = [] {
        std::vector<Choice> out;
        for (int i = 0; i < glic::kWaveletCount; i++) out.push_back({i, std::string(glic::waveletName(i))});
        out.push_back({glic::kWaveletRandom, "RANDOM"});
        return out;
    }();
    return v;
}

const std::vector<Choice>& colorSpaceChoices() {
    static const auto v = [] {
        std::vector<Choice> out;
        for (int i = 0; i < glic::kColorSpaceCount; i++)
            out.push_back({i, std::string(glic::colorSpaceName(static_cast<glic::ColorSpace>(i)))});
        return out;
    }();
    return v;
}

const std::vector<Choice>& encodingChoices() {
    static const auto v = make({0, 1, 2}, [](int e) { return glic::encodingName(static_cast<glic::Encoding>(e)); });
    return v;
}

const std::vector<Choice>& transformTypeChoices() {
    static const std::vector<Choice> v = {{0, "FWT"}, {1, "WPT"}, {-1, "RANDOM"}};
    return v;
}

int log2Floor(int v) {
    int e = 0;
    while (v > 1) {
        v >>= 1;
        e++;
    }
    return e;
}

void resetParam(glic::EncodeSettings& s, int channel, ChannelParam p) {
    copyParam(s.channels[channel], glic::ChannelSettings{}, p);
}

void copyToAllChannels(glic::EncodeSettings& s, int channel, ChannelParam p) {
    for (int c = 0; c < 3; c++)
        if (c != channel) copyParam(s.channels[c], s.channels[channel], p);
}

void linkChannels(glic::EncodeSettings& s) {
    s.channels[1] = s.channels[0];
    s.channels[2] = s.channels[0];
}

bool channelsIdentical(const glic::EncodeSettings& s) {
    return s.channels[0] == s.channels[1] && s.channels[0] == s.channels[2];
}

} // namespace studio
