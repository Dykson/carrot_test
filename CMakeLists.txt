cmake_minimum_required(VERSION 3.20)
project(carrot_mjpeg_codec LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ZLIB REQUIRED)

add_library(carrot_mjpeg
    src/BitStream.cpp
    src/Image.cpp
    src/MjpegCodec.cpp
    src/PngIo.cpp
)
target_include_directories(carrot_mjpeg PUBLIC include third_party/stb)
target_link_libraries(carrot_mjpeg PUBLIC ZLIB::ZLIB)

enable_testing()
add_executable(mjpeg_tests tests/MjpegCodecTests.cpp)
target_link_libraries(mjpeg_tests PRIVATE carrot_mjpeg)
add_test(NAME mjpeg_tests COMMAND mjpeg_tests)
