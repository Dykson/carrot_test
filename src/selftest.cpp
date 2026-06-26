#include "ima_adpcm.h"
#include "mjpeg.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_adpcm_case(const std::vector<int16_t>& samples, uint16_t channels, const std::string& name) {
    const carrot::ImaAdpcmEncoder encoder;
    const carrot::ImaAdpcmDecoder decoder;
    const std::vector<carrot::ImaAdpcmBlock> encoded = encoder.encode(samples, channels);
    const std::vector<int16_t> decoded = decoder.decode(encoded, channels);
    expect(decoded.size() == samples.size(), "ADPCM size mismatch: " + name);
}

carrot::ImageRgb make_image(int width, int height, int mode) {
    carrot::ImageRgb image{width, height, std::vector<uint8_t>(static_cast<size_t>(width) * static_cast<size_t>(height) * 3)};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t off = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
            if (mode == 0) {
                image.pixels[off] = 80; image.pixels[off + 1] = 120; image.pixels[off + 2] = 160;
            } else if (mode == 1) {
                image.pixels[off] = static_cast<uint8_t>((x * 255) / std::max(1, width - 1));
                image.pixels[off + 1] = static_cast<uint8_t>((y * 255) / std::max(1, height - 1));
                image.pixels[off + 2] = 64;
            } else {
                const uint8_t v = ((x / 4 + y / 4) % 2) ? 255 : 0;
                image.pixels[off] = v; image.pixels[off + 1] = static_cast<uint8_t>(255 - v); image.pixels[off + 2] = 128;
            }
        }
    }
    return image;
}

void test_mjpeg_case(const carrot::ImageRgb& image, const std::string& name) {
    const carrot::MjpegEncoder encoder(85);
    const carrot::MjpegDecoder decoder;
    const carrot::MjpegFrame frame = encoder.encode(image);
    const carrot::ImageRgb decoded = decoder.decode(frame);
    expect(decoded.width == image.width && decoded.height == image.height, "MJPEG dimensions mismatch: " + name);
    expect(decoded.pixels.size() == image.pixels.size(), "MJPEG pixel size mismatch: " + name);
}

}  // namespace

int main() {
    try {
        test_adpcm_case({}, 1, "empty mono");
        test_adpcm_case({0}, 1, "one sample");
        test_adpcm_case({0, 1000}, 1, "two samples");
        test_adpcm_case({0, 1000, -1000}, 1, "three samples");
        test_adpcm_case(std::vector<int16_t>(101, 0), 1, "mono silence odd");
        std::vector<int16_t> ramp;
        for (int i = 0; i < 257; ++i) ramp.push_back(static_cast<int16_t>(i * 50 - 6000));
        test_adpcm_case(ramp, 1, "mono ramp");
        std::vector<int16_t> stereo;
        for (int i = 0; i < 123; ++i) { stereo.push_back(static_cast<int16_t>(i * 100)); stereo.push_back(static_cast<int16_t>(12000 - i * 70)); }
        test_adpcm_case(stereo, 2, "stereo different channels");

        test_mjpeg_case(make_image(8, 8, 0), "8x8 solid");
        test_mjpeg_case(make_image(9, 9, 1), "9x9 gradient");
        test_mjpeg_case(make_image(17, 17, 2), "17x17 checkerboard");
        std::cout << "selftest passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
