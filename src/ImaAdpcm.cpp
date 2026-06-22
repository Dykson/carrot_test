#include "ImaAdpcm.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace carrot::audio {
namespace {

constexpr std::array<int, 89> kStepTable = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

constexpr std::array<int, 16> kIndexTable = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

[[nodiscard]] int16_t clampInt16(int value) {
    return static_cast<int16_t>(std::clamp(value, -32768, 32767));
}

[[nodiscard]] uint8_t encodeNibble(int16_t sample, int& predictor, int& stepIndex) {
    const int step = kStepTable[static_cast<size_t>(stepIndex)];
    int diff = static_cast<int>(sample) - predictor;
    uint8_t code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }

    int delta = step >> 3;
    if (diff >= step) {
        code |= 4;
        diff -= step;
        delta += step;
    }
    if (diff >= (step >> 1)) {
        code |= 2;
        diff -= step >> 1;
        delta += step >> 1;
    }
    if (diff >= (step >> 2)) {
        code |= 1;
        delta += step >> 2;
    }

    predictor += (code & 8) != 0 ? -delta : delta;
    predictor = clampInt16(predictor);
    stepIndex = std::clamp(stepIndex + kIndexTable[code], 0, 88);
    return code;
}

[[nodiscard]] int16_t decodeNibble(uint8_t code, int& predictor, int& stepIndex) {
    const int step = kStepTable[static_cast<size_t>(stepIndex)];
    int delta = step >> 3;
    if ((code & 4) != 0) {
        delta += step;
    }
    if ((code & 2) != 0) {
        delta += step >> 1;
    }
    if ((code & 1) != 0) {
        delta += step >> 2;
    }

    predictor += (code & 8) != 0 ? -delta : delta;
    predictor = clampInt16(predictor);
    stepIndex = std::clamp(stepIndex + kIndexTable[code & 0x0F], 0, 88);
    return static_cast<int16_t>(predictor);
}

} // namespace

ImaAdpcmEncoder::ImaAdpcmEncoder(uint32_t blockSamples) : blockSamples_(blockSamples) {
    if (blockSamples_ == 0) {
        throw std::invalid_argument("IMA ADPCM block size must be greater than zero");
    }
}

ImaAdpcmStream ImaAdpcmEncoder::encode(const std::vector<int16_t>& interleavedPcm,
                                       uint16_t channels,
                                       uint32_t sampleRate) const {
    if (channels == 0) {
        throw std::invalid_argument("channel count must be greater than zero");
    }
    if (interleavedPcm.size() % channels != 0) {
        throw std::invalid_argument("PCM sample count is not divisible by channel count");
    }

    ImaAdpcmStream stream;
    stream.channels = channels;
    stream.sampleRate = sampleRate;
    stream.channelBlocks.resize(channels);

    const size_t frames = interleavedPcm.size() / channels;
    for (uint16_t channel = 0; channel < channels; ++channel) {
        for (size_t blockStart = 0; blockStart < frames; blockStart += blockSamples_) {
            const size_t count = std::min<size_t>(blockSamples_, frames - blockStart);
            ImaAdpcmBlock block;
            block.initialPredictor = interleavedPcm[blockStart * channels + channel];
            block.initialStepIndex = 0;
            block.sampleCount = static_cast<uint32_t>(count);
            block.nibbles.reserve(count > 0 ? count - 1 : 0);

            int predictor = block.initialPredictor;
            int stepIndex = block.initialStepIndex;
            for (size_t i = 1; i < count; ++i) {
                block.nibbles.push_back(encodeNibble(interleavedPcm[(blockStart + i) * channels + channel],
                                                     predictor,
                                                     stepIndex));
            }
            stream.channelBlocks[channel].push_back(std::move(block));
        }
    }

    return stream;
}

std::vector<int16_t> ImaAdpcmDecoder::decode(const ImaAdpcmStream& stream) const {
    if (stream.channels == 0 || stream.channelBlocks.size() != stream.channels) {
        throw std::invalid_argument("invalid IMA ADPCM stream channel layout");
    }

    size_t totalFrames = 0;
    for (const auto& block : stream.channelBlocks.front()) {
        totalFrames += block.sampleCount;
    }

    std::vector<std::vector<int16_t>> planar(stream.channels);
    for (uint16_t channel = 0; channel < stream.channels; ++channel) {
        for (const auto& block : stream.channelBlocks[channel]) {
            if (block.sampleCount == 0 || block.nibbles.size() + 1 != block.sampleCount) {
                throw std::invalid_argument("invalid IMA ADPCM block size");
            }
            int predictor = block.initialPredictor;
            int stepIndex = std::clamp<int>(block.initialStepIndex, 0, 88);
            planar[channel].push_back(block.initialPredictor);
            for (const uint8_t nibble : block.nibbles) {
                planar[channel].push_back(decodeNibble(nibble & 0x0F, predictor, stepIndex));
            }
        }
        if (planar[channel].size() != totalFrames) {
            throw std::invalid_argument("all channels must contain the same number of decoded frames");
        }
    }

    std::vector<int16_t> interleaved;
    interleaved.reserve(totalFrames * stream.channels);
    for (size_t frame = 0; frame < totalFrames; ++frame) {
        for (uint16_t channel = 0; channel < stream.channels; ++channel) {
            interleaved.push_back(planar[channel][frame]);
        }
    }
    return interleaved;
}

} // namespace carrot::audio
