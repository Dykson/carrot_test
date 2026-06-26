# Carrot Broadcast media codec test

C++17 implementation of the requested media-coding test assignment:

1. IMA ADPCM audio encode/decode.
2. Educational MJPEG-style video encode/decode.
3. SDL3 + OpenGL playback of decoded audio/video streams.

The code intentionally keeps the codec algorithms visible and readable instead of hiding them behind FFmpeg or platform codecs.

## Build

```bash
cmake -S . -B build
cmake --build build
```

Targets:

* `codec_tool` is always built.
* `codec_tool player ...` is enabled when CMake can find SDL3 and OpenGL development packages; otherwise the mode prints an explanatory error.

## Usage

```bash
./build/codec_tool adpcm input.wav decoded.wav
./build/codec_tool mjpeg frames_png decoded_frames
./build/codec_tool player input.wav frames_png 25
```

`codec_tool player` decodes the WAV through the IMA ADPCM roundtrip, decodes PNG frames through the simplified MJPEG roundtrip, plays PCM through SDL3, uploads frames into an OpenGL texture, and chooses the displayed frame from the audio/video clock time.

## IMA ADPCM assumptions

* Input WAV must be little-endian PCM16.
* Channels are encoded independently.
* The first sample of each channel is stored as the predictor.
* Subsequent samples are encoded as 4-bit IMA ADPCM nibbles with the standard step and index adaptation tables.
* The decoder reconstructs interleaved PCM16 from the encoded channel blocks.

## Simplified MJPEG assumptions

This is deliberately not a complete JPEG/JFIF writer. It demonstrates the algorithmic core that is relevant for the test assignment:

* RGB input is converted to YCbCr.
* Luma is kept at full resolution while Cb/Cr are stored with 4:2:0 chroma subsampling.
* Every component is processed in 8x8 blocks and level-shifted by 128.
* Forward DCT is applied to every block/component.
* Coefficients are quantized with one quality-dependent scalar.
* Quantized coefficients are written into a compact educational `SJP2` stream.
* The decoder performs inverse quantization, IDCT, chroma upsampling, and YCbCr-to-RGB conversion.

Deliberate simplifications:

* no JPEG marker syntax, JFIF/EXIF metadata, restart intervals, or progressive scans;
* no Huffman table generation and no entropy-coded scan segments;
* no zig-zag ordering or run-length coding;
* fixed 4:2:0 chroma subsampling rather than arbitrary JPEG sampling factors;
* no separate luminance/chrominance quantization matrices.

## PNG / stb

The image IO layer uses the requested `stb_image.h` and `stb_image_write.h` function names. In this container the upstream headers could not be downloaded from `https://github.com/planetack/stb_image`, so `third_party/stb` contains a tiny API-compatible facade that is enough for build-only checks. Replace those two files with the real upstream stb headers to enable actual PNG reading and writing without changing codec code.

## Player notes

`codec_tool player` is implemented with SDL3 for window/audio/timing and OpenGL for presenting decoded video frames. It requests an OpenGL 4.6 compatibility context so the test player can stay compact while still exercising the expected SDL3/OpenGL integration path. When a `glad::glad` CMake target is available, the player mode initializes glad after creating the SDL OpenGL context. A production player would replace the compact compatibility-profile quad with a shader/VBO/VAO renderer.
