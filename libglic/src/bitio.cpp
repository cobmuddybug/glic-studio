#include "bitio.hpp"

namespace glic {

namespace {

// BitIoConstraints.requireValidSize(unsigned, 5, size): 1..31 unsigned, 1..32 signed.
void requireValidIntSize(bool isUnsigned, int size) {
    if (size < 1 || size > (isUnsigned ? 31 : 32)) throw BitSizeError{};
}

// Java's int >> (arithmetic, count masked to 5 bits).
inline int sar(int v, int n) { return v >> (n & 31); }

} // namespace

void BitOutput::unsigned8(int size, int value) {
    if (size < 1 || size > 8) throw BitSizeError{};
    if (size == 8 && index_ == 0) {
        octet(value);
        return;
    }
    const int required = size - (8 - index_);
    if (required > 0) {
        unsigned8(size - required, sar(value, required));
        unsigned8(required, value);
        return;
    }
    for (int i = index_ + size - 1; i >= index_; i--) {
        flags_[i] = (value & 0x01) == 0x01;
        value = sar(value, 1);
    }
    index_ += size;
    if (index_ == 8) {
        int o = 0;
        for (int i = 0; i < 8; i++) o = (o << 1) | (flags_[i] ? 1 : 0);
        octet(o);
        index_ = 0;
    }
}

void BitOutput::unsigned16(int size, int value) {
    if (size < 1 || size > 16) throw BitSizeError{};
    const int quotient = size / 8;
    const int remainder = size % 8;
    if (remainder > 0) unsigned8(remainder, sar(value, quotient * 8));
    for (int i = quotient - 1; i >= 0; i--) unsigned8(8, sar(value, 8 * i));
}

void BitOutput::writeInt(bool isUnsigned, int size, int value) {
    requireValidIntSize(isUnsigned, size);
    if (!isUnsigned) {
        const int usize = size - 1;
        writeInt(true, 1, sar(value, usize));
        if (usize > 0) writeInt(true, usize, value);
        return;
    }
    const int quotient = size / 16;
    const int remainder = size % 16;
    if (remainder > 0) unsigned16(remainder, sar(value, quotient * 16));
    for (int i = quotient - 1; i >= 0; i--) unsigned16(16, sar(value, 16 * i));
}

void BitOutput::align(int bytes) {
    if (index_ > 0) unsigned8(8 - index_, 0);
    const int64_t remainder = count_ % bytes;
    int64_t octets = (remainder > 0 ? bytes : 0) - remainder;
    for (; octets > 0; octets--) unsigned8(8, 0);
}

int BitInput::unsigned8(int size) {
    if (size < 1 || size > 8) throw BitSizeError{};
    if (index_ == 8) {
        int o = octet();
        if (size == 8) return o;
        for (int i = 7; i >= 0; i--) {
            flags_[i] = (o & 0x01) == 0x01;
            o >>= 1;
        }
        index_ = 0;
    }
    const int available = 8 - index_;
    const int required = size - available;
    if (required > 0) return (unsigned8(available) << required) | unsigned8(required);
    int value = 0;
    for (int i = 0; i < size; i++) value = (value << 1) | (flags_[index_++] ? 1 : 0);
    return value;
}

int BitInput::unsigned16(int size) {
    if (size < 1 || size > 16) throw BitSizeError{};
    int value = 0;
    const int quotient = size / 8;
    const int remainder = size % 8;
    for (int i = 0; i < quotient; i++) value = (value << 8) | unsigned8(8);
    if (remainder > 0) value = (value << remainder) | unsigned8(remainder);
    return value;
}

int BitInput::readInt(bool isUnsigned, int size) {
    requireValidIntSize(isUnsigned, size);
    if (!isUnsigned) {
        const int usize = size - 1;
        int value = 0 - readInt(true, 1);
        if (usize > 0) {
            value = static_cast<int>(static_cast<uint32_t>(value) << usize);
            value |= readInt(true, usize);
        }
        return value;
    }
    int value = 0;
    const int quotient = size / 16;
    const int remainder = size % 16;
    for (int i = 0; i < quotient; i++) value = static_cast<int>(static_cast<uint32_t>(value) << 16) | unsigned16(16);
    if (remainder > 0) value = static_cast<int>(static_cast<uint32_t>(value) << remainder) | unsigned16(remainder);
    return value;
}

} // namespace glic
