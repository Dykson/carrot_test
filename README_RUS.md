# Carrot Broadcast media codec test

Учебный проект медиакодеков на C++17 для тестового задания, ориентированного на live broadcasting. Логика кодеков находится внутри репозитория: аудио кодируется рукописной реализацией IMA ADPCM, а видео использует небольшой MJPEG-подобный intra-frame кодек. Проект **не** вызывает FFmpeg/libavcodec, libjpeg/libjpeg-turbo или библиотечный ADPCM-кодек.

## Что реализовано

* Чтение/запись PCM16 WAV для command-line round trip сценариев.
* Кодирование/декодирование IMA ADPCM с независимыми блоками для каждого канала.
* Учебный MJPEG-подобный RGB video path:
  * преобразование RGB -> YCbCr;
  * chroma subsampling 4:2:0;
  * 8x8 DCT/IDCT;
  * отдельные скалярные значения квантования для luma/chroma;
  * zig-zag порядок коэффициентов перед RLE;
  * компактные локальные для проекта payload-кадры `SJR3` внутри stream writer формата `CMJ2`.
* PNG input/output через `stb_image.h` и `stb_image_write.h` только для image I/O.
* Опциональный SDL3/OpenGL player, если доступны зависимости.
* Опциональные smoke-тесты `carrot_selftest` для edge cases кодеков.

## Сборка

```bash
cmake -S . -B build
cmake --build build --config Release
```

Опции CMake:

```bash
cmake -S . -B build -DCARROT_BUILD_PLAYER=OFF
cmake -S . -B build -DCARROT_BUILD_SELFTEST=ON
```

Targets:

* `codec_tool` собирается всегда.
* `carrot_selftest` собирается при `CARROT_BUILD_SELFTEST=ON` (по умолчанию выключено).
* `codec_tool player ...` компилируется только когда `CARROT_BUILD_PLAYER=ON` и CMake находит SDL3 плюс include path с `glad/gl.h`. Жёстко заданных абсолютных путей к SDL нет.

## Использование

Audio ADPCM round trip:

```bash
./build/codec_tool adpcm input.wav decoded.wav
```

Video MJPEG-like round trip по папке с PNG-кадрами:

```bash
./build/codec_tool mjpeg frames_png decoded_frames 85
```

Список PNG-кадров сначала собирается и сортируется лексикографически перед кодированием, поэтому имена вроде `frame_0001.png`, `frame_0002.png` и `frame_0010.png` воспроизводятся в ожидаемом порядке.

Player mode, если SDL3/OpenGL/glad были доступны на этапе сборки:

```bash
./build/codec_tool player input.wav frames_png 25
```

Self tests:

```bash
./build/carrot_selftest
```

Command-line ADPCM и MJPEG round trip режимы записывают демонстрационные промежуточные streams в `media/out`:

* `media/out/audio.cadp` для ADPCM channel blocks;
* `media/out/video.mjpeg` для локального для проекта MJPEG-like frame stream.

## Заметки по IMA ADPCM

* Входной WAV должен быть little-endian PCM16.
* Каналы кодируются независимо.
* Каждый channel block хранит initial predictor и точное количество PCM samples для этого канала.
* Decoder использует сохранённое количество samples, чтобы декодировать только валидные nibbles, поэтому нечётное количество nibbles не создаёт лишний padding sample.
* Empty, 1-sample, 2-sample, 3-sample, odd-length mono и stereo layouts покрыты size checks в `carrot_selftest`.

## Заметки по MJPEG-like формату

Это учебный intra-frame stream, а не полноценный JPEG/JFIF/MJPEG file format. Текущая magic-сигнатура frame payload — `SJR3`; старые payloads `SJR2` отклоняются, чтобы не декодировать их с неправильным порядком коэффициентов.

Validation, выполняемая decoder, включает:

* валидные magic/header;
* ненулевые luma/chroma quantization scales;
* положительные размеры frame и component dimensions;
* bounds checks при чтении RLE bitstream;
* отклонение unexpected trailing bytes после декодирования всех ожидаемых component blocks.

Осознанные упрощения, которые остаются:

* нет Huffman coding;
* нет JPEG marker syntax, JFIF/EXIF metadata или restart intervals;
* нет progressive JPEG scans;
* фиксированный chroma sampling 4:2:0;
* scalar luma/chroma quantization values вместо полных JPEG quantization matrices.

## PNG / stb

Репозиторий содержит реальные заголовки `stb_image.h` и `stb_image_write.h` в `third_party/stb`. Они используются только для чтения и записи PNG-файлов; сам MJPEG-like кодек реализован вручную в `src/mjpeg.cpp`.

## Заметки по player

Player декодирует аудио через IMA ADPCM path, а кадры — через MJPEG-like path, затем использует SDL3 для events/audio и OpenGL для показа texture. Выбор video frame строго основан на elapsed time из steady clock и заданном fps, а audio clock используется только для startup alignment и diagnostics. Audio callback непрерывно зацикливает PCM buffer без вставки silence между loops. Waveform discontinuity в точке loop всё ещё может давать click, если сам WAV не подготовлен для бесшовного loop.
