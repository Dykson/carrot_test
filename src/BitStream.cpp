#include "carrot/BitStream.hpp"

#include <stdexcept>

namespace carrot {

void BitWriter::writeBit(bool bit) {
    current_ = static_cast<std::uint8_t>((current_ << 1U) | (bit ? 1U : 0U));
    ++used_;
    if (used_ == 8U) {
        bytes_.push_back(current_);
        current_ = 0;
        used_ = 0;
    }
}

void BitWriter::writeBits(std::uint32_t value, std::uint8_t count) {
    for (std::uint8_t i = 0; i < count; ++i) {
        const auto shift = static_cast<std::uint8_t>(count - i - 1U);
        writeBit(((value >> shift) & 1U) != 0U);
    }
}

std::vector<std::uint8_t> BitWriter::finish() {
    if (used_ != 0U) {
        current_ <<= static_cast<std::uint8_t>(8U - used_);
        bytes_.push_back(current_);
        current_ = 0;
        used_ = 0;
    }
    return bytes_;
}

BitReader::BitReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

bool BitReader::readBit() {
    if (index_ >= bytes_.size()) {
        throw std::runtime_error("unexpected end of bit stream");
    }
    const bool bit = ((bytes_[index_] >> (7U - used_)) & 1U) != 0U;
    ++used_;
    if (used_ == 8U) {
        used_ = 0;
        ++index_;
    }
    return bit;
}

std::uint32_t BitReader::readBits(std::uint8_t count) {
    std::uint32_t value = 0;
    for (std::uint8_t i = 0; i < count; ++i) {
        value = (value << 1U) | (readBit() ? 1U : 0U);
    }
    return value;
}

} // namespace carrot
