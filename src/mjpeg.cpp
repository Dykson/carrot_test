#include "mjpeg.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace carrot {
namespace {

constexpr double kInvSqrt2 = 0.70710678118654752440;
constexpr int kBlockSize = 8;

constexpr std::array<int, 64> kZigZag = {
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63,
};

struct QuantizationConfig { int luma; int chroma; };

size_t checked_mul(size_t a, size_t b, const char* what) {
    if (a != 0 && b > std::numeric_limits<size_t>::max() / a) {
        throw std::runtime_error(std::string(what) + " size overflow");
    }
    return a * b;
}

size_t rgb_byte_count(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("image dimensions must be positive");
    }
    return checked_mul(checked_mul(static_cast<size_t>(width), static_cast<size_t>(height), "image"), 3, "RGB image");
}


QuantizationConfig quantization_config(int quality) {
    const int base = std::max(1, 101 - std::clamp(quality, 1, 100));
    return {base, std::max(1, base + base / 2)};
}

void write_i16(std::vector<uint8_t>& output, int value) {
    output.push_back(static_cast<uint8_t>(value & 0xFF));
    output.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

uint32_t checked_u32(size_t value, const char* what) {
    if (value > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string(what) + " does not fit into 32-bit stream field");
    }
    return static_cast<uint32_t>(value);
}

void write_u32(std::ostream& stream, uint32_t value) {
    stream.put(static_cast<char>(value & 0xFF));
    stream.put(static_cast<char>((value >> 8) & 0xFF));
    stream.put(static_cast<char>((value >> 16) & 0xFF));
    stream.put(static_cast<char>((value >> 24) & 0xFF));
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

void forward_dct_block(const double input[kBlockSize][kBlockSize], double output[kBlockSize][kBlockSize]);
void inverse_dct_block(const double input[kBlockSize][kBlockSize], double output[kBlockSize][kBlockSize]);

struct YCbCr {
    double y = 0.0;
    double cb = 0.0;
    double cr = 0.0;
};

int half_ceil(int value) {
    return (value + 1) / 2;
}

YCbCr rgb_to_ycbcr(double red, double green, double blue) {
    return {
        0.299 * red + 0.587 * green + 0.114 * blue,
        128.0 - 0.168736 * red - 0.331264 * green + 0.5 * blue,
        128.0 + 0.5 * red - 0.418688 * green - 0.081312 * blue,
    };
}

uint8_t clamp_to_u8(double value) {
    const long rounded = std::lround(value);
    return static_cast<uint8_t>(std::clamp(rounded, 0L, 255L));
}

void ycbcr_to_rgb(double y, double cb, double cr, uint8_t& red, uint8_t& green, uint8_t& blue) {
    const double cb_delta = cb - 128.0;
    const double cr_delta = cr - 128.0;
    red = clamp_to_u8(y + 1.402 * cr_delta);
    green = clamp_to_u8(y - 0.344136 * cb_delta - 0.714136 * cr_delta);
    blue = clamp_to_u8(y + 1.772 * cb_delta);
}

std::vector<double> make_luma_component(const ImageRgb& image) {
    std::vector<double> luma(checked_mul(static_cast<size_t>(image.width), static_cast<size_t>(image.height), "luma"));

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const double red = pixel_at_clamped(image, x, y, 0);
            const double green = pixel_at_clamped(image, x, y, 1);
            const double blue = pixel_at_clamped(image, x, y, 2);
            luma[static_cast<size_t>(y * image.width + x)] = rgb_to_ycbcr(red, green, blue).y;
        }
    }

    return luma;
}

std::vector<double> make_subsampled_chroma_component(const ImageRgb& image, bool cr_component) {
    // The active path is YUV420p / 4:2:0: chroma is half-resolution in both axes.
    // Each Cb/Cr sample is the average of a 2x2 RGB pixel area converted to YCbCr.
    const int chroma_width = half_ceil(image.width);
    const int chroma_height = half_ceil(image.height);

    // YUV422p / 4:2:2 would keep full vertical chroma resolution and subsample only horizontally:
    // const int chroma_width = half_ceil(image.width);
    // const int chroma_height = image.height;
    // The loop would then average a 2x1 area by using offset_y < 1 and dividing the sum by 2.0.
    std::vector<double> chroma(checked_mul(static_cast<size_t>(chroma_width), static_cast<size_t>(chroma_height), "chroma"));

    for (int y = 0; y < chroma_height; ++y) {
        for (int x = 0; x < chroma_width; ++x) {
            double sum = 0.0;
            for (int offset_y = 0; offset_y < 2; ++offset_y) {
                for (int offset_x = 0; offset_x < 2; ++offset_x) {
                    const int source_x = std::min(image.width - 1, x * 2 + offset_x);
                    const int source_y = std::min(image.height - 1, y * 2 + offset_y);
                    const double red = pixel_at_clamped(image, source_x, source_y, 0);
                    const double green = pixel_at_clamped(image, source_x, source_y, 1);
                    const double blue = pixel_at_clamped(image, source_x, source_y, 2);
                    const YCbCr ycbcr = rgb_to_ycbcr(red, green, blue);
                    sum += cr_component ? ycbcr.cr : ycbcr.cb;
                }
            }

            chroma[static_cast<size_t>(y * chroma_width + x)] = sum / 4.0;
        }
    }

    return chroma;
}

double component_at_clamped(const std::vector<double>& component, int width, int height, int x, int y) {
    const int clamped_x = std::clamp(x, 0, width - 1);
    const int clamped_y = std::clamp(y, 0, height - 1);
    return component[static_cast<size_t>(clamped_y * width + clamped_x)];
}

void write_rle_block(std::vector<uint8_t>& bitstream, const double coefficients[kBlockSize][kBlockSize], int q) {
    uint8_t zero_run = 0;

    for (int v = 0; v < kBlockSize; ++v) {
        for (int u = 0; u < kBlockSize; ++u) {
            const int zigzag_index = kZigZag[static_cast<size_t>(v * kBlockSize + u)];
            const int source_y = zigzag_index / kBlockSize;
            const int source_x = zigzag_index % kBlockSize;
            const int quantized = static_cast<int>(std::lround(coefficients[source_y][source_x] / q));
            if (quantized == 0) {
                ++zero_run;
                continue;
            }

            bitstream.push_back(zero_run);
            write_i16(bitstream, quantized);
            zero_run = 0;
        }
    }

    if (zero_run > 0) {
        bitstream.push_back(255);  // end of block: remaining coefficients are zero.
    }
}

void read_rle_block(const std::vector<uint8_t>& bitstream,
                    size_t& offset,
                    int q,
                    double coefficients[kBlockSize][kBlockSize]) {
    int coefficient_index = 0;
    std::fill(&coefficients[0][0], &coefficients[0][0] + kBlockSize * kBlockSize, 0.0);

    while (coefficient_index < kBlockSize * kBlockSize) {
        if (offset >= bitstream.size()) {
            throw std::runtime_error("truncated simplified MJPEG RLE stream");
        }

        const uint8_t zero_run = bitstream[offset++];
        if (zero_run == 255) {
            break;
        }

        coefficient_index += zero_run;
        if (coefficient_index >= kBlockSize * kBlockSize) {
            throw std::runtime_error("bad simplified MJPEG RLE run");
        }

        const int16_t value = read_i16(bitstream, offset);
        const int zigzag_index = kZigZag[static_cast<size_t>(coefficient_index)];
        coefficients[zigzag_index / kBlockSize][zigzag_index % kBlockSize] = value * q;
        ++coefficient_index;
    }
}

void encode_component(std::vector<uint8_t>& bitstream,
                      const std::vector<double>& component,
                      int width,
                      int height,
                      int q) {
    double samples[kBlockSize][kBlockSize]{};
    double coefficients[kBlockSize][kBlockSize]{};

    for (int block_y = 0; block_y < height; block_y += kBlockSize) {
        for (int block_x = 0; block_x < width; block_x += kBlockSize) {
            for (int y = 0; y < kBlockSize; ++y) {
                for (int x = 0; x < kBlockSize; ++x) {
                    samples[y][x] = component_at_clamped(component, width, height, block_x + x, block_y + y) - 128.0;
                }
            }

            forward_dct_block(samples, coefficients);

            write_rle_block(bitstream, coefficients, q);
        }
    }
}

std::vector<double> decode_component(const MjpegFrame& frame, size_t& offset, int width, int height, int q) {
    std::vector<double> component(checked_mul(static_cast<size_t>(width), static_cast<size_t>(height), "component"));
    double coefficients[kBlockSize][kBlockSize]{};
    double samples[kBlockSize][kBlockSize]{};

    for (int block_y = 0; block_y < height; block_y += kBlockSize) {
        for (int block_x = 0; block_x < width; block_x += kBlockSize) {
            read_rle_block(frame.bitstream, offset, q, coefficients);

            inverse_dct_block(coefficients, samples);

            for (int y = 0; y < kBlockSize; ++y) {
                for (int x = 0; x < kBlockSize; ++x) {
                    const int component_x = block_x + x;
                    const int component_y = block_y + y;
                    if (component_x >= width || component_y >= height) {
                        continue;
                    }

                    component[static_cast<size_t>(component_y * width + component_x)] =
                        std::clamp(samples[y][x] + 128.0, 0.0, 255.0);
                }
            }
        }
    }

    return component;
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
    const size_t expected_bytes = rgb_byte_count(image.width, image.height);
    if (image.pixels.size() != expected_bytes) {
        throw std::runtime_error("invalid RGB image");
    }

    const QuantizationConfig quantization = quantization_config(quality_);
    const int chroma_width = half_ceil(image.width);
    const int chroma_height = half_ceil(image.height);
    const std::vector<double> luma = make_luma_component(image);
    const std::vector<double> chroma_blue = make_subsampled_chroma_component(image, false);
    const std::vector<double> chroma_red = make_subsampled_chroma_component(image, true);

    MjpegFrame frame{image.width, image.height, {}};
    frame.bitstream.insert(frame.bitstream.end(), {'S', 'J', 'R', '3', static_cast<uint8_t>(quantization.luma), static_cast<uint8_t>(quantization.chroma)});

    encode_component(frame.bitstream, luma, image.width, image.height, quantization.luma);
    encode_component(frame.bitstream, chroma_blue, chroma_width, chroma_height, quantization.chroma);
    encode_component(frame.bitstream, chroma_red, chroma_width, chroma_height, quantization.chroma);

    return frame;
}

ImageRgb MjpegDecoder::decode(const MjpegFrame& frame) const {
    if (frame.width <= 0 || frame.height <= 0) {
        throw std::runtime_error("bad simplified MJPEG frame dimensions");
    }
    (void)rgb_byte_count(frame.width, frame.height);
    if (frame.bitstream.size() < 6 || std::string(reinterpret_cast<const char*>(frame.bitstream.data()), 4) != "SJR3") {
        throw std::runtime_error("bad simplified MJPEG stream magic/header");
    }

    const int q_luma = frame.bitstream[4];
    const int q_chroma = frame.bitstream[5];
    if (q_luma == 0 || q_chroma == 0) {
        throw std::runtime_error("bad simplified MJPEG quantization scale");
    }
    const int chroma_width = half_ceil(frame.width);
    const int chroma_height = half_ceil(frame.height);
    size_t offset = 6;

    const std::vector<double> luma = decode_component(frame, offset, frame.width, frame.height, q_luma);
    const std::vector<double> chroma_blue = decode_component(frame, offset, chroma_width, chroma_height, q_chroma);
    const std::vector<double> chroma_red = decode_component(frame, offset, chroma_width, chroma_height, q_chroma);
    if (offset != frame.bitstream.size()) {
        throw std::runtime_error("unexpected trailing bytes in simplified MJPEG stream");
    }

    ImageRgb image{frame.width, frame.height, std::vector<uint8_t>(rgb_byte_count(frame.width, frame.height))};
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const double yy = luma[static_cast<size_t>(y * frame.width + x)];
            const double cb = component_at_clamped(chroma_blue, chroma_width, chroma_height, x / 2, y / 2);
            const double cr = component_at_clamped(chroma_red, chroma_width, chroma_height, x / 2, y / 2);

            uint8_t red = 0;
            uint8_t green = 0;
            uint8_t blue = 0;
            ycbcr_to_rgb(yy, cb, cr, red, green, blue);

            const size_t pixel_offset = (static_cast<size_t>(y) * static_cast<size_t>(frame.width) + static_cast<size_t>(x)) * 3;
            image.pixels[pixel_offset] = red;
            image.pixels[pixel_offset + 1] = green;
            image.pixels[pixel_offset + 2] = blue;
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

    const size_t byte_count = rgb_byte_count(width, height);
    ImageRgb image{width, height, std::vector<uint8_t>(pixels, pixels + byte_count)};
    stbi_image_free(pixels);
    return image;
}

void save_png_rgb(const std::string& path, const ImageRgb& image) {
    (void)rgb_byte_count(image.width, image.height);
    if (image.pixels.size() != rgb_byte_count(image.width, image.height)) {
        throw std::runtime_error("invalid RGB image");
    }
    const int stride_bytes = image.width * 3;
    if (!stbi_write_png(path.c_str(), image.width, image.height, 3, image.pixels.data(), stride_bytes)) {
        throw std::runtime_error("failed to write image: " + path);
    }
}

std::vector<MjpegFrame> encode_folder(const std::string& folder, int quality) {
    std::vector<MjpegFrame> frames;
    MjpegEncoder encoder(quality);

    std::vector<std::filesystem::path> png_paths;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.path().extension() == ".png") {
            png_paths.push_back(entry.path());
        }
    }
    std::sort(png_paths.begin(), png_paths.end());
    frames.reserve(png_paths.size());
    for (const std::filesystem::path& png_path : png_paths) {
        frames.push_back(encoder.encode(load_png_rgb(png_path.string())));
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


void write_mjpeg_stream(const std::string& path, const std::vector<MjpegFrame>& frames) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot create MJPEG file: " + path);
    }

    file.write("CMJ2", 4);
    write_u32(file, checked_u32(frames.size(), "frame count"));

    for (const MjpegFrame& frame : frames) {
        if (frame.width <= 0 || frame.height <= 0) {
            throw std::runtime_error("cannot write MJPEG frame with invalid dimensions");
        }
        write_u32(file, static_cast<uint32_t>(frame.width));
        write_u32(file, static_cast<uint32_t>(frame.height));
        write_u32(file, checked_u32(frame.bitstream.size(), "frame bitstream size"));
        file.write(reinterpret_cast<const char*>(frame.bitstream.data()),
                   static_cast<std::streamsize>(frame.bitstream.size()));
    }
}

}  // namespace carrot
