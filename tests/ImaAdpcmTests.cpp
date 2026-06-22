#include "ImaAdpcm.hpp"
#include "WavFile.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<int16_t> makeStereoSine(size_t frames) {
    std::vector<int16_t> pcm;
    pcm.reserve(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / 48000.0;
        pcm.push_back(static_cast<int16_t>(std::sin(2.0 * 3.141592653589793 * 440.0 * t) * 12000.0));
        pcm.push_back(static_cast<int16_t>(std::sin(2.0 * 3.141592653589793 * 880.0 * t) * 8000.0));
    }
    return pcm;
}

void testRoundTripShape() {
    const auto pcm = makeStereoSine(4096);
    const carrot::audio::ImaAdpcmEncoder encoder(257);
    const carrot::audio::ImaAdpcmDecoder decoder;
    const auto encoded = encoder.encode(pcm, 2, 48000);
    const auto decoded = decoder.decode(encoded);

    require(encoded.channels == 2, "encoded stream must preserve channel count");
    require(encoded.sampleRate == 48000, "encoded stream must preserve sample rate");
    require(decoded.size() == pcm.size(), "decoded PCM must preserve sample count");
    require(decoded.front() == pcm.front(), "first predictor sample is stored exactly");
}

void testWavIo() {
    const char* path = "ima_adpcm_test_tmp.wav";
    const carrot::audio::WavPcm16 original{2, 48000, makeStereoSine(32)};
    carrot::audio::writePcm16Wav(path, original);
    const auto loaded = carrot::audio::readPcm16Wav(path);
    std::remove(path);

    require(loaded.channels == original.channels, "WAV channel count mismatch");
    require(loaded.sampleRate == original.sampleRate, "WAV sample rate mismatch");
    require(loaded.samples == original.samples, "WAV samples mismatch");
}

} // namespace

int main() {
    try {
        testRoundTripShape();
        testWavIo();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
