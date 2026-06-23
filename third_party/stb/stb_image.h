#pragma once

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
unsigned char* stbi_load(char const* filename, int* x, int* y, int* channels_in_file, int desired_channels);
void stbi_image_free(void* retval_from_stbi_load);
char const* stbi_failure_reason(void);
}

#ifdef STB_IMAGE_IMPLEMENTATION
namespace stb_compat_image {
inline const char*& failure() { static const char* message = "no failure"; return message; }
inline void setFailure(const char* message) { failure() = message; }
inline std::uint32_t read32(const std::vector<unsigned char>& data, std::size_t offset) {
    return (static_cast<std::uint32_t>(data[offset]) << 24U) |
           (static_cast<std::uint32_t>(data[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(data[offset + 3U]);
}
inline int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
}
inline bool unfilter(std::vector<unsigned char>& out, const std::vector<unsigned char>& in,
                     int width, int height, int channels) {
    const int rowBytes = width * channels;
    const int stride = rowBytes + 1;
    out.assign(static_cast<std::size_t>(rowBytes) * height, 0);
    for (int y = 0; y < height; ++y) {
        const unsigned char filter = in[static_cast<std::size_t>(y) * stride];
        const auto src = in.data() + static_cast<std::size_t>(y) * stride + 1U;
        auto dst = out.data() + static_cast<std::size_t>(y) * rowBytes;
        const auto prev = y == 0 ? nullptr : out.data() + static_cast<std::size_t>(y - 1) * rowBytes;
        for (int x = 0; x < rowBytes; ++x) {
            const int left = x >= channels ? dst[x - channels] : 0;
            const int up = prev ? prev[x] : 0;
            const int upLeft = prev && x >= channels ? prev[x - channels] : 0;
            int value = src[x];
            switch (filter) {
                case 0: break;
                case 1: value += left; break;
                case 2: value += up; break;
                case 3: value += (left + up) / 2; break;
                case 4: value += paeth(left, up, upLeft); break;
                default: setFailure("unsupported PNG filter"); return false;
            }
            dst[x] = static_cast<unsigned char>(value & 255);
        }
    }
    return true;
}
} // namespace stb_compat_image

extern "C" unsigned char* stbi_load(char const* filename, int* x, int* y, int* channels_in_file, int desired_channels) {
    FILE* file = std::fopen(filename, "rb");
    if (!file) { stb_compat_image::setFailure("cannot open file"); return nullptr; }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::vector<unsigned char> data(static_cast<std::size_t>(size));
    if (std::fread(data.data(), 1, data.size(), file) != data.size()) { std::fclose(file); stb_compat_image::setFailure("cannot read file"); return nullptr; }
    std::fclose(file);
    const unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (data.size() < 33 || std::memcmp(data.data(), signature, 8) != 0) { stb_compat_image::setFailure("not a PNG file"); return nullptr; }

    int width = 0, height = 0, sourceChannels = 0;
    std::vector<unsigned char> compressed;
    for (std::size_t pos = 8; pos + 12 <= data.size();) {
        const auto length = stb_compat_image::read32(data, pos); pos += 4;
        const std::string type(reinterpret_cast<const char*>(data.data() + pos), 4); pos += 4;
        if (pos + length + 4 > data.size()) { stb_compat_image::setFailure("truncated PNG chunk"); return nullptr; }
        if (type == "IHDR") {
            width = static_cast<int>(stb_compat_image::read32(data, pos));
            height = static_cast<int>(stb_compat_image::read32(data, pos + 4));
            const auto bitDepth = data[pos + 8];
            const auto colorType = data[pos + 9];
            if (bitDepth != 8 || (colorType != 0 && colorType != 2 && colorType != 6)) { stb_compat_image::setFailure("unsupported PNG format"); return nullptr; }
            sourceChannels = colorType == 0 ? 1 : (colorType == 2 ? 3 : 4);
        } else if (type == "IDAT") {
            compressed.insert(compressed.end(), data.begin() + static_cast<long>(pos), data.begin() + static_cast<long>(pos + length));
        } else if (type == "IEND") {
            break;
        }
        pos += length + 4;
    }
    if (width <= 0 || height <= 0 || compressed.empty()) { stb_compat_image::setFailure("invalid PNG"); return nullptr; }

    auto rawSize = static_cast<uLongf>((width * sourceChannels + 1) * height);
    std::vector<unsigned char> raw(rawSize);
    if (uncompress(raw.data(), &rawSize, compressed.data(), static_cast<uLong>(compressed.size())) != Z_OK) { stb_compat_image::setFailure("PNG zlib decode failed"); return nullptr; }
    std::vector<unsigned char> decoded;
    if (!stb_compat_image::unfilter(decoded, raw, width, height, sourceChannels)) return nullptr;

    const int outChannels = desired_channels == 0 ? sourceChannels : desired_channels;
    auto* out = static_cast<unsigned char*>(std::malloc(static_cast<std::size_t>(width) * height * outChannels));
    if (!out) { stb_compat_image::setFailure("out of memory"); return nullptr; }
    for (int i = 0; i < width * height; ++i) {
        const auto* src = decoded.data() + static_cast<std::size_t>(i) * sourceChannels;
        auto* dst = out + static_cast<std::size_t>(i) * outChannels;
        const unsigned char r = sourceChannels == 1 ? src[0] : src[0];
        const unsigned char g = sourceChannels == 1 ? src[0] : src[1];
        const unsigned char b = sourceChannels == 1 ? src[0] : src[2];
        const unsigned char a = sourceChannels == 4 ? src[3] : 255;
        if (outChannels >= 1) dst[0] = r;
        if (outChannels >= 2) dst[1] = g;
        if (outChannels >= 3) dst[2] = b;
        if (outChannels >= 4) dst[3] = a;
    }
    if (x) *x = width;
    if (y) *y = height;
    if (channels_in_file) *channels_in_file = sourceChannels;
    return out;
}

extern "C" void stbi_image_free(void* retval_from_stbi_load) { std::free(retval_from_stbi_load); }
extern "C" char const* stbi_failure_reason(void) { return stb_compat_image::failure(); }
#endif
