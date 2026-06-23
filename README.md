# Simplified MJPEG codec

This repository contains a small C++17 implementation of an educational MJPEG-like codec.

## Public API

- `carrot::MjpegEncoder` reads a folder of lexicographically sorted `.png` frames and writes an `SMJ1` movie file.
- `carrot::MjpegDecoder` reads an `SMJ1` movie file, reconstructs raster frames, and can write them back as PNG files.
- PNG reading and writing is delegated to `stb_image` / `stb_image_write`; video compression and decompression are implemented in this project.

## Compression pipeline

For every frame the encoder:

1. Converts RGB pixels to YCbCr.
2. Pads edge blocks by repeating the last pixel so every block is 8x8.
3. Runs a direct 8x8 DCT for Y, Cb, and Cr planes.
4. Quantizes coefficients with JPEG-style luminance/chrominance matrices scaled by the requested quality.
5. Reorders AC coefficients in zig-zag order.
6. Uses zero run-length coding plus a compact bit stream for signed coefficient magnitudes.
7. Stores every compressed still frame independently in a simple `SMJ1` container.

The decoder performs the inverse operations and produces RGB raster images.

## Intentional simplifications

This is not a standards-compliant JPEG or AVI/QuickTime MJPEG implementation. To keep the code compact and focused on codec fundamentals, it intentionally simplifies JPEG in these ways:

- No entropy Huffman table construction or arithmetic coding; coefficients use fixed-size fields plus RLE.
- No inter-frame prediction, motion compensation, or temporal compression; each frame is independent like MJPEG.
- No chroma subsampling; Y, Cb, and Cr are encoded at full resolution.
- No progressive scans, restart markers, EXIF/JFIF metadata, or standard JPEG marker layout.
- No SIMD or fast integer DCT; the implementation uses clear floating-point DCT/IDCT loops.
- A custom `SMJ1` container is used instead of AVI/MOV so the bit-stream structure remains easy to inspect.

## Build and test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
