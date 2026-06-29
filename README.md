# Carrot Broadcast media codec test

C++17 educational media-codec project for a live-broadcasting-oriented test assignment. It keeps the codec logic in the repository: audio is encoded with a hand-written IMA ADPCM implementation and video uses a small MJPEG-like intra-frame codec. The project does **not** call FFmpeg/libavcodec, libjpeg/libjpeg-turbo, or a library ADPCM codec.

## What is implemented

* PCM16 WAV read/write for command-line round trips.
* IMA ADPCM encode/decode with independent per-channel blocks.
* A teaching MJPEG-like RGB video path:
  * RGB -> YCbCr conversion;
  * 4:2:0 chroma subsampling;
  * 8x8 DCT/IDCT;
  * separate luma/chroma scalar quantization values;
  * zig-zag coefficient ordering before RLE;
  * compact project-local `SJR3` frame payloads inside a `CMJ2` stream writer.
* PNG input/output through `stb_image.h` and `stb_image_write.h` only for image I/O.
* Optional SDL3/OpenGL player when dependencies are available.
* Optional `carrot_selftest` smoke tests for codec edge cases.

## Build

```bash
cmake -S . -B build
cmake --build build --config Release
```

CMake options:

```bash
cmake -S . -B build -DCARROT_BUILD_PLAYER=OFF
cmake -S . -B build -DCARROT_BUILD_SELFTEST=ON
```

Targets:

* `codec_tool` is always built.
* `carrot_selftest` is built when `CARROT_BUILD_SELFTEST=ON` (disabled by default).
* `codec_tool player ...` is compiled only when `CARROT_BUILD_PLAYER=ON` and CMake finds SDL3 plus a `glad/gl.h` include path. There are no hard-coded absolute SDL paths.

## Usage

Audio ADPCM round trip:

```bash
./build/codec_tool adpcm input.wav decoded.wav
```

Video MJPEG-like round trip over a folder of PNG frames:

```bash
./build/codec_tool mjpeg frames_png decoded_frames 85
```

The PNG frame list is collected first and sorted lexicographically before encoding, so names such as `frame_0001.png`, `frame_0002.png`, and `frame_0010.png` play in the expected order.

Player mode, if SDL3/OpenGL/glad were available at build time:

```bash
./build/codec_tool player input.wav frames_png 25
```

Self tests:

```bash
./build/carrot_selftest
```

The command-line ADPCM and MJPEG round trips write demonstration intermediate streams under `media/out`:

* `media/out/audio.cadp` for the ADPCM channel blocks;
* `media/out/video.mjpeg` for the project-local MJPEG-like frame stream.

## IMA ADPCM notes

* Input WAV must be little-endian PCM16.
* Channels are encoded independently.
* Each channel block stores the initial predictor and the exact PCM sample count for that channel.
* The decoder uses the stored sample count to decode only valid nibbles, so odd nibble counts do not produce an extra padding sample.
* Empty, 1-sample, 2-sample, 3-sample, odd-length mono, and stereo layouts are covered by `carrot_selftest` size checks.

## MJPEG-like format notes

This is an educational intra-frame stream, not a full JPEG/JFIF/MJPEG file format. The current frame payload magic is `SJR3`; older `SJR2` payloads are rejected instead of being decoded with the wrong coefficient order.

Validation performed by the decoder includes:

* valid magic/header;
* non-zero luma/chroma quantization scales;
* positive frame and component dimensions;
* bounds checks while reading the RLE bitstream;
* rejection of unexpected trailing bytes after all expected component blocks are decoded.

Deliberate simplifications that remain:

* no Huffman coding;
* no JPEG marker syntax, JFIF/EXIF metadata, or restart intervals;
* no progressive JPEG scans;
* fixed 4:2:0 chroma sampling;
* scalar luma/chroma quantization values instead of full JPEG quantization matrices.

## PNG / stb

The repository contains real `stb_image.h` and `stb_image_write.h` headers under `third_party/stb`. They are used only to read and write PNG files; the MJPEG-like codec itself is implemented manually in `src/mjpeg.cpp`.

## Player notes

The player decodes audio through the IMA ADPCM path and frames through the MJPEG-like path, then uses SDL3 for events/audio and OpenGL for texture presentation. Video frame selection is driven strictly by steady-clock elapsed time and the configured fps, while the audio clock is used only for startup alignment and diagnostics. The audio callback continuously wraps the PCM buffer without inserting silence between loops. A waveform discontinuity at the loop point can still click if the WAV itself is not loop-friendly.
