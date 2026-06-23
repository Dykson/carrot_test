#include "ima_adpcm.h"
#include "mjpeg.h"
#include "wav.h"

#include <SDL.h>
#if defined(CARROT_WITH_GLAD)
#include <glad/glad.h>
#else
#include <SDL_opengl.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct AudioState {
    const uint8_t* data = nullptr;
    uint32_t byte_count = 0;
    std::atomic<uint32_t> cursor{0};
};

void audio_callback(void* userdata, uint8_t* stream, int stream_size) {
    auto* state = static_cast<AudioState*>(userdata);
    SDL_memset(stream, 0, stream_size);

    const uint32_t cursor = state->cursor.load(std::memory_order_relaxed);
    if (cursor >= state->byte_count) {
        return;
    }

    const uint32_t available = state->byte_count - cursor;
    const uint32_t bytes_to_copy = std::min<uint32_t>(available, static_cast<uint32_t>(stream_size));
    SDL_memcpy(stream, state->data + cursor, bytes_to_copy);
    state->cursor.store(cursor + bytes_to_copy, std::memory_order_relaxed);
}

GLuint create_texture(const carrot::ImageRgb& first_frame) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGB8,
        first_frame.width,
        first_frame.height,
        0,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        first_frame.pixels.data());
    return texture;
}

void draw_textured_fullscreen_quad(GLuint texture) {
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);

    // The context is requested as OpenGL 4.6. For a compact test-player implementation
    // this uses the compatibility profile immediate-mode path; swapping it for a glad-loaded
    // shader/VBO path does not affect the codec synchronization logic.
    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0.0F, 1.0F);
    glVertex2f(-1.0F, -1.0F);
    glTexCoord2f(1.0F, 1.0F);
    glVertex2f(1.0F, -1.0F);
    glTexCoord2f(0.0F, 0.0F);
    glVertex2f(-1.0F, 1.0F);
    glTexCoord2f(1.0F, 0.0F);
    glVertex2f(1.0F, 1.0F);
    glEnd();
}

std::vector<carrot::ImageRgb> load_decoded_frames(const std::string& folder) {
    const std::vector<carrot::MjpegFrame> encoded_frames = carrot::encode_folder(folder, 50);
    const carrot::MjpegDecoder decoder;
    std::vector<carrot::ImageRgb> decoded_frames;
    decoded_frames.reserve(encoded_frames.size());

    for (const carrot::MjpegFrame& encoded_frame : encoded_frames) {
        decoded_frames.push_back(decoder.decode(encoded_frame));
    }

    return decoded_frames;
}

void print_usage() {
    std::cout << "Usage: simple_player <audio.wav> <png_frames_folder> [fps]\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 0;
    }

    try {
        const std::string audio_path = argv[1];
        const std::string frames_folder = argv[2];
        const double fps = argc >= 4 ? std::stod(argv[3]) : 25.0;

        carrot::WavPcm wav = carrot::read_wav_pcm16(audio_path);
        const carrot::ImaAdpcmEncoder audio_encoder;
        const carrot::ImaAdpcmDecoder audio_decoder;
        wav.samples = audio_decoder.decode(audio_encoder.encode(wav.samples, wav.channels), wav.channels);

        std::vector<carrot::ImageRgb> frames = load_decoded_frames(frames_folder);
        if (frames.empty()) {
            throw std::runtime_error("no PNG frames found in: " + frames_folder);
        }

        if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
            throw std::runtime_error(SDL_GetError());
        }

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        SDL_Window* window = SDL_CreateWindow(
            "Carrot codec player",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            frames.front().width,
            frames.front().height,
            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
        if (window == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }

        SDL_GLContext gl_context = SDL_GL_CreateContext(window);
        if (gl_context == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }
#if defined(CARROT_WITH_GLAD)
        if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)) == 0) {
            throw std::runtime_error("failed to initialize glad");
        }
#endif
        SDL_GL_SetSwapInterval(1);

        AudioState audio_state;
        audio_state.data = reinterpret_cast<const uint8_t*>(wav.samples.data());
        audio_state.byte_count = static_cast<uint32_t>(wav.samples.size() * sizeof(int16_t));

        SDL_AudioSpec desired{};
        desired.freq = static_cast<int>(wav.sample_rate);
        desired.format = AUDIO_S16SYS;
        desired.channels = static_cast<uint8_t>(wav.channels);
        desired.samples = 1024;
        desired.callback = audio_callback;
        desired.userdata = &audio_state;

        SDL_AudioSpec obtained{};
        SDL_AudioDeviceID audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
        if (audio_device == 0) {
            throw std::runtime_error(SDL_GetError());
        }

        GLuint texture = create_texture(frames.front());
        SDL_PauseAudioDevice(audio_device, 0);

        bool running = true;
        const auto start_time = std::chrono::steady_clock::now();

        while (running) {
            SDL_Event event{};
            while (SDL_PollEvent(&event) != 0) {
                if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                    running = false;
                }
            }

            const auto now = std::chrono::steady_clock::now();
            const double seconds = std::chrono::duration<double>(now - start_time).count();
            const size_t frame_index = std::min<size_t>(static_cast<size_t>(seconds * fps), frames.size() - 1);
            const carrot::ImageRgb& frame = frames[frame_index];

            glBindTexture(GL_TEXTURE_2D, texture);
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                0,
                0,
                frame.width,
                frame.height,
                GL_RGB,
                GL_UNSIGNED_BYTE,
                frame.pixels.data());

            draw_textured_fullscreen_quad(texture);
            SDL_GL_SwapWindow(window);

            if (frame_index + 1 == frames.size() && audio_state.cursor.load(std::memory_order_relaxed) >= audio_state.byte_count) {
                running = false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        SDL_CloseAudioDevice(audio_device);
        glDeleteTextures(1, &texture);
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
