#pragma once

// Text metadata in PNG files (design §5.3): an uncompressed iTXt chunk with a
// keyword, inserted before IEND, and read back. Reading never trusts lengths.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace glic::png {

// Returns `png` with an iTXt chunk (keyword -> UTF-8 text) inserted before IEND.
// Returns an empty vector if `png` is not a valid PNG.
std::vector<uint8_t> addText(const std::vector<uint8_t>& png, const std::string& keyword, const std::string& text);

// The text of the first iTXt or tEXt chunk with this keyword, if any.
std::optional<std::string> readText(const std::vector<uint8_t>& png, const std::string& keyword);

bool isPng(const std::vector<uint8_t>& bytes);

} // namespace glic::png
