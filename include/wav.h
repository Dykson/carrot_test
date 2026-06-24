#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace carrot {
struct WavPcm { uint16_t channels{}; uint32_t sample_rate{}; std::vector<int16_t> samples; };
WavPcm read_wav_pcm16(const std::string& path);
void write_wav_pcm16(const std::string& path, const WavPcm& wav);
}
