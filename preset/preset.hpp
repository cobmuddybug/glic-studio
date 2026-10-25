#pragma once

// glic-studio presets (design §5.1): a versioned JSON document holding every
// setting needed to reproduce a render. Shared by the GUI and the CLI, so both
// produce identical output from the same file.
//
// Unknown fields are kept on load and written back on save (forward
// compatibility), including unknown keys inside channel objects.

#include <glic/glic.hpp>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace glic::preset {

constexpr const char* kFormat = "glic-studio-preset";
constexpr int kVersion = 1;
// PNG iTXt keyword for embedded presets (design §5.3).
constexpr const char* kPngKeyword = "glic-studio-preset";

struct Preset {
    std::string name;
    EncodeSettings encode;           // includes the seed
    bool decodeLinked = true;
    EncodeSettings decode;           // decode-side values; used when !decodeLinked
    nlohmann::json document = nlohmann::json::object();  // as loaded; unknown fields survive

    bool sameSettings(const Preset& o) const {
        return encode == o.encode && decodeLinked == o.decodeLinked && (decodeLinked || decode == o.decode);
    }
};

// Parses a preset document. Values may be names ("Haar", "PAETH") or GLIC ids.
std::optional<Preset> parse(const std::string& text, std::string* error = nullptr);
std::optional<Preset> load(const std::string& path, std::string* error = nullptr);

// Pretty-printed, deterministic (sorted keys).
std::string serialize(const Preset& p);
bool save(const std::string& path, const Preset& p);

// What to decode with.
DecodeSettings decodeSettings(const Preset& p);

// Encode + decode one image with a preset. The GUI's full-resolution render
// and `glic render --preset` both go through this.
DecodeResult render(const Image& image, const Preset& p, const Hooks* hooks = nullptr);

// A PNG of `image` with the preset embedded (iTXt "glic-studio-preset").
std::vector<uint8_t> exportPng(const Image& image, const Preset& p);
// The preset embedded in PNG bytes, if any. nullopt if absent or unreadable.
std::optional<Preset> fromPng(const std::vector<uint8_t>& png, std::string* error = nullptr);

} // namespace glic::preset
