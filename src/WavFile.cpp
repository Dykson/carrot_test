#include "WavFile.hpp"

#include <fstream>
#include <stdexcept>
#include <string>

namespace carrot::audio {
namespace {

template <typename T>
T readLE(std::istream& in) {
    T value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!in) {
        throw std::runtime_error("unexpected end of WAV file");
    }
    return value;
}

template <typename T>
void writeLE(std::ostream& out, T value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

std::string readTag(std::istream& in) {
    char tag[4]{};
    in.read(tag, sizeof(tag));
    if (!in) {
        throw std::runtime_error("unexpected end of WAV file while reading chunk tag");
    }
    return std::string(tag, sizeof(tag));
}

} // namespace

WavPcm16 readPcm16Wav(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open WAV file: " + path);
    }

    if (readTag(in) != "RIFF") {
        throw std::runtime_error("WAV file must start with RIFF");
    }
    (void)readLE<uint32_t>(in);
    if (readTag(in) != "WAVE") {
        throw std::runtime_error("RIFF file is not WAVE");
    }

    bool haveFmt = false;
    bool haveData = false;
    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    std::vector<int16_t> samples;

    while (in && !(haveFmt && haveData)) {
        const std::string tag = readTag(in);
        const uint32_t size = readLE<uint32_t>(in);
        const std::streampos payloadStart = in.tellg();

        if (tag == "fmt ") {
            audioFormat = readLE<uint16_t>(in);
            channels = readLE<uint16_t>(in);
            sampleRate = readLE<uint32_t>(in);
            (void)readLE<uint32_t>(in);
            (void)readLE<uint16_t>(in);
            bitsPerSample = readLE<uint16_t>(in);
            haveFmt = true;
        } else if (tag == "data") {
            if (!haveFmt) {
                throw std::runtime_error("WAV data chunk appeared before fmt chunk");
            }
            if (audioFormat != 1 || bitsPerSample != 16 || channels == 0) {
                throw std::runtime_error("only PCM 16-bit WAV files are supported");
            }
            if (size % sizeof(int16_t) != 0) {
                throw std::runtime_error("WAV data chunk size is not aligned to 16-bit samples");
            }
            samples.resize(size / sizeof(int16_t));
            in.read(reinterpret_cast<char*>(samples.data()), size);
            if (!in) {
                throw std::runtime_error("failed to read WAV sample data");
            }
            haveData = true;
        }

        in.seekg(payloadStart + static_cast<std::streamoff>(size + (size & 1U)));
    }

    if (!haveFmt || !haveData) {
        throw std::runtime_error("WAV file is missing fmt or data chunk");
    }

    return WavPcm16{channels, sampleRate, std::move(samples)};
}

void writePcm16Wav(const std::string& path, const WavPcm16& wav) {
    if (wav.channels == 0 || wav.samples.size() % wav.channels != 0) {
        throw std::invalid_argument("invalid PCM WAV channel layout");
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to create WAV file: " + path);
    }

    const uint32_t dataBytes = static_cast<uint32_t>(wav.samples.size() * sizeof(int16_t));
    const uint32_t riffSize = 4 + (8 + 16) + (8 + dataBytes);
    const uint16_t blockAlign = static_cast<uint16_t>(wav.channels * sizeof(int16_t));
    const uint32_t byteRate = wav.sampleRate * blockAlign;

    out.write("RIFF", 4);
    writeLE(out, riffSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeLE<uint32_t>(out, 16);
    writeLE<uint16_t>(out, 1);
    writeLE<uint16_t>(out, wav.channels);
    writeLE<uint32_t>(out, wav.sampleRate);
    writeLE<uint32_t>(out, byteRate);
    writeLE<uint16_t>(out, blockAlign);
    writeLE<uint16_t>(out, 16);
    out.write("data", 4);
    writeLE<uint32_t>(out, dataBytes);
    out.write(reinterpret_cast<const char*>(wav.samples.data()), dataBytes);
}

} // namespace carrot::audio
