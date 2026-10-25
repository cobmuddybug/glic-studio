#include "preset.hpp"

#include "png_meta.hpp"
#include "spec.hpp"

#include <glic/image_io.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace glic::preset {

using nlohmann::json;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

struct ParseError {
    std::string message;
};

// An enum value written as a name, or as GLIC's integer id.
template <typename NameFn>
int enumValue(const json& v, int lo, int hi, NameFn name, const char* what) {
    if (v.is_number_integer()) {
        int i = v.get<int>();
        if (i < lo || i > hi) throw ParseError{std::string(what) + " id out of range: " + std::to_string(i)};
        return i;
    }
    if (v.is_string()) {
        const std::string want = lower(v.get<std::string>());
        for (int i = lo; i <= hi; i++)
            if (lower(std::string(name(i))) == want) return i;
        throw ParseError{std::string("unknown ") + what + ": " + v.get<std::string>()};
    }
    throw ParseError{std::string(what) + " must be a name or a number"};
}

int colorSpaceOf(const json& v) {
    return enumValue(v, 0, kColorSpaceCount - 1, [](int i) { return colorSpaceName(static_cast<ColorSpace>(i)); },
                     "colour space");
}
int predictionOf(const json& v) {
    return enumValue(v, -3, kPredictionCount - 1, [](int i) { return predictionName(static_cast<Prediction>(i)); },
                     "prediction");
}
int waveletOf(const json& v) {
    return enumValue(v, -1, kWaveletCount - 1, [](int i) { return waveletName(i); }, "wavelet");
}
int encodingOf(const json& v) {
    return enumValue(v, 0, 2, [](int i) { return encodingName(static_cast<Encoding>(i)); }, "encoding");
}
int transformOf(const json& v) {
    return enumValue(v, -1, 1, [](int i) { return i == 0 ? "FWT" : i == 1 ? "WPT" : "RANDOM"; }, "transform");
}
int clampOf(const json& v) {
    return enumValue(v, 0, 1, [](int i) { return i == 0 ? "none" : "mod256"; }, "clamp");
}

const char* transformName(TransformType t) {
    return t == TransformType::FWT ? "FWT" : t == TransformType::WPT ? "WPT" : "RANDOM";
}

void readBorder(const json& o, std::array<uint8_t, 3>& border) {
    if (!o.contains("border")) return;
    const json& b = o["border"];
    if (!b.is_array() || b.size() != 3) throw ParseError{"border must be [r, g, b]"};
    for (int i = 0; i < 3; i++) border[i] = static_cast<uint8_t>(std::clamp(b[i].get<int>(), 0, 255));
}

// Channel fields; `decodeSide` limits them to what the decoder reads.
void readChannel(const json& c, ChannelSettings& ch, bool decodeSide) {
    if (!c.is_object()) throw ParseError{"channel entries must be objects"};
    if (c.contains("prediction")) ch.prediction = static_cast<Prediction>(predictionOf(c["prediction"]));
    if (c.contains("quantization")) ch.quantization = c["quantization"].get<int>();
    if (c.contains("clamp")) ch.clamp = static_cast<ClampMethod>(clampOf(c["clamp"]));
    if (c.contains("wavelet")) ch.wavelet = waveletOf(c["wavelet"]);
    if (c.contains("transform")) ch.transformType = static_cast<TransformType>(transformOf(c["transform"]));
    if (c.contains("scale")) ch.scale = c["scale"].get<int>();
    if (c.contains("encoding")) ch.encoding = static_cast<Encoding>(encodingOf(c["encoding"]));
    if (decodeSide) return;
    if (c.contains("min_block")) ch.minBlockSize = c["min_block"].get<int>();
    if (c.contains("max_block")) ch.maxBlockSize = c["max_block"].get<int>();
    if (c.contains("threshold")) ch.threshold = c["threshold"].get<float>();
    if (c.contains("compression")) ch.compress = c["compression"].get<float>();
}

void readChannels(const json& o, std::array<ChannelSettings, 3>& channels, bool decodeSide) {
    if (!o.contains("channels")) return;
    const json& cs = o["channels"];
    // One entry applies to all channels; three set them individually.
    if (!cs.is_array() || (cs.size() != 1 && cs.size() != 3)) throw ParseError{"channels must have 1 or 3 entries"};
    for (int i = 0; i < 3; i++) readChannel(cs[cs.size() == 3 ? i : 0], channels[i], decodeSide);
}

json writeChannel(const ChannelSettings& ch, bool decodeSide, json c) {
    c["prediction"] = predictionName(ch.prediction);
    c["quantization"] = ch.quantization;
    c["clamp"] = ch.clamp == ClampMethod::MOD256 ? "mod256" : "none";
    c["wavelet"] = std::string(waveletName(ch.wavelet));
    c["transform"] = transformName(ch.transformType);
    c["scale"] = ch.scale;
    c["encoding"] = encodingName(ch.encoding);
    if (!decodeSide) {
        c["min_block"] = ch.minBlockSize;
        c["max_block"] = ch.maxBlockSize;
        c["threshold"] = ch.threshold;
        c["compression"] = ch.compress;
    }
    return c;
}

// Writes channels into `o`, merging with whatever unknown keys were there.
void writeChannels(json& o, const std::array<ChannelSettings, 3>& channels, bool decodeSide) {
    json old = o.contains("channels") && o["channels"].is_array() ? o["channels"] : json::array();
    json out = json::array();
    for (int i = 0; i < 3; i++) {
        json base = i < static_cast<int>(old.size()) && old[i].is_object() ? old[i]
                    : old.size() == 1 && old[0].is_object()              ? old[0]
                                                                         : json::object();
        out.push_back(writeChannel(channels[i], decodeSide, base));
    }
    o["channels"] = out;
}

void readCorruption(const json& c, CorruptionSettings& out) {
    if (!c.is_object()) throw ParseError{"corruption must be an object"};
    out.enabled = c.value("enabled", out.enabled);
    out.seed = c.value("seed", out.seed);
    if (c.contains("mode") && c["mode"].get<std::string>() != "random_bytes")
        throw ParseError{"unknown corruption mode \"" + c["mode"].get<std::string>() + "\""};
    out.rate = c.value("rate", out.rate);
    out.protectHeader = c.value("protect_header", out.protectHeader);
    out.regions.clear();
    if (c.contains("regions")) {
        if (!c["regions"].is_array()) throw ParseError{"corruption regions must be a list"};
        for (const json& r : c["regions"])
            out.regions.push_back({r.at("start").get<double>(), r.at("end").get<double>(), r.at("rate").get<double>()});
    }
}

void writeCorruption(json& c, const CorruptionSettings& in) {
    if (!c.is_object()) c = json::object();
    c["enabled"] = in.enabled;
    c["seed"] = in.seed;
    c["mode"] = "random_bytes";
    c["rate"] = in.rate;
    c["protect_header"] = in.protectHeader;
    json regions = json::array();
    for (const auto& r : in.regions) regions.push_back({{"start", r.start}, {"end", r.end}, {"rate", r.rate}});
    c["regions"] = regions;
}

} // namespace

std::optional<Preset> parse(const std::string& text, std::string* error) {
    auto fail = [&](const std::string& m) -> std::optional<Preset> {
        if (error) *error = m;
        return std::nullopt;
    };
    json doc = json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) return fail("not a JSON object");

    try {
        // value() throws on a wrongly typed field, so these checks are inside the try.
        if (doc.value("format", "") != kFormat) return fail("not a glic-studio preset (format field)");
        if (doc.value("version", 0) > kVersion)
            return fail("preset version " + std::to_string(doc.value("version", 0)) + " is newer than this build supports");
        Preset p;
        p.document = doc;
        p.name = doc.value("name", "");
        if (doc.contains("encode")) {
            const json& e = doc["encode"];
            if (e.contains("colorspace")) p.encode.colorSpace = static_cast<ColorSpace>(colorSpaceOf(e["colorspace"]));
            readBorder(e, p.encode.border);
            if (e.contains("seed")) p.encode.seed = e["seed"].get<int64_t>();
            readChannels(e, p.encode.channels, false);
        }
        // Decode values default to the encode values. Meta predictors become
        // NONE, as GLIC's skip-header decode does; random choices stay random.
        p.decode = p.encode;
        for (auto& ch : p.decode.channels)
            if (static_cast<int>(ch.prediction) < 0) ch.prediction = Prediction::NONE;
        if (doc.contains("decode")) {
            const json& d = doc["decode"];
            p.decodeLinked = d.value("linked", true);
            if (d.contains("colorspace")) p.decode.colorSpace = static_cast<ColorSpace>(colorSpaceOf(d["colorspace"]));
            readBorder(d, p.decode.border);
            readChannels(d, p.decode.channels, true);
        }
        p.decode.seed = p.encode.seed;
        if (doc.contains("corruption")) readCorruption(doc["corruption"], p.corruption);
        return p;
    } catch (const ParseError& e) {
        return fail(e.message);
    } catch (const json::exception& e) {
        return fail(std::string("bad value: ") + e.what());
    }
}

std::optional<Preset> load(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot read " + path;
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return parse(ss.str(), error);
}

std::string serialize(const Preset& p) {
    json doc = p.document.is_object() ? p.document : json::object();
    doc["format"] = kFormat;
    doc["version"] = kVersion;
    doc["name"] = p.name;

    json& e = doc["encode"];
    if (!e.is_object()) e = json::object();
    e["colorspace"] = colorSpaceName(p.encode.colorSpace);
    e["border"] = {p.encode.border[0], p.encode.border[1], p.encode.border[2]};
    e["seed"] = p.encode.seed;
    writeChannels(e, p.encode.channels, false);

    json& d = doc["decode"];
    if (!d.is_object()) d = json::object();
    d["linked"] = p.decodeLinked;
    d["colorspace"] = colorSpaceName(p.decode.colorSpace);
    d["border"] = {p.decode.border[0], p.decode.border[1], p.decode.border[2]};
    writeChannels(d, p.decode.channels, true);

    writeCorruption(doc["corruption"], p.corruption);

    return doc.dump(2) + "\n";
}

bool save(const std::string& path, const Preset& p) {
    std::ofstream out(path, std::ios::binary);
    out << serialize(p);
    return static_cast<bool>(out);
}

DecodeSettings decodeSettings(const Preset& p, int width, int height) {
    DecodeSettings ds;
    ds.seed = p.encode.seed;
    if (!p.decodeLinked) ds.override = paramsFromSettings(p.decode);
    if (p.corruption.enabled && width > 0 && height > 0) limitCorruptDecode(ds, width, height);
    return ds;
}

DecodeResult render(const Image& image, const Preset& p, const Hooks* hooks) {
    auto enc = encode(image, p.encode, hooks);
    if (!enc.ok) {
        DecodeResult r;
        r.cancelled = enc.cancelled;
        r.error = enc.error;
        return r;
    }
    corrupt(enc.data, p.corruption);
    return decode(enc.data, decodeSettings(p, image.width, image.height), hooks);
}

std::vector<uint8_t> exportPng(const Image& image, const Preset& p) {
    return png::addText(encodePng(image), kPngKeyword, serialize(p));
}

std::optional<Preset> fromPng(const std::vector<uint8_t>& bytes, std::string* error) {
    auto text = png::readText(bytes, kPngKeyword);
    if (!text) {
        if (error) *error = "no embedded preset";
        return std::nullopt;
    }
    return parse(*text, error);
}

} // namespace glic::preset
