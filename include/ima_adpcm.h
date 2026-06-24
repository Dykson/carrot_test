#pragma once
#include <cstdint>
#include <vector>

namespace carrot {
struct ImaAdpcmBlock { int16_t predictor{}; uint8_t step_index{}; std::vector<uint8_t> nibbles; };
class ImaAdpcmEncoder {
public: std::vector<ImaAdpcmBlock> encode(const std::vector<int16_t>& interleaved_pcm, uint16_t channels) const;
};
class ImaAdpcmDecoder {
public: std::vector<int16_t> decode(const std::vector<ImaAdpcmBlock>& blocks, uint16_t channels) const;
};
}
