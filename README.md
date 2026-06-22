# IMA ADPCM test task

C++17 implementation of a small IMA ADPCM encoder/decoder for PCM16 WAV input.
The codec logic is implemented in `ImaAdpcmEncoder` and `ImaAdpcmDecoder` without using media codec libraries.

## Build and test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Usage

```bash
./build/ima_adpcm_tool input.wav decoded.wav
```

The tool reads a 16-bit PCM WAV file, encodes each channel into IMA ADPCM blocks, decodes the stream back to PCM, and writes the reconstructed WAV file.
