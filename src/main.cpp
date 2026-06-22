#include "ImaAdpcm.hpp"
#include "WavFile.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: ima_adpcm_tool <input-pcm16.wav> <decoded-output.wav>\n";
        return 1;
    }

    try {
        const auto wav = carrot::audio::readPcm16Wav(argv[1]);
        const carrot::audio::ImaAdpcmEncoder encoder;
        const carrot::audio::ImaAdpcmDecoder decoder;
        const auto encoded = encoder.encode(wav.samples, wav.channels, wav.sampleRate);
        const auto decoded = decoder.decode(encoded);
        carrot::audio::writePcm16Wav(argv[2], carrot::audio::WavPcm16{wav.channels, wav.sampleRate, decoded});
        std::cout << "Encoded and decoded " << wav.samples.size() / wav.channels
                  << " frames, channels=" << wav.channels
                  << ", sampleRate=" << wav.sampleRate << '\n';
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }

    return 0;
}
