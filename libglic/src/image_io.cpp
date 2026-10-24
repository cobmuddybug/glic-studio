#include <glic/image_io.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <fstream>

namespace glic {

namespace {

std::optional<Image> fromRgba(unsigned char* data, int width, int height, std::string* error) {
    if (!data) {
        if (error) *error = stbi_failure_reason() ? stbi_failure_reason() : "unknown error";
        return std::nullopt;
    }
    Image img;
    img.width = width;
    img.height = height;
    img.pixels.resize(static_cast<size_t>(width) * height);
    for (size_t i = 0; i < img.pixels.size(); i++) {
        img.pixels[i] = makeColor(data[i * 4], data[i * 4 + 1], data[i * 4 + 2], data[i * 4 + 3]);
    }
    stbi_image_free(data);
    return img;
}

std::vector<unsigned char> toRgba(const Image& image) {
    std::vector<unsigned char> data(image.pixels.size() * 4);
    for (size_t i = 0; i < image.pixels.size(); i++) {
        data[i * 4] = getR(image.pixels[i]);
        data[i * 4 + 1] = getG(image.pixels[i]);
        data[i * 4 + 2] = getB(image.pixels[i]);
        data[i * 4 + 3] = getA(image.pixels[i]);
    }
    return data;
}

void appendBytes(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

} // namespace

std::optional<Image> loadImage(const std::string& path, std::string* error) {
    int width, height, channels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    return fromRgba(data, width, height, error);
}

std::optional<Image> loadImageFromMemory(const uint8_t* bytes, size_t size, std::string* error) {
    int width, height, channels;
    unsigned char* data = stbi_load_from_memory(bytes, static_cast<int>(size), &width, &height, &channels, 4);
    return fromRgba(data, width, height, error);
}

bool saveImage(const std::string& path, const Image& image) {
    auto data = toRgba(image);
    return stbi_write_png(path.c_str(), image.width, image.height, 4, data.data(), image.width * 4) != 0;
}

std::vector<uint8_t> encodePng(const Image& image) {
    auto data = toRgba(image);
    std::vector<uint8_t> out;
    stbi_write_png_to_func(appendBytes, &out, image.width, image.height, 4, data.data(), image.width * 4);
    return out;
}

std::optional<std::vector<uint8_t>> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;
    std::streamoff size = file.tellg();
    if (size < 0) return std::nullopt;
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) return std::nullopt;
    return bytes;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

} // namespace glic
