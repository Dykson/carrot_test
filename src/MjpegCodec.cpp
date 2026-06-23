#include "carrot/MjpegCodec.hpp"
#include "carrot/BitStream.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace carrot {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr std::array<int, 64> zigzag{0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
constexpr std::array<int, 64> qy{16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99};
constexpr std::array<int, 64> qc{17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99};

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) { for (int s : {24,16,8,0}) out.push_back(static_cast<std::uint8_t>(v >> s)); }
std::uint32_t get32(const std::vector<std::uint8_t>& in, std::size_t& p) { if (p + 4 > in.size()) throw std::runtime_error("truncated stream"); std::uint32_t v=0; for(int i=0;i<4;++i) v=(v<<8U)|in[p++]; return v; }
void put16(std::vector<std::uint8_t>& out, std::uint16_t v) { out.push_back(v>>8U); out.push_back(v&255U); }
std::int16_t get16(const std::vector<std::uint8_t>& in, std::size_t& p) { if (p + 2 > in.size()) throw std::runtime_error("truncated stream"); return static_cast<std::int16_t>((in[p++] << 8U) | in[p++]); }
std::uint8_t clampByte(double v) { return static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(v)), 0, 255)); }

std::array<int, 64> scaledQuant(const std::array<int, 64>& base, std::uint8_t quality) {
    quality = std::clamp<std::uint8_t>(quality, 1, 100);
    const int scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
    std::array<int, 64> out{};
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = std::clamp((base[i] * scale + 50) / 100, 1, 255);
    return out;
}

void dct8(const double in[64], double out[64]) {
    for (int v = 0; v < 8; ++v) for (int u = 0; u < 8; ++u) {
        double sum = 0.0;
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) sum += in[y*8+x] * std::cos(((2*x+1)*u*pi)/16.0) * std::cos(((2*y+1)*v*pi)/16.0);
        const double cu = u == 0 ? 1.0 / std::sqrt(2.0) : 1.0;
        const double cv = v == 0 ? 1.0 / std::sqrt(2.0) : 1.0;
        out[v*8+u] = 0.25 * cu * cv * sum;
    }
}

void idct8(const double in[64], double out[64]) {
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
        double sum = 0.0;
        for (int v = 0; v < 8; ++v) for (int u = 0; u < 8; ++u) {
            const double cu = u == 0 ? 1.0 / std::sqrt(2.0) : 1.0;
            const double cv = v == 0 ? 1.0 / std::sqrt(2.0) : 1.0;
            sum += cu * cv * in[v*8+u] * std::cos(((2*x+1)*u*pi)/16.0) * std::cos(((2*y+1)*v*pi)/16.0);
        }
        out[y*8+x] = 0.25 * sum;
    }
}

void writeSigned(BitWriter& w, int value) { const auto mag = static_cast<std::uint32_t>(std::abs(value)); std::uint8_t bits = 0; while ((1U << bits) <= mag && bits < 15) ++bits; w.writeBits(bits, 4); if (bits) { w.writeBit(value < 0); w.writeBits(mag, bits); } }
int readSigned(BitReader& r) { const auto bits = static_cast<std::uint8_t>(r.readBits(4)); if (!bits) return 0; const bool neg = r.readBit(); const int mag = static_cast<int>(r.readBits(bits)); return neg ? -mag : mag; }

void encodeBlock(BitWriter& w, const double values[64], const std::array<int, 64>& q) {
    double freq[64]; dct8(values, freq);
    std::array<int, 64> coeff{};
    for (int i = 0; i < 64; ++i) coeff[i] = static_cast<int>(std::lround(freq[i] / q[i]));
    writeSigned(w, coeff[0]);
    int run = 0;
    for (int i = 1; i < 64; ++i) {
        const int c = coeff[zigzag[i]];
        if (c == 0) { ++run; continue; }
        while (run > 15) { w.writeBits(15, 4); writeSigned(w, 0); run -= 15; }
        w.writeBits(static_cast<std::uint32_t>(run), 4); writeSigned(w, c); run = 0;
    }
    w.writeBits(15, 4); writeSigned(w, 0);
}

void decodeBlock(BitReader& r, double values[64], const std::array<int, 64>& q) {
    std::array<int, 64> coeff{};
    coeff[0] = readSigned(r);
    int pos = 1;
    while (pos < 64) {
        const int run = static_cast<int>(r.readBits(4));
        const int value = readSigned(r);
        if (run == 15 && value == 0) break;
        pos += run;
        if (pos < 64) coeff[zigzag[pos++]] = value;
    }
    double freq[64]; for (int i = 0; i < 64; ++i) freq[i] = coeff[i] * q[i];
    idct8(freq, values);
}

} // namespace

MjpegEncoder::MjpegEncoder(MjpegEncoderOptions options) : options_(options) {}

std::vector<std::uint8_t> MjpegEncoder::encodeFrame(const Image& frame) const {
    std::vector<std::uint8_t> out{'S','J','P','G'}; put32(out, frame.width()); put32(out, frame.height()); out.push_back(options_.quality);
    const auto yq = scaledQuant(qy, options_.quality); const auto cq = scaledQuant(qc, options_.quality);
    BitWriter w;
    for (std::uint32_t by = 0; by < frame.height(); by += 8) for (std::uint32_t bx = 0; bx < frame.width(); bx += 8) {
        double planes[3][64]{};
        for (int yy = 0; yy < 8; ++yy) for (int xx = 0; xx < 8; ++xx) {
            const auto p = frame.at(std::min<std::uint32_t>(bx + xx, frame.width() - 1), std::min<std::uint32_t>(by + yy, frame.height() - 1));
            const double y = 0.299*p.r + 0.587*p.g + 0.114*p.b;
            const double cb = -0.168736*p.r - 0.331264*p.g + 0.5*p.b + 128.0;
            const double cr = 0.5*p.r - 0.418688*p.g - 0.081312*p.b + 128.0;
            const int i = yy*8+xx; planes[0][i]=y-128.0; planes[1][i]=cb-128.0; planes[2][i]=cr-128.0;
        }
        encodeBlock(w, planes[0], yq); encodeBlock(w, planes[1], cq); encodeBlock(w, planes[2], cq);
    }
    auto bits = w.finish(); put32(out, static_cast<std::uint32_t>(bits.size())); out.insert(out.end(), bits.begin(), bits.end()); return out;
}

Image MjpegDecoder::decodeFrame(const std::vector<std::uint8_t>& data) const {
    std::size_t p = 0; if (data.size() < 17 || std::string(data.begin(), data.begin()+4) != "SJPG") throw std::runtime_error("not a simplified JPEG frame"); p = 4;
    const auto width = get32(data, p); const auto height = get32(data, p); const auto quality = data[p++]; const auto bitSize = get32(data, p);
    if (p + bitSize > data.size()) throw std::runtime_error("truncated frame payload");
    std::vector<std::uint8_t> bits(data.begin()+static_cast<long>(p), data.begin()+static_cast<long>(p+bitSize)); BitReader r(bits);
    const auto yq = scaledQuant(qy, quality); const auto cq = scaledQuant(qc, quality); Image image(width, height);
    for (std::uint32_t by = 0; by < height; by += 8) for (std::uint32_t bx = 0; bx < width; bx += 8) {
        double planes[3][64]; decodeBlock(r, planes[0], yq); decodeBlock(r, planes[1], cq); decodeBlock(r, planes[2], cq);
        for (int yy = 0; yy < 8; ++yy) for (int xx = 0; xx < 8; ++xx) if (bx+xx < width && by+yy < height) {
            const int i = yy*8+xx; const double y = planes[0][i] + 128.0; const double cb = planes[1][i]; const double cr = planes[2][i];
            image.at(bx+xx, by+yy) = RgbPixel{clampByte(y + 1.402*cr), clampByte(y - 0.344136*cb - 0.714136*cr), clampByte(y + 1.772*cb)};
        }
    }
    return image;
}

void MjpegEncoder::encodeImages(const std::vector<Image>& frames, const std::filesystem::path& outputFile) const {
    std::ofstream file(outputFile, std::ios::binary); if (!file) throw std::runtime_error("cannot open MJPEG output");
    file.write("SMJ1", 4); std::vector<std::vector<std::uint8_t>> encoded; for (const auto& f : frames) encoded.push_back(encodeFrame(f));
    std::vector<std::uint8_t> header; put32(header, static_cast<std::uint32_t>(encoded.size())); file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    for (const auto& frame : encoded) { std::vector<std::uint8_t> len; put32(len, static_cast<std::uint32_t>(frame.size())); file.write(reinterpret_cast<const char*>(len.data()), 4); file.write(reinterpret_cast<const char*>(frame.data()), static_cast<std::streamsize>(frame.size())); }
}

void MjpegEncoder::encodeFolder(const std::filesystem::path& pngFolder, const std::filesystem::path& outputFile) const {
    std::vector<std::filesystem::path> files; for (const auto& e : std::filesystem::directory_iterator(pngFolder)) if (e.path().extension() == ".png") files.push_back(e.path());
    std::sort(files.begin(), files.end()); std::vector<Image> frames; for (const auto& f : files) frames.push_back(readPng(f)); encodeImages(frames, outputFile);
}

std::vector<Image> MjpegDecoder::decodeFile(const std::filesystem::path& inputFile) const {
    std::ifstream file(inputFile, std::ios::binary); if (!file) throw std::runtime_error("cannot open MJPEG input");
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(file)), {}); std::size_t p = 0; if (data.size() < 8 || std::string(data.begin(), data.begin()+4) != "SMJ1") throw std::runtime_error("not an SMJ1 file"); p = 4;
    const auto count = get32(data, p); std::vector<Image> frames; for (std::uint32_t i = 0; i < count; ++i) { const auto len = get32(data, p); if (p + len > data.size()) throw std::runtime_error("truncated MJPEG file"); frames.push_back(decodeFrame({data.begin()+static_cast<long>(p), data.begin()+static_cast<long>(p+len)})); p += len; } return frames;
}

void MjpegDecoder::decodeToFolder(const std::filesystem::path& inputFile, const std::filesystem::path& outputFolder) const {
    std::filesystem::create_directories(outputFolder); const auto frames = decodeFile(inputFile);
    for (std::size_t i = 0; i < frames.size(); ++i) { std::ostringstream name; name << "frame_" << std::setw(4) << std::setfill('0') << i << ".png"; writePng(outputFolder / name.str(), frames[i]); }
}

} // namespace carrot
