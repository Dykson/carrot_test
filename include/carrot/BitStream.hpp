#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace carrot {

class BitWriter {
public:
    void writeBit(bool bit);
    void writeBits(std::uint32_t value, std::uint8_t count);
    [[nodiscard]] std::vector<std::uint8_t> finish();

private:
    std::vector<std::uint8_t> bytes_;
    std::uint8_t current_{};
    std::uint8_t used_{};
};

class BitReader {
public:
    explicit BitReader(const std::vector<std::uint8_t>& bytes);
    [[nodiscard]] bool readBit();
    [[nodiscard]] std::uint32_t readBits(std::uint8_t count);

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t index_{};
    std::uint8_t used_{};
};

} // namespace carrot
