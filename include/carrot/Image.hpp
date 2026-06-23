#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace carrot {

struct RgbPixel {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
};

class Image {
public:
    Image() = default;
    Image(std::uint32_t width, std::uint32_t height);

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] bool empty() const noexcept { return pixels_.empty(); }

    [[nodiscard]] const std::vector<RgbPixel>& pixels() const noexcept { return pixels_; }
    [[nodiscard]] std::vector<RgbPixel>& pixels() noexcept { return pixels_; }

    [[nodiscard]] const RgbPixel& at(std::uint32_t x, std::uint32_t y) const;
    [[nodiscard]] RgbPixel& at(std::uint32_t x, std::uint32_t y);

private:
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::vector<RgbPixel> pixels_;
};

Image readPng(const std::filesystem::path& path);
void writePng(const std::filesystem::path& path, const Image& image);

} // namespace carrot
