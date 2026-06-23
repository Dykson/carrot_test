#include "mjpeg.h"

#include "stb_image.h"
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace carrot {
namespace {

constexpr double kPi = 3.14159265358979323846;
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

void forward_dct_block(const double input[kBlockSize][kBlockSize], double output[kBlockSize][kBlockSize]) {
    for (int v = 0; v < kBlockSize; ++v) {
        for (int u = 0; u < kBlockSize; ++u) {
            double sum = 0.0;

            for (int y = 0; y < kBlockSize; ++y) {
                for (int x = 0; x < kBlockSize; ++x) {
                    sum += input[y][x] *
                           std::cos((2 * x + 1) * u * kPi / 16.0) *
                           std::cos((2 * y + 1) * v * kPi / 16.0);
                }
            }

            const double cu = (u == 0) ? 1.0 / std::sqrt(2.0) : 1.0;
            const double cv = (v == 0) ? 1.0 / std::sqrt(2.0) : 1.0;
            output[v][u] = 0.25 * cu * cv * sum;
        }
    }
}

void inverse_dct_block(const double input[kBlockSize][kBlockSize], double output[kBlockSize][kBlockSize]) {
    for (int y = 0; y < kBlockSize; ++y) {
        for (int x = 0; x < kBlockSize; ++x) {
            double sum = 0.0;

            for (int v = 0; v < kBlockSize; ++v) {
                for (int u = 0; u < kBlockSize; ++u) {
                    const double cu = (u == 0) ? 1.0 / std::sqrt(2.0) : 1.0;
                    const double cv = (v == 0) ? 1.0 / std::sqrt(2.0) : 1.0;
                    sum += cu * cv * input[v][u] *
                           std::cos((2 * x + 1) * u * kPi / 16.0) *
                           std::cos((2 * y + 1) * v * kPi / 16.0);
                }
            }

            output[y][x] = 0.25 * sum;
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
