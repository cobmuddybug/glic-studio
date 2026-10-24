#pragma once

#include <glic/glic.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace glic {

// PNG/JPG/BMP/... via stb_image, converted to ARGB.
std::optional<Image> loadImage(const std::string& path, std::string* error = nullptr);
std::optional<Image> loadImageFromMemory(const uint8_t* data, size_t size, std::string* error = nullptr);

// RGBA PNG via stb_image_write.
bool saveImage(const std::string& path, const Image& image);
std::vector<uint8_t> encodePng(const Image& image);

std::optional<std::vector<uint8_t>> readFile(const std::string& path);
bool writeFile(const std::string& path, const std::vector<uint8_t>& bytes);

} // namespace glic
