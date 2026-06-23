#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace carrot {
struct ImageRgb { int width{}; int height{}; std::vector<uint8_t> pixels; };
struct MjpegFrame { int width{}; int height{}; std::vector<uint8_t> bitstream; };
class MjpegEncoder { public: explicit MjpegEncoder(int quality = 50); MjpegFrame encode(const ImageRgb& image) const; private: int quality_; };
class MjpegDecoder { public: ImageRgb decode(const MjpegFrame& frame) const; };
ImageRgb load_png_rgb(const std::string& path);
void save_png_rgb(const std::string& path, const ImageRgb& image);
std::vector<MjpegFrame> encode_folder(const std::string& folder, int quality);
void decode_folder(const std::vector<MjpegFrame>& frames, const std::string& folder);
}
