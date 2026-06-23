#include "carrot/Image.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace carrot {
namespace {

struct StbImageDeleter {
    void operator()(unsigned char* pixels) const noexcept { stbi_image_free(pixels); }
};

std::string describe(const std::filesystem::path& path) { return path.string(); }

} // namespace

Image readPng(const std::filesystem::path& path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::unique_ptr<unsigned char, StbImageDeleter> data(
        stbi_load(path.string().c_str(), &width, &height, &channels, 3));
    if (!data || width <= 0 || height <= 0) {
        throw std::runtime_error("cannot decode PNG with stb_image: " + describe(path) +
                                 " (" + stbi_failure_reason() + ")");
    }

    Image image(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    for (std::uint32_t y = 0; y < image.height(); ++y) {
        for (std::uint32_t x = 0; x < image.width(); ++x) {
            const auto offset = (static_cast<std::size_t>(y) * image.width() + x) * 3U;
            image.at(x, y) = RgbPixel{data.get()[offset], data.get()[offset + 1U], data.get()[offset + 2U]};
        }
    }
    return image;
}

void writePng(const std::filesystem::path& path, const Image& image) {
    std::vector<unsigned char> data(static_cast<std::size_t>(image.width()) * image.height() * 3U);
    for (std::uint32_t y = 0; y < image.height(); ++y) {
        for (std::uint32_t x = 0; x < image.width(); ++x) {
            const auto pixel = image.at(x, y);
            const auto offset = (static_cast<std::size_t>(y) * image.width() + x) * 3U;
            data[offset] = pixel.r;
            data[offset + 1U] = pixel.g;
            data[offset + 2U] = pixel.b;
        }
    }

    const int stride = static_cast<int>(image.width() * 3U);
    const int ok = stbi_write_png(path.string().c_str(), static_cast<int>(image.width()),
                                  static_cast<int>(image.height()), 3, data.data(), stride);
    if (ok == 0) {
        throw std::runtime_error("cannot encode PNG with stb_image_write: " + describe(path));
    }
}

} // namespace carrot
