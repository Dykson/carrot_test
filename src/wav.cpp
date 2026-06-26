#include "wav.h"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace carrot {
namespace {

uint32_t read_u32(std::istream& stream) {
    unsigned char bytes[4]{};
    stream.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    return bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
}

uint16_t read_u16(std::istream& stream) {
    unsigned char bytes[2]{};
    stream.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
}

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

}  // namespace

WavPcm read_wav_pcm16(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open WAV file: " + path);
    }

    char id[4]{};
    file.read(id, sizeof(id));
    if (std::string(id, 4) != "RIFF") {
        throw std::runtime_error("not a RIFF file");
    }

    (void)read_u32(file);
    file.read(id, sizeof(id));
    if (std::string(id, 4) != "WAVE") {
        throw std::runtime_error("not a WAVE file");
    }

    WavPcm wav;
    bool has_format_chunk = false;
    bool has_data_chunk = false;

    while (file.read(id, sizeof(id))) {
        const std::string chunk_id(id, 4);
        const uint32_t chunk_size = read_u32(file);

        if (chunk_id == "fmt ") {
            const uint16_t audio_format = read_u16(file);
            wav.channels = read_u16(file);
            wav.sample_rate = read_u32(file);
            (void)read_u32(file);  // byte rate
            (void)read_u16(file);  // block align
            const uint16_t bits_per_sample = read_u16(file);

            if (audio_format != 1 || bits_per_sample != 16) {
                throw std::runtime_error("only little-endian PCM16 WAV is supported");
            }

            if (chunk_size > 16) {
                file.seekg(chunk_size - 16, std::ios::cur);
            }
            has_format_chunk = true;
        } else if (chunk_id == "data") {
            wav.samples.resize(chunk_size / sizeof(int16_t));
            file.read(reinterpret_cast<char*>(wav.samples.data()), chunk_size);
            has_data_chunk = true;
        } else {
            file.seekg(chunk_size, std::ios::cur);
        }
    }

    if (!has_format_chunk || !has_data_chunk) {
        throw std::runtime_error("WAV file misses fmt or data chunk");
    }

    return wav;
}

void write_wav_pcm16(const std::string& path, const WavPcm& wav) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot create WAV file: " + path);
    }

    if (wav.channels == 0 || wav.channels > std::numeric_limits<uint16_t>::max() / sizeof(int16_t)) {
        throw std::runtime_error("invalid WAV channel count");
    }
    if (wav.samples.size() > std::numeric_limits<uint32_t>::max() / sizeof(int16_t)) {
        throw std::runtime_error("WAV data is too large");
    }
    const uint16_t block_align = static_cast<uint16_t>(static_cast<size_t>(wav.channels) * sizeof(int16_t));
    if (wav.sample_rate > std::numeric_limits<uint32_t>::max() / block_align) {
        throw std::runtime_error("WAV byte rate overflow");
    }
    const uint32_t byte_rate = wav.sample_rate * block_align;
    const uint32_t data_size = static_cast<uint32_t>(wav.samples.size() * sizeof(int16_t));
    if (data_size > std::numeric_limits<uint32_t>::max() - 36U) {
        throw std::runtime_error("WAV RIFF size overflow");
    }
    const uint32_t riff_size = 36 + data_size;

    file.write("RIFF", 4);
    write_u32(file, riff_size);
    file.write("WAVE", 4);

    file.write("fmt ", 4);
    write_u32(file, 16);
    write_u16(file, 1);
    write_u16(file, wav.channels);
    write_u32(file, wav.sample_rate);
    write_u32(file, byte_rate);
    write_u16(file, block_align);
    write_u16(file, 16);

    file.write("data", 4);
    write_u32(file, data_size);
    file.write(reinterpret_cast<const char*>(wav.samples.data()), data_size);
}

}  // namespace carrot
