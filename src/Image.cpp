#include "carrot/Image.hpp"

#include <stdexcept>

namespace carrot {

Image::Image(std::uint32_t width, std::uint32_t height)
    : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) * height) {}

const RgbPixel& Image::at(std::uint32_t x, std::uint32_t y) const {
    if (x >= width_ || y >= height_) {
        throw std::out_of_range("image coordinates are outside image bounds");
    }
    return pixels_[static_cast<std::size_t>(y) * width_ + x];
}

RgbPixel& Image::at(std::uint32_t x, std::uint32_t y) {
    return const_cast<RgbPixel&>(static_cast<const Image&>(*this).at(x, y));
}

} // namespace carrot
