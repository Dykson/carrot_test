#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace carrot::audio {

struct WavPcm16 {
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    std::vector<int16_t> samples;
};

[[nodiscard]] WavPcm16 readPcm16Wav(const std::string& path);
void writePcm16Wav(const std::string& path, const WavPcm16& wav);

} // namespace carrot::audio
