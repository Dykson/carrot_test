#include "carrot/Image.hpp"

#include <png.h>

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

namespace carrot {
namespace {

struct FileCloser {
    void operator()(FILE* file) const noexcept { std::fclose(file); }
};

std::string describe(const std::filesystem::path& path) { return path.string(); }

} // namespace

Image readPng(const std::filesystem::path& path) {
    std::unique_ptr<FILE, FileCloser> file(std::fopen(path.string().c_str(), "rb"));
    if (!file) {
        throw std::runtime_error("cannot open PNG for reading: " + describe(path));
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        throw std::runtime_error("cannot decode PNG: " + describe(path));
    }

    png_init_io(png, file.get());
    png_read_info(png, info);

    const auto width = png_get_image_width(png, info);
    const auto height = png_get_image_height(png, info);
    const auto colorType = png_get_color_type(png, info);
    const auto bitDepth = png_get_bit_depth(png, info);

    if (bitDepth == 16) png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if (colorType & PNG_COLOR_MASK_ALPHA) png_set_strip_alpha(png);

    png_read_update_info(png, info);
    Image image(width, height);
    std::vector<png_bytep> rows(height);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(png_get_rowbytes(png, info)) * height);
    for (std::uint32_t y = 0; y < height; ++y) rows[y] = data.data() + static_cast<std::size_t>(y) * png_get_rowbytes(png, info);
    png_read_image(png, rows.data());
    png_destroy_read_struct(&png, &info, nullptr);

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto* px = rows[y] + static_cast<std::size_t>(x) * 3U;
            image.at(x, y) = RgbPixel{px[0], px[1], px[2]};
        }
    }
    return image;
}

void writePng(const std::filesystem::path& path, const Image& image) {
    std::unique_ptr<FILE, FileCloser> file(std::fopen(path.string().c_str(), "wb"));
    if (!file) throw std::runtime_error("cannot open PNG for writing: " + describe(path));

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        throw std::runtime_error("cannot encode PNG: " + describe(path));
    }

    png_init_io(png, file.get());
    png_set_IHDR(png, info, image.width(), image.height(), 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    std::vector<std::uint8_t> data(static_cast<std::size_t>(image.width()) * image.height() * 3U);
    for (std::uint32_t y = 0; y < image.height(); ++y) {
        for (std::uint32_t x = 0; x < image.width(); ++x) {
            const auto p = image.at(x, y);
            const auto offset = (static_cast<std::size_t>(y) * image.width() + x) * 3U;
            data[offset] = p.r;
            data[offset + 1U] = p.g;
            data[offset + 2U] = p.b;
        }
    }
    std::vector<png_bytep> rows(image.height());
    for (std::uint32_t y = 0; y < image.height(); ++y) rows[y] = data.data() + static_cast<std::size_t>(y) * image.width() * 3U;
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
}

} // namespace carrot
