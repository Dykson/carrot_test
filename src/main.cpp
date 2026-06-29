#include "ima_adpcm.h"
#include "mjpeg.h"
#include "wav.h"

#if defined(CARROT_WITH_PLAYER)
#include "player.h"
#endif

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cout << "Usage:\n"
              << "  codec_tool adpcm <input.wav> <decoded.wav>\n"
              << "  codec_tool mjpeg <png_folder> <decoded_folder>\n"
              << "  codec_tool player <input.wav> <png_frames_folder> [fps]\n";
}

int run_adpcm_roundtrip(const char* input_path, const char* output_path) {
    carrot::WavPcm wav = carrot::read_wav_pcm16(input_path);
    const carrot::ImaAdpcmEncoder encoder;
    const carrot::ImaAdpcmDecoder decoder;

    const std::vector<carrot::ImaAdpcmBlock> encoded = encoder.encode(wav.samples, wav.channels);
    std::filesystem::create_directories("media");
    carrot::write_ima_adpcm_stream("media/audio.adpcm", encoded, wav.channels, wav.sample_rate);
    wav.samples = decoder.decode(encoded, wav.channels);
    carrot::write_wav_pcm16(output_path, wav);
    return 0;
}

int run_mjpeg_roundtrip(const char* input_folder, const char* output_folder) {
    const std::vector<carrot::MjpegFrame> encoded_frames = carrot::encode_folder(input_folder, 50);
    std::filesystem::create_directories("media");
    carrot::write_mjpeg_stream("media/video.mjpeg", encoded_frames);
    carrot::decode_folder(encoded_frames, output_folder);
    return 0;
}

int run_player_mode(int argc, char** argv) {
#if defined(CARROT_WITH_PLAYER)
    return carrot::run_player(argc - 1, argv + 1);
#else
    (void)argc;
    (void)argv;
    std::cerr << "player mode is unavailable: rebuild with SDL3 and OpenGL development packages\n";
    return 3;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            print_usage();
            return 0;
        }

        const std::string mode = argv[1];
        if (mode == "adpcm" && argc == 4) {
            return run_adpcm_roundtrip(argv[2], argv[3]);
        }

        if (mode == "mjpeg" && argc == 4) {
            return run_mjpeg_roundtrip(argv[2], argv[3]);
        }

        if (mode == "player" && argc >= 4) {
            return run_player_mode(argc, argv);
        }

        print_usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
