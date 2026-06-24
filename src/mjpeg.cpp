#include "mjpeg.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace carrot {
namespace {

constexpr double kInvSqrt2 = 0.70710678118654752440;
constexpr int kBlockSize = 8;


int quantization_scale(int quality) {
    return std::max(1, 101 - std::clamp(quality, 1, 100));
}

void write_i16(std::vector<uint8_t>& output, int value) {
    output.push_back(static_cast<uint8_t>(value & 0xFF));
    output.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

int16_t read_i16(const std::vector<uint8_t>& input, size_t& offset) {
    if (offset + 2 > input.size()) {
        throw std::runtime_error("truncated simplified MJPEG stream");
    }

    const uint16_t value = static_cast<uint16_t>(input[offset] | (input[offset + 1] << 8));
    offset += 2;
    return static_cast<int16_t>(value);
}

uint8_t pixel_at_clamped(const ImageRgb& image, int x, int y, int channel) {
    const int clamped_x = std::clamp(x, 0, image.width - 1);
    const int clamped_y = std::clamp(y, 0, image.height - 1);
    return image.pixels[(clamped_y * image.width + clamped_x) * 3 + channel];
}

void forward_dct_1d_llm(const double input[kBlockSize], double output[kBlockSize]) {
    // Loeffler-Ligtenberg-Moschytz 8-point DCT, adapted from:
    // https://github.com/norishigefukushima/dct_simd/blob/master/dct/dct8x8_simd.cpp
    constexpr double r1 = 1.3870398453221475;  // sqrt(2) * cos(pi / 16)
    constexpr double r2 = 1.3065629648763766;  // sqrt(2) * cos(2pi / 16)
    constexpr double r3 = 1.1758756024193588;  // sqrt(2) * cos(3pi / 16)
    constexpr double r5 = 0.7856949583871022;  // sqrt(2) * cos(5pi / 16)
    constexpr double r6 = 0.5411961001461971;  // sqrt(2) * cos(6pi / 16)
    constexpr double r7 = 0.2758993792829431;  // sqrt(2) * cos(7pi / 16)

    const double t0 = input[0] + input[7];
    const double t7 = input[0] - input[7];
    const double t1 = input[1] + input[6];
    const double t6 = input[1] - input[6];
    const double t2 = input[2] + input[5];
    const double t5 = input[2] - input[5];
    const double t3 = input[3] + input[4];
    const double t4 = input[3] - input[4];

    const double c0 = t0 + t3;
    const double c3 = t0 - t3;
    const double c1 = t1 + t2;
    const double c2 = t1 - t2;

    output[0] = c0 + c1;
    output[4] = c0 - c1;
    output[2] = c2 * r6 + c3 * r2;
    output[6] = c3 * r6 - c2 * r2;

    const double c3_odd = t4 * r3 + t7 * r5;
    const double c0_odd = t7 * r3 - t4 * r5;
    const double c2_odd = t5 * r1 + t6 * r7;
    const double c1_odd = t6 * r1 - t5 * r7;

    output[5] = c3_odd - c1_odd;
    output[3] = c0_odd - c2_odd;

    const double c0_rotated = (c0_odd + c2_odd) * kInvSqrt2;
    const double c3_rotated = (c3_odd + c1_odd) * kInvSqrt2;
    output[1] = c0_rotated + c3_rotated;
    output[7] = c0_rotated - c3_rotated;
}

void inverse_dct_1d_llm(const double input[kBlockSize], double output[kBlockSize]) {
    // Loeffler-Ligtenberg-Moschytz 8-point IDCT, adapted from:
    // https://github.com/norishigefukushima/dct_simd/blob/master/dct/dct8x8_simd.cpp
    constexpr double r1 = 1.3870398453221475;  // sqrt(2) * cos(pi / 16)
    constexpr double r2 = 1.3065629648763766;  // sqrt(2) * cos(2pi / 16)
    constexpr double r3 = 1.1758756024193588;  // sqrt(2) * cos(3pi / 16)
    constexpr double r5 = 0.7856949583871022;  // sqrt(2) * cos(5pi / 16)
    constexpr double r6 = 0.5411961001461971;  // sqrt(2) * cos(6pi / 16)
    constexpr double r7 = 0.2758993792829431;  // sqrt(2) * cos(7pi / 16)

    double z0 = input[1] + input[7];
    double z1 = input[3] + input[5];
    double z2 = input[3] + input[7];
    double z3 = input[1] + input[5];
    const double z4 = (z0 + z1) * r3;

    z0 *= -r3 + r7;
    z1 *= -r3 - r1;
    z2 = z2 * (-r3 - r5) + z4;
    z3 = z3 * (-r3 + r5) + z4;

    const double b3 = input[7] * (-r1 + r3 + r5 - r7) + z0 + z2;
    const double b2 = input[5] * (r1 + r3 - r5 + r7) + z1 + z3;
    const double b1 = input[3] * (r1 + r3 + r5 - r7) + z1 + z2;
    const double b0 = input[1] * (r1 + r3 - r5 - r7) + z0 + z3;

    const double z4_even = (input[2] + input[6]) * r6;
    z0 = input[0] + input[4];
    z1 = input[0] - input[4];
    z2 = z4_even - input[6] * (r2 + r6);
    z3 = z4_even + input[2] * (r2 - r6);

    const double a0 = z0 + z3;
    const double a3 = z0 - z3;
    const double a1 = z1 + z2;
    const double a2 = z1 - z2;

    output[0] = a0 + b0;
    output[7] = a0 - b0;
    output[1] = a1 + b1;
    output[6] = a1 - b1;
    output[2] = a2 + b2;
    output[5] = a2 - b2;
    output[3] = a3 + b3;
    output[4] = a3 - b3;
}

void forward_dct_block(const double input[kBlockSize][kBlockSize], double output[kBlockSize][kBlockSize]) {
    double temp[kBlockSize][kBlockSize]{};
    double column[kBlockSize]{};
    double transformed[kBlockSize]{};

    for (int y = 0; y < kBlockSize; ++y) {
        forward_dct_1d_llm(input[y], temp[y]);
    }

    for (int x = 0; x < kBlockSize; ++x) {
        for (int y = 0; y < kBlockSize; ++y) {
            column[y] = temp[y][x];
        }

        forward_dct_1d_llm(column, transformed);

        for (int y = 0; y < kBlockSize; ++y) {
            output[y][x] = transformed[y] * 0.125;
        }
    }
}

void inverse_dct_block(const double input[kBlockSize][kBlockSize], double output[kBlockSize][kBlockSize]) {
    double temp[kBlockSize][kBlockSize]{};
    double column[kBlockSize]{};
    double transformed[kBlockSize]{};

    for (int y = 0; y < kBlockSize; ++y) {
        inverse_dct_1d_llm(input[y], temp[y]);
    }

    for (int x = 0; x < kBlockSize; ++x) {
        for (int y = 0; y < kBlockSize; ++y) {
            column[y] = temp[y][x];
        }

        inverse_dct_1d_llm(column, transformed);

        for (int y = 0; y < kBlockSize; ++y) {
            output[y][x] = transformed[y] * 0.125;
        }
    }
}

}  // namespace

MjpegEncoder::MjpegEncoder(int quality)
    : quality_(quality) {}

MjpegFrame MjpegEncoder::encode(const ImageRgb& image) const {
    if (image.width <= 0 || image.height <= 0 || image.pixels.size() != static_cast<size_t>(image.width * image.height * 3)) {
        throw std::runtime_error("invalid RGB image");
    }

    const int q = quantization_scale(quality_);
    MjpegFrame frame{image.width, image.height, {}};
    frame.bitstream.insert(frame.bitstream.end(), {'S', 'J', 'P', 'G', static_cast<uint8_t>(q)});

    double samples[kBlockSize][kBlockSize]{};
    double coefficients[kBlockSize][kBlockSize]{};

    for (int block_y = 0; block_y < image.height; block_y += kBlockSize) {
        for (int block_x = 0; block_x < image.width; block_x += kBlockSize) {
            for (int channel = 0; channel < 3; ++channel) {
                for (int y = 0; y < kBlockSize; ++y) {
                    for (int x = 0; x < kBlockSize; ++x) {
                        samples[y][x] = pixel_at_clamped(image, block_x + x, block_y + y, channel) - 128.0;
                    }
                }

                forward_dct_block(samples, coefficients);

                for (int v = 0; v < kBlockSize; ++v) {
                    for (int u = 0; u < kBlockSize; ++u) {
                        write_i16(frame.bitstream, static_cast<int>(std::lround(coefficients[v][u] / q)));
                    }
                }
            }
        }
    }

    return frame;
}

ImageRgb MjpegDecoder::decode(const MjpegFrame& frame) const {
    if (frame.bitstream.size() < 5 || std::string(reinterpret_cast<const char*>(frame.bitstream.data()), 4) != "SJPG") {
        throw std::runtime_error("bad simplified MJPEG stream");
    }

    const int q = frame.bitstream[4];
    size_t offset = 5;
    ImageRgb image{frame.width, frame.height, std::vector<uint8_t>(static_cast<size_t>(frame.width * frame.height * 3))};

    double coefficients[kBlockSize][kBlockSize]{};
    double samples[kBlockSize][kBlockSize]{};

    for (int block_y = 0; block_y < frame.height; block_y += kBlockSize) {
        for (int block_x = 0; block_x < frame.width; block_x += kBlockSize) {
            for (int channel = 0; channel < 3; ++channel) {
                for (int v = 0; v < kBlockSize; ++v) {
                    for (int u = 0; u < kBlockSize; ++u) {
                        coefficients[v][u] = read_i16(frame.bitstream, offset) * q;
                    }
                }

                inverse_dct_block(coefficients, samples);

                for (int y = 0; y < kBlockSize; ++y) {
                    for (int x = 0; x < kBlockSize; ++x) {
                        const int image_x = block_x + x;
                        const int image_y = block_y + y;
                        if (image_x >= frame.width || image_y >= frame.height) {
                            continue;
                        }

                        const long rounded = std::lround(samples[y][x] + 128.0);
                        image.pixels[(image_y * frame.width + image_x) * 3 + channel] =
                            static_cast<uint8_t>(std::clamp(rounded, 0L, 255L));
                    }
                }
            }
        }
    }

    return image;
}

ImageRgb load_png_rgb(const std::string& path) {
    int width = 0;
    int height = 0;
    int component_count = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &component_count, 3);

    if (pixels == nullptr) {
        throw std::runtime_error(stbi_failure_reason());
    }

    ImageRgb image{width, height, std::vector<uint8_t>(pixels, pixels + static_cast<size_t>(width * height * 3))};
    stbi_image_free(pixels);
    return image;
}

void save_png_rgb(const std::string& path, const ImageRgb& image) {
    const int stride_bytes = image.width * 3;
    if (!stbi_write_png(path.c_str(), image.width, image.height, 3, image.pixels.data(), stride_bytes)) {
        throw std::runtime_error("failed to write image: " + path);
    }
}

std::vector<MjpegFrame> encode_folder(const std::string& folder, int quality) {
    std::vector<MjpegFrame> frames;
    MjpegEncoder encoder(quality);

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.path().extension() == ".png") {
            frames.push_back(encoder.encode(load_png_rgb(entry.path().string())));
        }
    }

    return frames;
}

void decode_folder(const std::vector<MjpegFrame>& frames, const std::string& folder) {
    std::filesystem::create_directories(folder);
    MjpegDecoder decoder;

    for (size_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
        const std::string path = folder + "/frame_" + std::to_string(frame_index) + ".png";
        save_png_rgb(path, decoder.decode(frames[frame_index]));
    }
}

}  // namespace carrot
