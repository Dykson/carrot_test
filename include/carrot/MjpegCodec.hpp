#pragma once

#include "carrot/Image.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace carrot {

struct MjpegEncoderOptions {
    std::uint8_t quality = 70;
};

class MjpegEncoder {
public:
    explicit MjpegEncoder(MjpegEncoderOptions options = {});

    void encodeFolder(const std::filesystem::path& pngFolder,
                      const std::filesystem::path& outputFile) const;
    void encodeImages(const std::vector<Image>& frames,
                      const std::filesystem::path& outputFile) const;
    [[nodiscard]] std::vector<std::uint8_t> encodeFrame(const Image& frame) const;

private:
    MjpegEncoderOptions options_;
};

class MjpegDecoder {
public:
    [[nodiscard]] std::vector<Image> decodeFile(const std::filesystem::path& inputFile) const;
    void decodeToFolder(const std::filesystem::path& inputFile,
                        const std::filesystem::path& outputFolder) const;
    [[nodiscard]] Image decodeFrame(const std::vector<std::uint8_t>& data) const;
};

} // namespace carrot
