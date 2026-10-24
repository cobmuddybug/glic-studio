// Port of GLIC's codec.pde: encode()/GlicCodecWriter and decode()/GlicCodecReader.
// The reader is emulated over the byte buffer with Java stream semantics, so
// truncated or corrupt files decode exactly as they would in GLIC.

#include <glic/glic.hpp>

#include "bitio.hpp"
#include "colorspaces.hpp"
#include "jvm.hpp"
#include "planes.hpp"
#include "predict.hpp"
#include "wavelets.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>

namespace glic {

using namespace jvm;

namespace {

constexpr int kMaxEncodeDimension = 16384;
constexpr int ENCODING_PACKED = 1;
constexpr int ENCODING_RLE = 2;
constexpr int WAVELETNO = 68;
constexpr int TRANSTYPENO = 2;

const uint8_t kSegmentationMark[] = {'S', 'E', 'G', 'M', 'E', 'N', 'T', 'A', 'T', 'I', 'O', 'N', ' '};
const uint8_t kPredictDataMark[] = {'P', 'R', 'E', 'D', 'I', 'C', 'T', 'D', 'A', 'T', 'A', ' '};
const uint8_t kImageDataMark[] = {'I', 'M', 'A', 'G', 'E', 'D', 'A', 'T', 'A', ' '};
constexpr size_t kSeparator = 512;

float quantValue(int v) { return v / 2.0f; }
float transCompressionValue(float v) { return 50 * sq(v / 255.0f); }

// Thrown where GLIC lets an exception escape encode()/decode() (which then fail).
struct CodecFailure {
    const char* what;
};

void quantize(Planes& planes, int p, const Segment& s, float val, bool forward) {
    if (!(val > 1)) return;
    for (int x = 0; x < s.size; x++) {
        for (int y = 0; y < s.size; y++) {
            float col = static_cast<float>(planes.get(p, x + s.x, y + s.y));
            col = forward ? col / val : col * val;
            planes.set(p, x + s.x, y + s.y, round(col));
        }
    }
}

// createWavelet: unknown ids pick a random one, as GLIC does.
const WaveletDef* createWavelet(int id, CodecContext& ctx) {
    while (true) {
        if (const WaveletDef* w = findWavelet(id)) return w;
        id = toInt(ctx.rng.random(1, static_cast<float>(WAVELETNO)));
    }
}

// createTransform: unknown types pick a random one.
bool createTransformIsPacket(int type, CodecContext& ctx) {
    while (type != 0 && type != 1) type = toInt(ctx.rng.random(static_cast<float>(TRANSTYPENO)));
    return type == 1;
}

int packedBits(int scale) { return ceil(log(static_cast<float>(scale)) / log(2.0f)); }

// ------------------------------------------------------------------- writer

struct Writer {
    std::vector<uint8_t> o;
    size_t currentWritten = 0;

    void byte(int v) { o.push_back(static_cast<uint8_t>(v & 0xFF)); }
    void shortBE(int v) {
        byte(v >> 8);
        byte(v);
    }
    void intBE(int v) {
        byte(v >> 24);
        byte(v >> 16);
        byte(v >> 8);
        byte(v);
    }
    void array(const std::vector<uint8_t>& a) { o.insert(o.end(), a.begin(), a.end()); }
    void separator(size_t n, int val) { o.insert(o.end(), n, static_cast<uint8_t>(val)); }
    void align(size_t bytes) {
        size_t written = o.size() - currentWritten;
        if (written < bytes) separator(bytes - written, 0);
        currentWritten = o.size();
    }
    void channelMark(int ch) {
        byte(0x43);
        byte(0x48);
        byte(0x30);
        byte(0x31 + ch);
    }
    void putIntAt(size_t at, int v) {
        o[at] = static_cast<uint8_t>(v >> 24);
        o[at + 1] = static_cast<uint8_t>(v >> 16);
        o[at + 2] = static_cast<uint8_t>(v >> 8);
        o[at + 3] = static_cast<uint8_t>(v);
    }
};

struct ChannelCfg {
    int minBlock, maxBlock;
    float thr;
    int encoding, prediction, quant, clamp, ttype, wavelet;
    float compress;
    int scale;
};

void emitPackedBits(BitOutput& out, const ChannelCfg& c, int bits, int val) {
    if (c.wavelet == 0) {
        if (c.clamp == CLAMP_NONE) out.writeInt(false, 9, val);
        else if (c.clamp == CLAMP_MOD256) out.writeInt(true, 8, val);
    } else {
        out.writeInt(false, bits + 1, val);
    }
}

void writeData(Writer& w, const ChannelCfg& c, const Planes& p, int pno, const std::vector<Segment>& segs) {
    if (c.encoding == ENCODING_PACKED) {
        std::vector<uint8_t> buf;
        BitOutput out(buf);
        int bits = packedBits(c.scale);
        for (const auto& s : segs)
            for (int x = 0; x < s.size; x++)
                for (int y = 0; y < s.size; y++) emitPackedBits(out, c, bits, p.get(pno, s.x + x, s.y + y));
        out.align(1);
        w.array(buf);
    } else if (c.encoding == ENCODING_RLE) {
        std::vector<uint8_t> buf;
        BitOutput out(buf);
        int bits = packedBits(c.scale);
        int currentval = 0;
        bool firstval = true;
        int currentcnt = 0;
        for (const auto& s : segs) {
            for (int x = 0; x < s.size; x++) {
                for (int y = 0; y < s.size; y++) {
                    int val = p.get(pno, s.x + x, s.y + y);
                    if (firstval) {
                        currentval = val;
                        currentcnt = 1;
                        firstval = false;
                    } else if (currentval != val || currentcnt == 129) {
                        if (currentcnt == 1) {
                            out.writeBoolean(false);
                        } else {
                            out.writeBoolean(true);
                            out.writeInt(true, 7, currentcnt - 2);
                        }
                        emitPackedBits(out, c, bits, currentval);
                        currentval = val;
                        currentcnt = 1;
                    } else {
                        currentcnt++;
                    }
                }
            }
        }
        // GLIC tests currentval here, not currentcnt; preserved.
        if (currentval == 1) {
            out.writeBoolean(false);
        } else {
            out.writeBoolean(true);
            out.writeInt(true, 7, currentcnt - 2);
        }
        emitPackedBits(out, c, bits, currentval);
        out.align(1);
        w.array(buf);
    } else {
        for (const auto& s : segs)
            for (int x = 0; x < s.size; x++)
                for (int y = 0; y < s.size; y++) w.intBE(p.get(pno, s.x + x, s.y + y));
    }
}

void capturePlane(const Planes& planes, int p, std::vector<int32_t>& out) {
    out.resize(static_cast<size_t>(planes.w) * planes.h);
    for (int y = 0; y < planes.h; y++)
        for (int x = 0; x < planes.w; x++) out[static_cast<size_t>(y) * planes.w + x] = planes.get(p, x, y);
}

void resetCapture(Capture* capture, const Planes& planes) {
    if (!capture) return;
    capture->width = planes.w;
    capture->height = planes.h;
    capture->rootSize = max(planes.ww, planes.hh);
    for (int p = 0; p < 3; p++) {
        capture->blocks[p].clear();
        capture->planeValues[p].clear();
    }
}

// ------------------------------------------------------------------- reader

// A view returned by readArray: `real` bytes from the file, zero-filled up to `size`.
struct ArrayView {
    const uint8_t* data;
    size_t real;
    size_t size;
};

class ZeroPaddedInput {
public:
    explicit ZeroPaddedInput(ArrayView v) : v_(v) {}
    bool eof() const { return pos_ >= v_.size; }
    int readUnsignedByte() {
        if (pos_ >= v_.size) throw BitInputExhausted{};
        size_t i = pos_++;
        return i < v_.real ? v_.data[i] : 0;
    }
    int readShort() {
        int a = readUnsignedByte();
        int b = readUnsignedByte();
        return static_cast<int16_t>((a << 8) | b);
    }

private:
    ArrayView v_;
    size_t pos_ = 0;
};

// DataInputStream over the whole file.
class Stream {
public:
    explicit Stream(const std::vector<uint8_t>& b) : b_(b) {}

    int readUnsignedByte() {
        if (pos_ >= b_.size()) throw CodecFailure{"Unexpected end of file"};
        return b_[pos_++];
    }
    int readInt() {
        int v = 0;
        for (int i = 0; i < 4; i++) v = static_cast<int>(static_cast<uint32_t>(v) << 8) | readUnsignedByte();
        return v;
    }
    // skip(): reads byte by byte and silently stops at end of file.
    void skip(size_t n) { pos_ = std::min(pos_ + n, b_.size()); }
    // readArray(): readFully, ignoring EOF (the tail stays zero).
    ArrayView readArray(size_t n) {
        size_t real = std::min(n, b_.size() - pos_);
        ArrayView v{b_.data() + pos_, real, n};
        pos_ += real;
        return v;
    }
    size_t pos() const { return pos_; }
    bool eof() const { return pos_ >= b_.size(); }

private:
    const std::vector<uint8_t>& b_;
    size_t pos_ = 0;
};

class PaddedBitInput {
public:
    explicit PaddedBitInput(ArrayView v) : storage_(), in_(nullptr, 0) {
        if (v.real == v.size) {
            in_ = BitInput(v.data, v.size);
        } else {
            storage_.assign(v.size, 0);
            if (v.real) std::memcpy(storage_.data(), v.data, v.real);
            in_ = BitInput(storage_.data(), storage_.size());
        }
    }
    BitInput& get() { return in_; }

private:
    std::vector<uint8_t> storage_;
    BitInput in_;
};

struct Header {
    int storedW = 0, storedH = 0, w = 0, h = 0;
    DecodeParams params;
    int segSizes[4] = {}, predSizes[4] = {}, dataSizes[4] = {};
};

Header readHeaders(Stream& o) {
    Header hd;
    o.skip(4);
    hd.storedW = o.readInt();
    hd.storedH = o.readInt();
    hd.w = max(64, abs(hd.storedW) % 8192);
    hd.h = max(64, abs(hd.storedH) % 8192);
    hd.params.colorSpace = o.readUnsignedByte();
    for (int i = 0; i < 3; i++) hd.params.border[i] = static_cast<uint8_t>(o.readUnsignedByte());
    for (int i = 0; i < 4; i++) hd.segSizes[i] = o.readInt() & 0x7ffff;
    for (int i = 0; i < 4; i++) hd.predSizes[i] = o.readInt() & 0xffffff;
    for (int i = 0; i < 4; i++) hd.dataSizes[i] = o.readInt() & 0x3ffffff;
    o.skip(128 - 16 - 16 - 16 - 16);
    for (int p = 0; p < 3; p++) {
        auto& c = hd.params.channels[p];
        o.skip(4);
        c.prediction = o.readUnsignedByte();
        c.quantization = o.readUnsignedByte();
        c.clamp = o.readUnsignedByte();
        c.wavelet = o.readUnsignedByte();
        c.transformType = o.readUnsignedByte();
        c.scale = o.readInt();
        c.encoding = o.readUnsignedByte();
        o.skip(32 - 4 - 6 - 4);
    }
    return hd;
}

int decodePackedBits(BitInput& in, const ChannelHeader& c, int bits) {
    if (c.wavelet == 0) {
        if (c.clamp == CLAMP_NONE) return in.readInt(false, 9);
        if (c.clamp == CLAMP_MOD256) return in.readInt(true, 8);
        return 0;
    }
    return in.readInt(false, bits + 1);
}

void readData(Stream& o, const ChannelHeader& c, int dataSize, Planes& p, int pno, const std::vector<Segment>& segs) {
    if (c.encoding == ENCODING_PACKED) {
        PaddedBitInput pin(o.readArray(static_cast<size_t>(dataSize)));
        BitInput& in = pin.get();
        int bits = packedBits(c.scale);
        try {
            for (const auto& s : segs)
                for (int x = 0; x < s.size; x++)
                    for (int y = 0; y < s.size; y++) p.set(pno, s.x + x, s.y + y, decodePackedBits(in, c, bits));
        } catch (const BitInputExhausted&) {
            // "decode packed failed": the rest of the channel keeps the border colour
        }
    } else if (c.encoding == ENCODING_RLE) {
        PaddedBitInput pin(o.readArray(static_cast<size_t>(dataSize)));
        BitInput& in = pin.get();
        int bits = packedBits(c.scale);
        int currentval = 0;
        bool doReadType = true;
        int currentcnt = 0;
        try {
            for (const auto& s : segs) {
                for (int x = 0; x < s.size; x++) {
                    for (int y = 0; y < s.size; y++) {
                        if (doReadType) {
                            if (in.readBoolean()) {
                                currentcnt = in.readInt(true, 7) + 2;
                                doReadType = false;
                            }
                            currentval = decodePackedBits(in, c, bits);
                        }
                        p.set(pno, s.x + x, s.y + y, currentval);
                        currentcnt--;
                        if (currentcnt <= 0) doReadType = true;
                    }
                }
            }
        } catch (const BitInputExhausted&) {
        }
    } else {
        // decode_raw reads ints straight from the file stream.
        try {
            int idx = 0;
            for (const auto& s : segs) {
                for (int x = 0; x < s.size; x++) {
                    for (int y = 0; y < s.size; y++) {
                        if (idx < dataSize) {
                            p.set(pno, s.x + x, s.y + y, o.readInt());
                            idx += 4;
                        }
                    }
                }
            }
        } catch (const CodecFailure&) {
            // IOException at end of file: "decode raw failed"
        }
    }
}

} // namespace

// ---------------------------------------------------------------------- API

DecodeParams decodeParamsFor(const EncodeSettings& s) {
    DecodeParams d;
    d.colorSpace = static_cast<int>(s.colorSpace) & 0xFF;
    d.border = s.border;
    for (int p = 0; p < 3; p++) {
        const auto& c = s.channels[p];
        auto& h = d.channels[p];
        int pred = static_cast<int>(c.prediction);
        h.prediction = (pred < 0 ? 0 : pred) & 0xFF;
        h.quantization = c.quantization & 0xFF;
        h.clamp = static_cast<int>(c.clamp) & 0xFF;
        h.wavelet = c.wavelet & 0xFF;  // RANDOM is resolved at encode time; this is a placeholder
        h.transformType = static_cast<int>(c.transformType) & 0xFF;
        h.scale = c.scale;
        h.encoding = static_cast<int>(c.encoding) & 0xFF;
    }
    return d;
}

std::string validate(const EncodeSettings& s) {
    for (int p = 0; p < 3; p++) {
        // GLIC recurses forever (StackOverflowError) when the max block size is < 1.
        if (s.channels[p].maxBlockSize < 1) return "channel " + std::to_string(p) + ": max block size must be >= 1";
    }
    return {};
}

GlicLayout GlicData::layout() const {
    GlicLayout l;
    Stream o(bytes);
    try {
        Header hd = readHeaders(o);
        l.storedWidth = hd.storedW;
        l.storedHeight = hd.storedH;
        l.width = hd.w;
        l.height = hd.h;
        l.params = hd.params;
        o.skip(sizeof kSegmentationMark);
        l.payloadOffset = o.pos();
        for (int p = 0; p < 3; p++) {
            o.skip(4);
            l.segmentation[p] = {o.pos(), static_cast<size_t>(hd.segSizes[p])};
            o.readArray(static_cast<size_t>(hd.segSizes[p]));
        }
        o.skip(kSeparator + sizeof kPredictDataMark);
        for (int p = 0; p < 3; p++) {
            o.skip(4);
            l.prediction[p] = {o.pos(), static_cast<size_t>(hd.predSizes[p])};
            o.readArray(static_cast<size_t>(hd.predSizes[p]));
        }
        o.skip(kSeparator + sizeof kImageDataMark);
        for (int p = 0; p < 3; p++) {
            o.skip(4);
            l.data[p] = {o.pos(), static_cast<size_t>(hd.dataSizes[p])};
            o.readArray(static_cast<size_t>(hd.dataSizes[p]));
        }
    } catch (const CodecFailure&) {
        // truncated header: return what was read
    }
    return l;
}

EncodeResult encode(const Image& image, const EncodeSettings& settings, const Hooks* hooks) {
    EncodeResult result;
    if (auto err = validate(settings); !err.empty()) {
        result.error = err;
        return result;
    }
    if (image.width < 1 || image.height < 1 || image.width > kMaxEncodeDimension ||
        image.height > kMaxEncodeDimension || image.pixels.size() != static_cast<size_t>(image.width) * image.height) {
        result.error = "Invalid image dimensions";
        return result;
    }

    CodecContext ctx;
    ctx.rng.seed(settings.seed);
    ctx.cancel = hooks ? hooks->cancel : nullptr;
    Capture* capture = hooks ? hooks->capture : nullptr;

    try {
        const int w = image.width;
        const int h = image.height;
        ChannelCfg cfg[3];
        for (int p = 0; p < 3; p++) {
            const auto& c = settings.channels[p];
            cfg[p] = {c.minBlockSize, c.maxBlockSize, c.threshold, static_cast<int>(c.encoding),
                      static_cast<int>(c.prediction), c.quantization, static_cast<int>(c.clamp),
                      static_cast<int>(c.transformType), c.wavelet, c.compress, c.scale};
        }
        const int cs = static_cast<int>(settings.colorSpace);
        const int colorOutside = static_cast<int>(0xff000000u | (uint32_t(settings.border[0]) << 16) |
                                                  (uint32_t(settings.border[1]) << 8) | settings.border[2]);

        Writer wr;
        // First header: fourcc, size, colour space, border colour.
        wr.byte('G');
        wr.byte('L');
        wr.byte('I');
        wr.byte('C');
        wr.intBE(w);
        wr.intBE(h);
        wr.byte(cs);
        wr.byte((colorOutside >> 16) & 0xff);
        wr.byte((colorOutside >> 8) & 0xff);
        wr.byte(colorOutside & 0xff);
        wr.align(128);

        // Resolve random wavelet/transform choices (stored resolved).
        for (int p = 0; p < 3; p++) {
            if (cfg[p].wavelet == kWaveletRandom) cfg[p].wavelet = toInt(ctx.rng.random(1, static_cast<float>(WAVELETNO)));
            if (cfg[p].ttype == static_cast<int>(TransformType::RANDOM))
                cfg[p].ttype = toInt(ctx.rng.random(static_cast<float>(TRANSTYPENO)));
        }

        // Second header, one 32-byte block per channel.
        for (int p = 0; p < 3; p++) {
            wr.channelMark(p);
            wr.byte(cfg[p].prediction < 0 ? 0 : cfg[p].prediction);
            wr.byte(cfg[p].quant);
            wr.byte(cfg[p].clamp);
            wr.byte(cfg[p].wavelet);
            wr.byte(cfg[p].ttype);
            wr.intBE(cfg[p].scale);
            wr.byte(cfg[p].encoding);
            wr.align(32);
        }

        Planes planes(image.pixels.data(), w, h, cs, RefColor(colorOutside, cs));
        resetCapture(capture, planes);

        int segSizes[4] = {}, predSizes[4] = {}, dataSizes[4] = {};
        std::vector<Segment> segments[3];

        wr.o.insert(wr.o.end(), std::begin(kSegmentationMark), std::end(kSegmentationMark));
        for (int p = 0; p < 3; p++) {
            wr.channelMark(p);
            std::vector<uint8_t> bits;
            BitOutput out(bits);
            segments[p] = makeSegmentation(out, planes, p, cfg[p].minBlock, cfg[p].maxBlock, cfg[p].thr, ctx);
            out.align(1);
            segSizes[p] = static_cast<int>(bits.size());
            wr.array(bits);
        }
        wr.separator(kSeparator, 0xff);

        Planes res = planes.cloneEmpty();

        for (int p = 0; p < 3; p++) {
            const ChannelCfg& c = cfg[p];
            const WaveletDef* wavelet = c.wavelet == 0 ? nullptr : createWavelet(c.wavelet, ctx);
            std::unique_ptr<WaveletTransform> trans;
            if (wavelet) trans = std::make_unique<WaveletTransform>(*wavelet, createTransformIsPacket(c.ttype, ctx));
            const bool comp = c.compress > 0;
            const double compThreshold = comp ? static_cast<double>(transCompressionValue(c.compress)) : 0.;
            const float pq = quantValue(c.quant);
            const bool metaSad = c.prediction == PRED_SAD || c.prediction == PRED_BSAD;

            for (Segment& s : segments[p]) {
                ctx.checkCancel();

                std::vector<int> pred = predict(c.prediction, planes, p, s, ctx);
                if (capture && capture->segments)
                    capture->blocks[p].push_back({s.x, s.y, s.size, metaSad ? s.predType : ctx.lastMethod});
                planes.subtract(p, s, pred, c.clamp);

                if (pq > 0) quantize(planes, p, s, pq, true);

                if (trans) {
                    Matrix tr = planes.getSegment(p, s);
                    if (trans->forward(tr)) {
                        if (comp) tr = compressMagnitude(tr, compThreshold);
                        for (int x = 0; x < s.size; x++)
                            for (int y = 0; y < s.size; y++)
                                planes.set(p, s.x + x, s.y + y,
                                           round(static_cast<float>((tr[x][y] * c.scale) / static_cast<float>(s.size))));
                    }
                }

                for (int x = 0; x < s.size; x++)
                    for (int y = 0; y < s.size; y++) res.set(p, s.x + x, s.y + y, planes.get(p, s.x + x, s.y + y));

                if (trans) {
                    Matrix tr(s.size, std::vector<double>(s.size));
                    for (int xx = 0; xx < s.size; xx++)
                        for (int yy = 0; yy < s.size; yy++)
                            tr[xx][yy] = (s.size * planes.get(p, s.x + xx, s.y + yy)) / static_cast<float>(c.scale);
                    if (trans->reverse(tr)) planes.setSegment(p, s, tr, c.clamp);
                }

                if (pq > 0) quantize(planes, p, s, pq, false);

                pred = predict(s.predType, planes, p, s, ctx);
                planes.add(p, s, pred, c.clamp);
            }
            if (capture && capture->values) capturePlane(res, p, capture->planeValues[p]);
        }

        wr.o.insert(wr.o.end(), std::begin(kPredictDataMark), std::end(kPredictDataMark));
        for (int p = 0; p < 3; p++) {
            wr.channelMark(p);
            size_t start = wr.o.size();
            for (const Segment& s : segments[p]) {
                wr.byte(cfg[p].prediction < 0 ? s.predType : PRED_NONE);
                wr.shortBE(s.refx);
                wr.shortBE(s.refy);
                wr.byte(s.refa);
                wr.shortBE(toInt(0x7000 * s.angle));
            }
            predSizes[p] = static_cast<int>(wr.o.size() - start);
        }
        wr.separator(kSeparator, 0xff);

        wr.o.insert(wr.o.end(), std::begin(kImageDataMark), std::end(kImageDataMark));
        for (int p = 0; p < 3; p++) {
            ctx.checkCancel();
            wr.channelMark(p);
            size_t start = wr.o.size();
            writeData(wr, cfg[p], res, p, segments[p]);
            dataSizes[p] = static_cast<int>(wr.o.size() - start);
        }

        for (int i = 0; i < 4; i++) {
            wr.putIntAt(16 + 4 * i, segSizes[i]);
            wr.putIntAt(32 + 4 * i, predSizes[i]);
            wr.putIntAt(48 + 4 * i, dataSizes[i]);
        }

        result.data.bytes = std::move(wr.o);
        result.ok = true;
    } catch (const Cancelled&) {
        result = EncodeResult{};
        result.cancelled = true;
        result.error = "Cancelled";
    } catch (const BitSizeError&) {
        result = EncodeResult{};
        result.error = "Encoding failed: invalid packed bit width (transform scale)";
    } catch (const std::bad_alloc&) {
        result = EncodeResult{};
        result.error = "Encoding failed: out of memory";
    }
    return result;
}

DecodeResult decode(const GlicData& data, const DecodeSettings& settings, const Hooks* hooks) {
    DecodeResult result;
    CodecContext ctx;
    ctx.rng.seed(settings.seed);
    ctx.cancel = hooks ? hooks->cancel : nullptr;
    Capture* capture = hooks ? hooks->capture : nullptr;

    try {
        Stream o(data.bytes);
        Header hd = readHeaders(o);
        if (settings.override) {
            // GLIC's do_skip_header: take the settings instead of the header.
            const DecodeParams& ov = *settings.override;
            hd.params.colorSpace = ov.colorSpace;
            hd.params.border = ov.border;
            for (int p = 0; p < 3; p++) {
                hd.params.channels[p] = ov.channels[p];
                hd.params.channels[p].prediction = max(0, ov.channels[p].prediction);
            }
        }
        if (settings.maxPixels > 0 && static_cast<int64_t>(hd.w) * hd.h > settings.maxPixels) {
            result.error = "Decoded size " + std::to_string(hd.w) + "x" + std::to_string(hd.h) + " exceeds the limit";
            return result;
        }

        const DecodeParams& prm = hd.params;
        const int cs = prm.colorSpace;
        const int color = static_cast<int>(0xff000000u | (uint32_t(prm.border[0]) << 16) |
                                           (uint32_t(prm.border[1]) << 8) | prm.border[2]);
        Planes planes(hd.w, hd.h, cs, RefColor(color, cs));
        resetCapture(capture, planes);

        std::vector<Segment> segments[3];
        o.skip(sizeof kSegmentationMark);
        for (int p = 0; p < 3; p++) {
            ctx.checkCancel();
            o.skip(4);
            PaddedBitInput pin(o.readArray(static_cast<size_t>(hd.segSizes[p])));
            segments[p] = readSegmentation(pin.get(), planes);
        }

        o.skip(kSeparator);
        o.skip(sizeof kPredictDataMark);
        for (int p = 0; p < 3; p++) {
            o.skip(4);
            ZeroPaddedInput in(o.readArray(static_cast<size_t>(hd.predSizes[p])));
            try {
                for (Segment& s : segments[p]) {
                    s.predType = in.readUnsignedByte();
                    s.predType = s.predType == PRED_NONE ? prm.channels[p].prediction : s.predType;
                    s.refx = in.readShort();
                    s.refy = in.readShort();
                    s.refa = in.readUnsignedByte() % 3;
                    s.angle = static_cast<float>(in.readShort()) / 0x7000;
                }
            } catch (const BitInputExhausted&) {
                // EOFException: remaining segments keep their defaults
            }
        }

        o.skip(kSeparator);
        o.skip(sizeof kImageDataMark);
        for (int p = 0; p < 3; p++) {
            ctx.checkCancel();
            o.skip(4);
            readData(o, prm.channels[p], hd.dataSizes[p], planes, p, segments[p]);
            if (capture && capture->values) capturePlane(planes, p, capture->planeValues[p]);
        }

        for (int p = 0; p < 3; p++) {
            const ChannelHeader& c = prm.channels[p];
            const WaveletDef* wavelet = c.wavelet == 0 ? nullptr : createWavelet(c.wavelet, ctx);
            std::unique_ptr<WaveletTransform> trans;
            if (wavelet) trans = std::make_unique<WaveletTransform>(*wavelet, createTransformIsPacket(c.transformType, ctx));
            const float pq = quantValue(c.quantization);

            for (Segment& s : segments[p]) {
                ctx.checkCancel();
                if (trans) {
                    Matrix tr(s.size, std::vector<double>(s.size));
                    for (int xx = 0; xx < s.size; xx++)
                        for (int yy = 0; yy < s.size; yy++)
                            tr[xx][yy] = (s.size * planes.get(p, s.x + xx, s.y + yy)) / static_cast<float>(c.scale);
                    if (trans->reverse(tr)) planes.setSegment(p, s, tr, c.clamp);
                }
                if (pq > 0) quantize(planes, p, s, pq, false);
                std::vector<int> pred = predict(s.predType, planes, p, s, ctx);
                if (capture && capture->segments) capture->blocks[p].push_back({s.x, s.y, s.size, ctx.lastMethod});
                planes.add(p, s, pred, c.clamp);
            }
        }

        result.image.width = planes.w;
        result.image.height = planes.h;
        result.image.pixels = planes.toPixels();
        result.ok = true;
    } catch (const Cancelled&) {
        result = DecodeResult{};
        result.cancelled = true;
        result.error = "Cancelled";
    } catch (const CodecFailure& e) {
        result = DecodeResult{};
        result.error = std::string("Decoding failed: ") + e.what;
    } catch (const BitSizeError&) {
        result = DecodeResult{};
        result.error = "Decoding failed: invalid packed bit width (transform scale)";
    } catch (const std::bad_alloc&) {
        result = DecodeResult{};
        result.error = "Decoding failed: out of memory";
    }
    return result;
}

} // namespace glic
