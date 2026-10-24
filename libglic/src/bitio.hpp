#pragma once

// Reimplementation of the bit-io 1.3.7 behaviour GLIC relies on (the library
// itself is Apache-2.0; no code is copied). MSB-first; signed values are two's
// complement in `size` bits; `unsigned` reads zero-extend.

#include <cstdint>
#include <vector>

namespace glic {

// bit-io's IllegalArgumentException for an invalid size (GLIC lets it escape).
struct BitSizeError {};
// ArrayByteInput's IllegalStateException when reading past the limit.
struct BitInputExhausted {};

class BitOutput {
public:
    explicit BitOutput(std::vector<uint8_t>& sink) : out_(sink) {}

    void writeBoolean(bool v) { writeInt(true, 1, v ? 1 : 0); }
    void writeInt(bool isUnsigned, int size, int value);
    // align(bytes): pad to a multiple of `bytes` octets counted from this writer's start.
    void align(int bytes);

private:
    void unsigned8(int size, int value);
    void unsigned16(int size, int value);
    void octet(int value) {
        out_.push_back(static_cast<uint8_t>(value & 0xFF));
        count_++;
    }

    std::vector<uint8_t>& out_;
    bool flags_[8] = {};
    int index_ = 0;
    int64_t count_ = 0;
};

class BitInput {
public:
    BitInput(const uint8_t* data, size_t limit) : data_(data), limit_(limit) {}

    bool readBoolean() { return readInt(true, 1) == 1; }
    int readInt(bool isUnsigned, int size);

private:
    int unsigned8(int size);
    int unsigned16(int size);
    int octet() {
        if (pos_ >= limit_) throw BitInputExhausted{};
        count_++;
        return data_[pos_++];
    }

    const uint8_t* data_;
    size_t limit_;
    size_t pos_ = 0;
    bool flags_[8] = {};
    int index_ = 8;
    int64_t count_ = 0;
};

} // namespace glic
