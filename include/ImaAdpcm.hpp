#pragma once

#include <cstdint>
#include <vector>

namespace carrot::audio {

struct ImaAdpcmBlock {
    int16_t initialPredictor = 0;
    uint8_t initialStepIndex = 0;
    uint32_t sampleCount = 0;
    std::vector<uint8_t> nibbles;
};

struct ImaAdpcmStream {
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    std::vector<std::vector<ImaAdpcmBlock>> channelBlocks;
};

class ImaAdpcmEncoder {
public:
    explicit ImaAdpcmEncoder(uint32_t blockSamples = 1024);

    [[nodiscard]] ImaAdpcmStream encode(const std::vector<int16_t>& interleavedPcm,
                                        uint16_t channels,
                                        uint32_t sampleRate) const;

private:
    uint32_t blockSamples_;
};

class ImaAdpcmDecoder {
public:
    [[nodiscard]] std::vector<int16_t> decode(const ImaAdpcmStream& stream) const;
};

} // namespace carrot::audio
