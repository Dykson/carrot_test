#include "player.h"

#include "ima_adpcm.h"
#include "mjpeg.h"
#include "wav.h"

#include <SDL3/SDL.h>
#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct AudioState
{
    const uint8_t *data = nullptr;
    uint32_t byte_count = 0;
    uint32_t bytes_per_second = 0;
    std::atomic<uint32_t> cursor{0};
    std::atomic<uint64_t> bytes_written{0};
};

static void audio_callback(void *userdata,
                           SDL_AudioStream *stream,
                           int additional_amount,
                           int /*total_amount*/)
{
    auto *state = static_cast<AudioState *>(userdata);

    if (state == nullptr || state->data == nullptr || state->byte_count == 0) {
        return;
    }

    int bytes_to_write = additional_amount;

    while (bytes_to_write > 0) {
        uint32_t cursor = state->cursor.load(std::memory_order_relaxed);

        if (cursor >= state->byte_count) {
            cursor = 0;
        }

        const uint32_t bytes_left_until_loop = state->byte_count - cursor;
        const uint32_t chunk_size = std::min<uint32_t>(static_cast<uint32_t>(bytes_to_write),
                                                       bytes_left_until_loop);

        SDL_PutAudioStreamData(stream, state->data + cursor, static_cast<int>(chunk_size));

        cursor += chunk_size;

        if (cursor >= state->byte_count) {
            cursor = 0;
        }

        state->cursor.store(cursor, std::memory_order_relaxed);
        state->bytes_written.fetch_add(chunk_size, std::memory_order_relaxed);

        bytes_to_write -= static_cast<int>(chunk_size);
    }
}

struct Renderer
{
    GLuint texture = 0;
    GLuint program = 0;
    GLuint vertex_array = 0;
    GLuint vertex_buffer = 0;
};

struct PlaybackState
{
    const std::vector<carrot::ImageRgb> &frames;
    const AudioState &audio_state;
    SDL_AudioStream *audio_stream = nullptr;
    double fps = 10.0;
    double loop_duration = 0.0;
    size_t current_frame = 0;
    double audio_pts = 0.0;
    double video_pts = 0.0;
    double av_delta_ms = 0.0;
};

struct DiagnosticsState
{
    using Clock = std::chrono::steady_clock;
    Clock::time_point last_report = Clock::now();
    uint64_t rendered_frames = 0;
    bool has_printed_report = false;
};

GLuint compile_shader(GLenum type, const char *source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE) {
        return shader;
    }

    std::array<GLchar, 512> log{};
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error(std::string("shader compilation failed: ") + log.data());
}

GLuint create_program()
{
    const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER,
                                                "#version 330 core\n"
                                                "layout(location = 0) in vec2 position;\n"
                                                "layout(location = 1) in vec2 tex_coord;\n"
                                                "out vec2 uv;\n"
                                                "void main() {\n"
                                                "    uv = tex_coord;\n"
                                                "    gl_Position = vec4(position, 0.0, 1.0);\n"
                                                "}\n");
    const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
                                                  "#version 330 core\n"
                                                  "in vec2 uv;\n"
                                                  "out vec4 color;\n"
                                                  "uniform sampler2D frame_texture;\n"
                                                  "void main() {\n"
                                                  "    color = texture(frame_texture, uv);\n"
                                                  "}\n");
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_TRUE) {
        return program;
    }

    std::array<GLchar, 512> log{};
    glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
    glDeleteProgram(program);
    throw std::runtime_error(std::string("shader linking failed: ") + log.data());
}

Renderer create_renderer(const carrot::ImageRgb &first_frame)
{
    Renderer renderer{};
    renderer.program = create_program();

    glGenTextures(1, &renderer.texture);
    glBindTexture(GL_TEXTURE_2D, renderer.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGB8,
                 first_frame.width,
                 first_frame.height,
                 0,
                 GL_RGB,
                 GL_UNSIGNED_BYTE,
                 first_frame.pixels.data());

    constexpr std::array<float, 16> vertices{
        -1.0F,
        -1.0F,
        0.0F,
        1.0F,
        1.0F,
        -1.0F,
        1.0F,
        1.0F,
        -1.0F,
        1.0F,
        0.0F,
        0.0F,
        1.0F,
        1.0F,
        1.0F,
        0.0F,
    };

    glGenVertexArrays(1, &renderer.vertex_array);
    glBindVertexArray(renderer.vertex_array);
    glGenBuffers(1, &renderer.vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer.vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    constexpr GLsizei stride = 4 * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          stride,
                          reinterpret_cast<void *>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glUseProgram(renderer.program);
    glUniform1i(glGetUniformLocation(renderer.program, "frame_texture"), 0);
    return renderer;
}


double audio_clock_seconds(const AudioState &state, SDL_AudioStream *stream)
{
    if (state.bytes_per_second == 0) {
        return 0.0;
    }

    const uint64_t written = state.bytes_written.load(std::memory_order_relaxed);
    const int queued = std::max(0, SDL_GetAudioStreamQueued(stream));
    const uint64_t queued_bytes = static_cast<uint64_t>(queued);
    const uint64_t played = written > queued_bytes ? written - queued_bytes : 0;
    return static_cast<double>(played) / static_cast<double>(state.bytes_per_second);
}

void draw_textured_fullscreen_quad(const Renderer &renderer)
{
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(renderer.program);
    glBindVertexArray(renderer.vertex_array);
    glBindTexture(GL_TEXTURE_2D, renderer.texture);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void upload_frame(const carrot::ImageRgb &frame)
{
    glTexSubImage2D(GL_TEXTURE_2D,
                    0,
                    0,
                    0,
                    frame.width,
                    frame.height,
                    GL_RGB,
                    GL_UNSIGNED_BYTE,
                    frame.pixels.data());
}

void process_events(bool &running)
{
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_EVENT_QUIT
            || (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
            running = false;
        }
    }
}

void print_diagnostics_if_due(const PlaybackState &playback, DiagnosticsState &diagnostics)
{
    ++diagnostics.rendered_frames;

    const DiagnosticsState::Clock::time_point now = DiagnosticsState::Clock::now();
    const std::chrono::duration<double> elapsed = now - diagnostics.last_report;
    if (elapsed.count() < 1.0) {
        return;
    }

    const double measured_fps = static_cast<double>(diagnostics.rendered_frames) / elapsed.count();
    if (diagnostics.has_printed_report) {
        std::cout << "\033[5F";
    }

    std::cout << std::fixed << std::setprecision(2)
              << "\033[2KFPS: " << measured_fps << '\n'
              << "\033[2KFrame: " << playback.current_frame << '\n'
              << std::setprecision(3)
              << "\033[2KAudio PTS: " << playback.audio_pts << '\n'
              << "\033[2KVideo PTS: " << playback.video_pts << '\n'
              << std::setprecision(1)
              << "\033[2KAV Delta: " << (playback.av_delta_ms >= 0.0 ? "+" : "")
              << playback.av_delta_ms << " ms\n"
              << std::flush;

    diagnostics.has_printed_report = true;
    diagnostics.last_report = now;
    diagnostics.rendered_frames = 0;
}

void update(PlaybackState &playback)
{
    const double raw_audio_pts = audio_clock_seconds(playback.audio_state, playback.audio_stream);
    playback.audio_pts = playback.loop_duration > 0.0
        ? std::fmod(raw_audio_pts, playback.loop_duration)
        : raw_audio_pts;

    const size_t next_frame =
        static_cast<size_t>(playback.audio_pts * playback.fps) % playback.frames.size();
    if (next_frame != playback.current_frame) {
        playback.current_frame = next_frame;
        upload_frame(playback.frames[playback.current_frame]);
    }

    playback.video_pts = static_cast<double>(playback.current_frame) / playback.fps;
    playback.av_delta_ms = (playback.video_pts - playback.audio_pts) * 1000.0;
}

void render(const Renderer &renderer,
            const PlaybackState &playback,
            DiagnosticsState &diagnostics)
{
    draw_textured_fullscreen_quad(renderer);
    print_diagnostics_if_due(playback, diagnostics);
}

void destroy_renderer(const Renderer &renderer)
{
    glDeleteBuffers(1, &renderer.vertex_buffer);
    glDeleteVertexArrays(1, &renderer.vertex_array);
    glDeleteProgram(renderer.program);
    glDeleteTextures(1, &renderer.texture);
}

std::vector<carrot::ImageRgb> load_decoded_frames(const std::string &folder)
{
    const std::vector<carrot::MjpegFrame> encoded_frames = carrot::encode_folder(folder, 100);
    std::filesystem::create_directories("media/out");
    carrot::write_mjpeg_stream("media/out/video.mjpeg", encoded_frames);
    const carrot::MjpegDecoder decoder;
    std::vector<carrot::ImageRgb> decoded_frames;
    decoded_frames.reserve(encoded_frames.size());

    for (const carrot::MjpegFrame &encoded_frame : encoded_frames) {
        decoded_frames.push_back(decoder.decode(encoded_frame));
    }

    return decoded_frames;
}

void print_usage()
{
    std::cout << "Usage: codec_tool player <audio.wav> <png_frames_folder> [fps]\n";
}

} // namespace

int carrot::run_player(int argc, char **argv)
{
    if (argc < 3) {
        print_usage();
        return 0;
    }

    try {
        const std::string audio_path = argv[1];
        const std::string frames_folder = argv[2];
        const double fps = argc >= 4 ? std::stod(argv[3]) : 25.0;
        if (fps <= 0.0) {
            throw std::runtime_error("fps must be positive");
        }

        carrot::WavPcm wav = carrot::read_wav_pcm16(audio_path);
        const carrot::ImaAdpcmEncoder audio_encoder;
        const carrot::ImaAdpcmDecoder audio_decoder;
        const std::vector<carrot::ImaAdpcmBlock> encoded_audio = audio_encoder.encode(wav.samples,
                                                                                      wav.channels);
        std::filesystem::create_directories("media/out");
        carrot::write_ima_adpcm_stream("media/out/audio.adpcm",
                                       encoded_audio,
                                       wav.channels,
                                       wav.sample_rate);
        wav.samples = audio_decoder.decode(encoded_audio, wav.channels);

        std::vector<carrot::ImageRgb> frames = load_decoded_frames(frames_folder);
        if (frames.empty()) {
            throw std::runtime_error("no PNG frames found in: " + frames_folder);
        }
        for (const auto &frame : frames) {
            if (frame.width != frames.front().width || frame.height != frames.front().height) {
                throw std::runtime_error("all video frames must have the same dimensions");
            }
        }

        if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            throw std::runtime_error(SDL_GetError());
        }

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        SDL_Window *window = SDL_CreateWindow("Carrot codec player",
                                              frames.front().width,
                                              frames.front().height,
                                              SDL_WINDOW_OPENGL);
        if (window == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }

        SDL_GLContext gl_context = SDL_GL_CreateContext(window);
        if (gl_context == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }
        if (!gladLoadGL(SDL_GL_GetProcAddress)) {
            throw std::runtime_error("failed to load OpenGL functions");
        }
        SDL_GL_SetSwapInterval(1);

        AudioState audio_state;
        audio_state.data = reinterpret_cast<const uint8_t *>(wav.samples.data());
        if (wav.samples.size() > std::numeric_limits<uint32_t>::max() / sizeof(int16_t)) {
            throw std::runtime_error("decoded audio buffer is too large for SDL callback state");
        }
        audio_state.byte_count = static_cast<uint32_t>(wav.samples.size() * sizeof(int16_t));
        if (wav.channels == 0 || wav.sample_rate > std::numeric_limits<uint32_t>::max() / (static_cast<uint32_t>(wav.channels) * sizeof(int16_t))) {
            throw std::runtime_error("audio byte rate overflow");
        }
        audio_state.bytes_per_second = wav.sample_rate * wav.channels * sizeof(int16_t);

        SDL_AudioSpec desired{};
        desired.freq = static_cast<int>(wav.sample_rate);
        desired.format = SDL_AUDIO_S16;
        desired.channels = static_cast<int>(wav.channels);

        SDL_AudioStream *audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                                  &desired,
                                                                  audio_callback,
                                                                  &audio_state);
        if (audio_stream == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }

        Renderer renderer = create_renderer(frames.front());

        draw_textured_fullscreen_quad(renderer);
        SDL_GL_SwapWindow(window);

        SDL_ClearAudioStream(audio_stream);
        audio_state.cursor.store(0, std::memory_order_relaxed);
        audio_state.bytes_written.store(0, std::memory_order_relaxed);
        SDL_ResumeAudioStreamDevice(audio_stream);

        bool running = true;
        PlaybackState playback{frames, audio_state, audio_stream, fps};
        playback.loop_duration = static_cast<double>(frames.size()) / fps;
        DiagnosticsState diagnostics;

        while (running) {
            process_events(running);
            update(playback);
            render(renderer, playback, diagnostics);
            SDL_GL_SwapWindow(window);
        }

        SDL_DestroyAudioStream(audio_stream);
        destroy_renderer(renderer);
        SDL_GL_DestroyContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
