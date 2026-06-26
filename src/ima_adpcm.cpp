#include "ima_adpcm.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <limits>
#include <string>

namespace carrot {
namespace {

constexpr std::array<int, 89> kStepTable = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086,
    29794, 32767,
};

constexpr std::array<int, 16> kIndexTable = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};


void write_u16(std::ostream& stream, uint16_t value) {
    stream.put(static_cast<char>(value & 0xFF));
    stream.put(static_cast<char>((value >> 8) & 0xFF));
}

void write_u32(std::ostream& stream, uint32_t value) {
    stream.put(static_cast<char>(value & 0xFF));
    stream.put(static_cast<char>((value >> 8) & 0xFF));
    stream.put(static_cast<char>((value >> 16) & 0xFF));
    stream.put(static_cast<char>((value >> 24) & 0xFF));
}

int16_t clamp_to_pcm16(int value) {
    return static_cast<int16_t>(std::clamp(value, -32768, 32767));
}

uint8_t encode_sample(int16_t sample, int& predictor, int& step_index) {
    const int step = kStepTable[step_index];
    int diff = sample - predictor;
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
    if (diff >= step / 2) {
        code |= 2;
        diff -= step / 2;
        delta += step / 2;
    }
    if (diff >= step / 4) {
        code |= 1;
        delta += step / 4;
    }

    predictor += (code & 8) ? -delta : delta;
    predictor = std::clamp(predictor, -32768, 32767);
    step_index = std::clamp(step_index + kIndexTable[code], 0, 88);
    return code;
}

int16_t decode_nibble(uint8_t code, int& predictor, int& step_index) {
    const int step = kStepTable[step_index];
    int delta = step >> 3;

    if (code & 4) {
        delta += step;
    }
    if (code & 2) {
        delta += step >> 1;
    }
    if (code & 1) {
        delta += step >> 2;
    }

    predictor += (code & 8) ? -delta : delta;
    predictor = std::clamp(predictor, -32768, 32767);
    step_index = std::clamp(step_index + kIndexTable[code], 0, 88);
    return clamp_to_pcm16(predictor);
}

}  // namespace

std::vector<ImaAdpcmBlock> ImaAdpcmEncoder::encode(
    const std::vector<int16_t>& interleaved_pcm,
    uint16_t channels) const {
    if (channels == 0 || interleaved_pcm.size() % channels != 0) {
        throw std::runtime_error("invalid PCM layout");
    }

    const size_t frame_count = interleaved_pcm.size() / channels;
    std::vector<ImaAdpcmBlock> encoded_channels(channels);

    for (uint16_t channel = 0; channel < channels; ++channel) {
        ImaAdpcmBlock& block = encoded_channels[channel];
        block.predictor = frame_count == 0 ? 0 : interleaved_pcm[channel];
        block.step_index = 0;
        if (frame_count > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
            throw std::runtime_error("ADPCM block has too many samples");
        }
        block.sample_count = static_cast<uint32_t>(frame_count);

        int predictor = block.predictor;
        int step_index = block.step_index;
        bool has_low_nibble = false;
        uint8_t packed_byte = 0;

        for (size_t frame = 1; frame < frame_count; ++frame) {
            const int16_t sample = interleaved_pcm[frame * channels + channel];
            const uint8_t nibble = encode_sample(sample, predictor, step_index);

            if (!has_low_nibble) {
                packed_byte = nibble;
                has_low_nibble = true;
            } else {
                block.nibbles.push_back(static_cast<uint8_t>(packed_byte | (nibble << 4)));
                has_low_nibble = false;
            }
        }

        if (has_low_nibble) {
            block.nibbles.push_back(packed_byte);
        }
    }

    return encoded_channels;
}

std::vector<int16_t> ImaAdpcmDecoder::decode(
    const std::vector<ImaAdpcmBlock>& blocks,
    uint16_t channels) const {
    if (channels == 0 || blocks.size() != channels) {
        throw std::runtime_error("invalid ADPCM layout");
    }

    uint32_t expected_samples = blocks.empty() ? 0 : blocks.front().sample_count;
    for (const ImaAdpcmBlock& block : blocks) {
        if (block.step_index > 88) {
            throw std::runtime_error("invalid ADPCM step index");
        }
        if (block.sample_count != expected_samples) {
            throw std::runtime_error("ADPCM channel sample counts differ");
        }
        const size_t valid_nibbles = block.sample_count == 0 ? 0 : static_cast<size_t>(block.sample_count - 1);
        if (block.nibbles.size() != (valid_nibbles + 1) / 2) {
            throw std::runtime_error("ADPCM nibble payload size does not match sample count");
        }
    }

    if (static_cast<size_t>(expected_samples) > std::numeric_limits<size_t>::max() / channels) {
        throw std::runtime_error("ADPCM output size overflow");
    }
    std::vector<int16_t> pcm(static_cast<size_t>(expected_samples) * channels);
    if (expected_samples == 0) {
        return pcm;
    }

    for (uint16_t channel = 0; channel < channels; ++channel) {
        const ImaAdpcmBlock& block = blocks[channel];
        int predictor = block.predictor;
        int step_index = block.step_index;

        pcm[channel] = clamp_to_pcm16(predictor);
        size_t frame = 1;
        const size_t valid_nibbles = static_cast<size_t>(block.sample_count - 1);

        for (size_t nibble_index = 0; nibble_index < valid_nibbles; ++nibble_index) {
            const uint8_t packed_byte = block.nibbles[nibble_index / 2];
            const uint8_t nibble = static_cast<uint8_t>(((nibble_index % 2 == 0) ? packed_byte : (packed_byte >> 4)) & 0x0F);
            pcm[frame * channels + channel] = decode_nibble(nibble, predictor, step_index);
            ++frame;
        }
    }

    return pcm;
}


void write_ima_adpcm_stream(const std::string& path,
                            const std::vector<ImaAdpcmBlock>& blocks,
                            uint16_t channels,
                            uint32_t sample_rate) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot create ADPCM file: " + path);
    }

    file.write("CADP", 4);
    write_u16(file, channels);
    write_u32(file, sample_rate);
    if (blocks.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("too many ADPCM blocks to write");
    }
    write_u32(file, static_cast<uint32_t>(blocks.size()));

    for (const ImaAdpcmBlock& block : blocks) {
        write_u16(file, static_cast<uint16_t>(block.predictor));
        file.put(static_cast<char>(block.step_index));
        write_u32(file, block.sample_count);
        write_u32(file, static_cast<uint32_t>(block.nibbles.size()));
        file.write(reinterpret_cast<const char*>(block.nibbles.data()),
                   static_cast<std::streamsize>(block.nibbles.size()));
    }
}

}  // namespace carrot
