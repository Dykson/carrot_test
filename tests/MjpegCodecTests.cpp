#include "carrot/MjpegCodec.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>

using carrot::Image;
using carrot::MjpegDecoder;
using carrot::MjpegEncoder;
using carrot::MjpegEncoderOptions;
using carrot::RgbPixel;

namespace {

Image makeGradient(std::uint32_t width, std::uint32_t height, int shift) {
    Image image(width, height);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            image.at(x, y) = RgbPixel{
                static_cast<std::uint8_t>((x * 9 + shift) % 256),
                static_cast<std::uint8_t>((y * 13 + shift * 2) % 256),
                static_cast<std::uint8_t>(((x + y) * 5 + shift * 3) % 256)};
        }
    }
    return image;
}

double meanAbsoluteError(const Image& a, const Image& b) {
    assert(a.width() == b.width());
    assert(a.height() == b.height());
    double total = 0.0;
    for (std::uint32_t y = 0; y < a.height(); ++y) {
        for (std::uint32_t x = 0; x < a.width(); ++x) {
            const auto p = a.at(x, y);
            const auto q = b.at(x, y);
            total += std::abs(static_cast<int>(p.r) - q.r);
            total += std::abs(static_cast<int>(p.g) - q.g);
            total += std::abs(static_cast<int>(p.b) - q.b);
        }
    }
    return total / (a.width() * a.height() * 3.0);
}

void testRoundTripMemory() {
    const auto source = makeGradient(19, 17, 7);
    const MjpegEncoder encoder(MjpegEncoderOptions{90});
    const MjpegDecoder decoder;
    const auto encoded = encoder.encodeFrame(source);
    const auto decoded = decoder.decodeFrame(encoded);
    assert(decoded.width() == source.width());
    assert(decoded.height() == source.height());
    assert(meanAbsoluteError(source, decoded) < 8.0);
}

void testContainerAndPngFolders() {
    const auto root = std::filesystem::temp_directory_path() / "carrot_mjpeg_tests";
    const auto input = root / "input";
    const auto output = root / "output";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(input);

    carrot::writePng(input / "0002.png", makeGradient(16, 16, 22));
    carrot::writePng(input / "0001.png", makeGradient(16, 16, 11));

    const auto movie = root / "movie.smjpg";
    const MjpegEncoder encoder(MjpegEncoderOptions{85});
    encoder.encodeFolder(input, movie);

    const MjpegDecoder decoder;
    const auto frames = decoder.decodeFile(movie);
    assert(frames.size() == 2);
    assert(frames[0].width() == 16);
    assert(frames[0].height() == 16);
    assert(meanAbsoluteError(makeGradient(16, 16, 11), frames[0]) < 12.0);

    decoder.decodeToFolder(movie, output);
    assert(std::filesystem::exists(output / "frame_0000.png"));
    assert(std::filesystem::exists(output / "frame_0001.png"));
}

} // namespace

int main() {
    testRoundTripMemory();
    testContainerAndPngFolders();
    std::cout << "MJPEG codec tests passed\n";
    return 0;
}
