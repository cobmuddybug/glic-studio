#include "png_meta.hpp"

#include <array>
#include <cstring>

namespace glic::png {

namespace {

const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 3; i >= 0; i--) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

struct Chunk {
    size_t offset;  // of the length field
    uint32_t length;
    char type[5];
};

// Walks chunks; stops at the first malformed one.
std::vector<Chunk> chunks(const std::vector<uint8_t>& png) {
    std::vector<Chunk> out;
    size_t pos = 8;
    while (pos + 12 <= png.size()) {
        Chunk c{};
        c.offset = pos;
        c.length = be32(&png[pos]);
        std::memcpy(c.type, &png[pos + 4], 4);
        if (c.length > png.size() - pos - 12) break;
        out.push_back(c);
        pos += 12 + c.length;
        if (std::strcmp(c.type, "IEND") == 0) break;
    }
    return out;
}

} // namespace

bool isPng(const std::vector<uint8_t>& bytes) {
    return bytes.size() >= 8 && std::memcmp(bytes.data(), kSignature, 8) == 0;
}

std::vector<uint8_t> addText(const std::vector<uint8_t>& png, const std::string& keyword, const std::string& text) {
    if (!isPng(png)) return {};
    size_t iend = 0;
    for (const Chunk& c : chunks(png))
        if (std::strcmp(c.type, "IEND") == 0) iend = c.offset;
    if (iend == 0) return {};

    // iTXt: keyword \0 compression-flag compression-method language \0 translated-keyword \0 text
    std::vector<uint8_t> body(keyword.begin(), keyword.end());
    body.push_back(0);
    body.push_back(0);  // uncompressed
    body.push_back(0);
    body.push_back(0);  // no language tag
    body.push_back(0);  // no translated keyword
    body.insert(body.end(), text.begin(), text.end());

    std::vector<uint8_t> chunk;
    put32(chunk, static_cast<uint32_t>(body.size()));
    const size_t typeAt = chunk.size();
    chunk.insert(chunk.end(), {'i', 'T', 'X', 't'});
    chunk.insert(chunk.end(), body.begin(), body.end());
    put32(chunk, crc32(&chunk[typeAt], chunk.size() - typeAt) ^ 0xFFFFFFFFu);

    std::vector<uint8_t> out(png.begin(), png.begin() + static_cast<std::ptrdiff_t>(iend));
    out.insert(out.end(), chunk.begin(), chunk.end());
    out.insert(out.end(), png.begin() + static_cast<std::ptrdiff_t>(iend), png.end());
    return out;
}

std::optional<std::string> readText(const std::vector<uint8_t>& png, const std::string& keyword) {
    if (!isPng(png)) return std::nullopt;
    for (const Chunk& c : chunks(png)) {
        const bool itxt = std::strcmp(c.type, "iTXt") == 0, text = std::strcmp(c.type, "tEXt") == 0;
        if (!itxt && !text) continue;
        const uint8_t* d = &png[c.offset + 8];
        const size_t n = c.length;
        size_t k = 0;
        while (k < n && d[k] != 0) k++;
        if (k == n || std::string(reinterpret_cast<const char*>(d), k) != keyword) continue;
        size_t pos = k + 1;
        if (text) return std::string(reinterpret_cast<const char*>(d + pos), n - pos);
        if (pos + 2 > n || d[pos] != 0) continue;  // compressed iTXt is not supported
        pos += 2;
        for (int skip = 0; skip < 2; skip++) {     // language tag, translated keyword
            while (pos < n && d[pos] != 0) pos++;
            if (pos >= n) break;
            pos++;
        }
        if (pos > n) continue;
        return std::string(reinterpret_cast<const char*>(d + pos), n - pos);
    }
    return std::nullopt;
}

} // namespace glic::png
