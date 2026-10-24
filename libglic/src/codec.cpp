// Encode/decode pipeline. The arithmetic is glic-cpp's glic.cpp, unchanged;
// what changed is the framing (GlicData, Hooks, cancellation, per-call RNGs)
// and hardening against corrupt input. See docs/glic-internals.md.

#include <glic/glic.hpp>

#include "bitio.hpp"
#include "colorspaces.hpp"
#include "context.hpp"
#include "encoding.hpp"
#include "planes.hpp"
#include "prediction.hpp"
#include "quantization.hpp"
#include "segment.hpp"
#include "wavelet.hpp"

#include <cmath>
#include <exception>
#include <memory>

namespace glic {

namespace {

constexpr int kMaxDimension = 16384;
constexpr int kMaxTransformScale = 1 << 30;  // keeps packed widths within 32 bits

void putU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((v >> 24) & 0xFF);
    out.push_back((v >> 16) & 0xFF);
    out.push_back((v >> 8) & 0xFF);
    out.push_back(v & 0xFF);
}

uint32_t getU32(std::span<const uint8_t> b, size_t pos) {
    return (static_cast<uint32_t>(b[pos]) << 24) | (static_cast<uint32_t>(b[pos + 1]) << 16) |
           (static_cast<uint32_t>(b[pos + 2]) << 8) | static_cast<uint32_t>(b[pos + 3]);
}

ChannelConfig toChannelConfig(const ChannelHeader& h) {
    ChannelConfig c;
    c.predictionMethod = h.predictionMethod;
    c.quantizationValue = h.quantizationValue;
    c.clampMethod = h.clampMethod;
    c.waveletType = h.waveletType;
    c.transformType = h.transformType;
    c.transformScale = h.transformScale;
    c.encodingMethod = h.encodingMethod;
    return c;
}

void capturePlane(const Planes& planes, int p, std::vector<int32_t>& out) {
    const int w = planes.width();
    const int h = planes.height();
    out.resize(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            out[static_cast<size_t>(y) * w + x] = planes.get(p, x, y);
        }
    }
}

int rootSizeFor(int width, int height) {
    int ww = 1;
    while (ww < width) ww *= 2;
    int hh = 1;
    while (hh < height) hh *= 2;
    return std::max(ww, hh);
}

void resetCapture(Capture* capture, int width, int height) {
    if (!capture) return;
    capture->width = width;
    capture->height = height;
    capture->rootSize = rootSizeFor(width, height);
    for (int p = 0; p < 3; p++) {
        capture->blocks[p].clear();
        capture->planeValues[p].clear();
    }
}

} // namespace

DecodeParams decodeParamsFor(const EncodeSettings& s) {
    DecodeParams d;
    d.colorSpace = s.colorSpace;
    d.border = {s.borderColorR, s.borderColorG, s.borderColorB};
    for (int p = 0; p < 3; p++) {
        const auto& c = s.channels[p];
        auto& h = d.channels[p];
        // Narrowed exactly as the header stores them, so an in-memory decode
        // matches a decode of the serialized file.
        h.predictionMethod = c.predictionMethod;
        h.quantizationValue = static_cast<uint8_t>(c.quantizationValue);
        h.clampMethod = c.clampMethod;
        h.waveletType = c.waveletType;
        h.transformType = c.transformType;
        h.transformScale = c.transformScale;
        h.encodingMethod = c.encodingMethod;
    }
    return d;
}

std::string validate(const EncodeSettings& s) {
    for (int p = 0; p < 3; p++) {
        const auto& c = s.channels[p];
        const std::string ch = "channel " + std::to_string(p) + ": ";
        if (c.transformScale < 1 || c.transformScale > kMaxTransformScale) {
            return ch + "transform scale must be 1.." + std::to_string(kMaxTransformScale);
        }
        if (c.maxBlockSize < 1) return ch + "max block size must be >= 1";
        if (std::isnan(c.segmentationPrecision)) return ch + "threshold is NaN";
    }
    return {};
}

std::vector<uint8_t> GlicData::serialize() const {
    std::vector<uint8_t> buffer;
    size_t payload = 0;
    for (int p = 0; p < 3; p++) payload += segmentation[p].size() + prediction[p].size() + data[p].size();
    buffer.reserve(kPayloadOffset + payload);

    putU32(buffer, kMagic);
    buffer.push_back((version >> 8) & 0xFF);
    buffer.push_back(version & 0xFF);
    putU32(buffer, static_cast<uint32_t>(width));
    putU32(buffer, static_cast<uint32_t>(height));
    buffer.push_back(static_cast<uint8_t>(params.colorSpace));
    buffer.insert(buffer.end(), params.border.begin(), params.border.end());
    for (int p = 0; p < 3; p++) putU32(buffer, static_cast<uint32_t>(segmentation[p].size()));
    for (int p = 0; p < 3; p++) putU32(buffer, static_cast<uint32_t>(prediction[p].size()));
    for (int p = 0; p < 3; p++) putU32(buffer, static_cast<uint32_t>(data[p].size()));
    buffer.resize(kHeaderSize, 0);

    for (int p = 0; p < 3; p++) {
        const auto& ch = params.channels[p];
        buffer.push_back(static_cast<uint8_t>(ch.predictionMethod));
        buffer.push_back(static_cast<uint8_t>(ch.quantizationValue));
        buffer.push_back(static_cast<uint8_t>(ch.clampMethod));
        buffer.push_back(static_cast<uint8_t>(ch.waveletType));
        buffer.push_back(static_cast<uint8_t>(ch.transformType));
        putU32(buffer, static_cast<uint32_t>(ch.transformScale));
        buffer.push_back(static_cast<uint8_t>(ch.encodingMethod));
        buffer.resize(kHeaderSize + (p + 1) * kChannelHeaderSize, 0);
    }

    for (const auto& s : segmentation) buffer.insert(buffer.end(), s.begin(), s.end());
    for (const auto& s : prediction) buffer.insert(buffer.end(), s.begin(), s.end());
    for (const auto& s : data) buffer.insert(buffer.end(), s.begin(), s.end());
    return buffer;
}

std::optional<GlicData> GlicData::parse(std::span<const uint8_t> b, std::string* error) {
    auto fail = [&](const char* msg) -> std::optional<GlicData> {
        if (error) *error = msg;
        return std::nullopt;
    };
    if (b.size() < kPayloadOffset) return fail("Buffer too small");
    if (getU32(b, 0) != kMagic) return fail("Invalid file format");

    GlicData d;
    d.version = static_cast<uint16_t>((b[4] << 8) | b[5]);
    d.width = static_cast<int>(getU32(b, 6));
    d.height = static_cast<int>(getU32(b, 10));
    d.params.colorSpace = static_cast<ColorSpace>(b[14]);
    d.params.border = {b[15], b[16], b[17]};

    uint32_t sizes[9];
    for (int i = 0; i < 9; i++) sizes[i] = getU32(b, 18 + 4 * i);

    size_t pos = kHeaderSize;
    for (int p = 0; p < 3; p++) {
        auto& ch = d.params.channels[p];
        ch.predictionMethod = static_cast<PredictionMethod>(static_cast<int8_t>(b[pos]));
        ch.quantizationValue = b[pos + 1];
        ch.clampMethod = static_cast<ClampMethod>(b[pos + 2]);
        ch.waveletType = static_cast<WaveletType>(b[pos + 3]);
        ch.transformType = static_cast<TransformType>(b[pos + 4]);
        ch.transformScale = static_cast<int>(getU32(b, pos + 5));
        ch.encodingMethod = static_cast<EncodingMethod>(b[pos + 9]);
        pos += kChannelHeaderSize;
    }

    // Upstream trusted these sizes and read past the end of the buffer (D8).
    std::array<std::vector<uint8_t>, 3>* sections[3] = {&d.segmentation, &d.prediction, &d.data};
    for (int i = 0; i < 9; i++) {
        size_t n = std::min<size_t>(sizes[i], b.size() - pos);
        (*sections[i / 3])[i % 3].assign(b.begin() + pos, b.begin() + pos + n);
        pos += n;
    }
    return d;
}

EncodeResult encode(const Image& image, const EncodeSettings& config, const Hooks* hooks) {
    EncodeResult result;
    if (auto err = validate(config); !err.empty()) {
        result.error = err;
        return result;
    }
    if (image.width < 1 || image.height < 1 || image.width > kMaxDimension || image.height > kMaxDimension ||
        image.pixels.size() != static_cast<size_t>(image.width) * image.height) {
        result.error = "Invalid image dimensions";
        return result;
    }

    CodecContext ctx;
    Capture* capture = hooks ? hooks->capture : nullptr;
    ctx.cancel = hooks ? hooks->cancel : nullptr;
    resetCapture(capture, image.width, image.height);

    try {
        const int width = image.width;
        const int height = image.height;
        RefColor ref(makeColor(config.borderColorR, config.borderColorG, config.borderColorB), config.colorSpace);
        Planes planes(image.pixels.data(), width, height, config.colorSpace, ref);

        GlicData& out = result.data;
        out.width = width;
        out.height = height;
        out.params = decodeParamsFor(config);

        std::vector<Segment> segments[3];

        for (int p = 0; p < 3; p++) {
            const auto& chConfig = config.channels[p];

            BitWriter segmWriter;
            segments[p] = makeSegmentation(segmWriter, planes, p, chConfig.minBlockSize, chConfig.maxBlockSize,
                                           chConfig.segmentationPrecision, ctx);
            segmWriter.align();
            out.segmentation[p] = segmWriter.data();

            std::shared_ptr<Wavelet> wavelet = nullptr;
            std::unique_ptr<WaveletTransform> transform = nullptr;
            std::unique_ptr<MagnitudeCompressor> compressor = nullptr;

            if (chConfig.waveletType != WaveletType::NONE) {
                wavelet = createWavelet(chConfig.waveletType);
                transform = createTransform(chConfig.transformType, wavelet);
                if (chConfig.transformCompress > 0) {
                    compressor = std::make_unique<MagnitudeCompressor>(transCompressionValue(chConfig.transformCompress));
                }
            }

            float pq = quantValue(chConfig.quantizationValue);
            auto resultPlanes = planes.clone();
            const bool metaSad = chConfig.predictionMethod == PredictionMethod::SAD ||
                                 chConfig.predictionMethod == PredictionMethod::BSAD;

            for (auto& seg : segments[p]) {
                ctx.checkCancel();

                auto pred = predict(chConfig.predictionMethod, planes, p, seg, ctx);
                if (capture && capture->segments) {
                    capture->blocks[p].push_back({seg.x, seg.y, seg.size, metaSad ? seg.predType : ctx.lastMethod});
                }

                planes.subtract(p, seg, pred, chConfig.clampMethod);

                if (pq > 0) {
                    quantize(planes, p, seg, pq, true);
                }

                if (transform) {
                    auto tr = planes.getSegment(p, seg);
                    tr = transform->forward(tr);

                    if (compressor) {
                        tr = compressor->compress(tr);
                    }

                    for (int x = 0; x < seg.size; x++) {
                        for (int y = 0; y < seg.size; y++) {
                            int val = toInt(std::round((tr[x][y] * chConfig.transformScale) / static_cast<float>(seg.size)));
                            planes.set(p, seg.x + x, seg.y + y, val);
                        }
                    }
                }

                for (int x = 0; x < seg.size; x++) {
                    for (int y = 0; y < seg.size; y++) {
                        resultPlanes->set(p, seg.x + x, seg.y + y, planes.get(p, seg.x + x, seg.y + y));
                    }
                }

                if (transform) {
                    std::vector<std::vector<double>> tr(seg.size, std::vector<double>(seg.size));
                    for (int x = 0; x < seg.size; x++) {
                        for (int y = 0; y < seg.size; y++) {
                            tr[x][y] = (seg.size * planes.get(p, seg.x + x, seg.y + y)) / static_cast<float>(chConfig.transformScale);
                        }
                    }
                    tr = transform->reverse(tr);
                    planes.setSegment(p, seg, tr, chConfig.clampMethod);
                }

                if (pq > 0) {
                    quantize(planes, p, seg, pq, false);
                }

                // Upstream quirk D3: for fixed predictors seg.predType is still
                // NONE here, so nothing is added back. Preserved.
                pred = predict(seg.predType, planes, p, seg, ctx);
                planes.add(p, seg, pred, chConfig.clampMethod);
            }

            BitWriter predWriter;
            for (const auto& seg : segments[p]) {
                predWriter.writeByte(static_cast<uint8_t>(seg.predType));
                predWriter.writeBits(static_cast<uint32_t>(static_cast<int16_t>(seg.refX)), 16);
                predWriter.writeBits(static_cast<uint32_t>(static_cast<int16_t>(seg.refY)), 16);
                predWriter.writeByte(static_cast<uint8_t>(seg.refAngle % 3));
                int16_t angleVal = static_cast<int16_t>(0x7000 * seg.angle);
                predWriter.writeBits(static_cast<uint32_t>(angleVal), 16);
            }
            predWriter.align();
            out.prediction[p] = predWriter.data();

            ctx.checkCancel();
            BitWriter dataWriter;
            encodeData(dataWriter, *resultPlanes, p, segments[p], chConfig.encodingMethod, chConfig);
            out.data[p] = dataWriter.data();

            if (capture && capture->values) {
                capturePlane(*resultPlanes, p, capture->planeValues[p]);
            }
        }

        result.ok = true;
    } catch (const Cancelled&) {
        result = EncodeResult{};
        result.cancelled = true;
        result.error = "Cancelled";
    } catch (const std::exception& e) {
        result = EncodeResult{};
        result.error = std::string("Encoding failed: ") + e.what();
    }
    return result;
}

DecodeResult decode(const GlicData& data, const DecodeSettings& settings, const Hooks* hooks) {
    DecodeResult result;
    const DecodeParams& params = settings.override ? *settings.override : data.params;
    const int width = data.width;
    const int height = data.height;

    if (width < 1 || height < 1 || width > kMaxDimension || height > kMaxDimension) {
        result.error = "Invalid dimensions " + std::to_string(width) + "x" + std::to_string(height);
        return result;
    }
    for (int p = 0; p < 3; p++) {
        int scale = params.channels[p].transformScale;
        if (scale < 1 || scale > kMaxTransformScale) {
            result.error = "Invalid transform scale in channel " + std::to_string(p);
            return result;
        }
    }

    CodecContext ctx;
    Capture* capture = hooks ? hooks->capture : nullptr;
    ctx.cancel = hooks ? hooks->cancel : nullptr;
    resetCapture(capture, width, height);

    try {
        ChannelConfig channelConfigs[3];
        for (int p = 0; p < 3; p++) channelConfigs[p] = toChannelConfig(params.channels[p]);

        RefColor ref(makeColor(params.border[0], params.border[1], params.border[2]), params.colorSpace);
        Planes planes(width, height, params.colorSpace, ref);

        int ww = 1;
        while (ww < width) ww *= 2;
        int hh = 1;
        while (hh < height) hh *= 2;

        std::vector<Segment> segments[3];
        for (int p = 0; p < 3; p++) {
            ctx.checkCancel();
            BitReader segReader(data.segmentation[p].data(), data.segmentation[p].size());
            segments[p] = readSegmentation(segReader, ww, hh, width, height);
        }

        for (int p = 0; p < 3; p++) {
            BitReader predReader(data.prediction[p].data(), data.prediction[p].size());
            for (auto& seg : segments[p]) {
                try {
                    seg.predType = static_cast<PredictionMethod>(predReader.readByte());
                    if (seg.predType == PredictionMethod::NONE) {
                        seg.predType = channelConfigs[p].predictionMethod;
                    }
                    seg.refX = static_cast<int16_t>(predReader.readBits(16));
                    seg.refY = static_cast<int16_t>(predReader.readBits(16));
                    seg.refAngle = predReader.readByte() % 3;
                    int16_t angleVal = static_cast<int16_t>(predReader.readBits(16));
                    seg.angle = static_cast<float>(angleVal) / 0x7000;
                } catch (...) {
                    break;
                }
            }
        }

        for (int p = 0; p < 3; p++) {
            ctx.checkCancel();
            BitReader dataReader(data.data[p].data(), data.data[p].size());
            decodeData(dataReader, planes, p, segments[p], channelConfigs[p].encodingMethod, channelConfigs[p]);
            if (capture && capture->values) {
                capturePlane(planes, p, capture->planeValues[p]);
            }
        }

        for (int p = 0; p < 3; p++) {
            const auto& chConfig = channelConfigs[p];

            std::shared_ptr<Wavelet> wavelet = nullptr;
            std::unique_ptr<WaveletTransform> transform = nullptr;

            if (chConfig.waveletType != WaveletType::NONE) {
                wavelet = createWavelet(chConfig.waveletType);
                transform = createTransform(chConfig.transformType, wavelet);
            }

            float pq = quantValue(chConfig.quantizationValue);

            for (auto& seg : segments[p]) {
                ctx.checkCancel();

                if (transform) {
                    std::vector<std::vector<double>> tr(seg.size, std::vector<double>(seg.size));
                    for (int x = 0; x < seg.size; x++) {
                        for (int y = 0; y < seg.size; y++) {
                            tr[x][y] = (seg.size * planes.get(p, seg.x + x, seg.y + y)) / static_cast<float>(chConfig.transformScale);
                        }
                    }
                    tr = transform->reverse(tr);
                    planes.setSegment(p, seg, tr, chConfig.clampMethod);
                }

                if (pq > 0) {
                    quantize(planes, p, seg, pq, false);
                }

                auto pred = predict(seg.predType, planes, p, seg, ctx);
                if (capture && capture->segments) {
                    capture->blocks[p].push_back({seg.x, seg.y, seg.size, ctx.lastMethod});
                }
                planes.add(p, seg, pred, chConfig.clampMethod);
            }
        }

        result.image.width = width;
        result.image.height = height;
        result.image.pixels = planes.toPixels();
        result.ok = true;
    } catch (const Cancelled&) {
        result = DecodeResult{};
        result.cancelled = true;
        result.error = "Cancelled";
    } catch (const std::exception& e) {
        result = DecodeResult{};
        result.error = std::string("Decoding failed: ") + e.what();
    }
    return result;
}

} // namespace glic
