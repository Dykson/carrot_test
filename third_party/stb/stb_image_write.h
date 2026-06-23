#pragma once

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <vector>

extern "C" int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);

#ifdef STB_IMAGE_WRITE_IMPLEMENTATION
namespace stb_compat_write {
inline void put32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value >> 24U));
    out.push_back(static_cast<unsigned char>(value >> 16U));
    out.push_back(static_cast<unsigned char>(value >> 8U));
    out.push_back(static_cast<unsigned char>(value));
}
inline void chunk(std::vector<unsigned char>& out, const char type[4], const std::vector<unsigned char>& payload) {
    put32(out, static_cast<std::uint32_t>(payload.size()));
    const auto start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    const auto crc = crc32(0, out.data() + start, static_cast<uInt>(out.size() - start));
    put32(out, static_cast<std::uint32_t>(crc));
}
} // namespace stb_compat_write

extern "C" int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes) {
    if (w <= 0 || h <= 0 || (comp != 1 && comp != 3 && comp != 4) || data == nullptr) return 0;
    if (stride_in_bytes == 0) stride_in_bytes = w * comp;

    std::vector<unsigned char> raw;
    raw.reserve(static_cast<std::size_t>(h) * (w * comp + 1));
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        const auto* row = bytes + static_cast<std::size_t>(y) * stride_in_bytes;
        raw.insert(raw.end(), row, row + static_cast<std::size_t>(w) * comp);
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<unsigned char> compressed(compressedSize);
    if (compress2(compressed.data(), &compressedSize, raw.data(), static_cast<uLong>(raw.size()), Z_BEST_SPEED) != Z_OK) return 0;
    compressed.resize(compressedSize);

    std::vector<unsigned char> png{137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<unsigned char> ihdr;
    stb_compat_write::put32(ihdr, static_cast<std::uint32_t>(w));
    stb_compat_write::put32(ihdr, static_cast<std::uint32_t>(h));
    ihdr.push_back(8);
    ihdr.push_back(comp == 1 ? 0 : (comp == 3 ? 2 : 6));
    ihdr.insert(ihdr.end(), {0, 0, 0});
    stb_compat_write::chunk(png, "IHDR", ihdr);
    stb_compat_write::chunk(png, "IDAT", compressed);
    stb_compat_write::chunk(png, "IEND", {});

    FILE* file = std::fopen(filename, "wb");
    if (!file) return 0;
    const auto written = std::fwrite(png.data(), 1, png.size(), file);
    std::fclose(file);
    return written == png.size() ? 1 : 0;
}
#endif
