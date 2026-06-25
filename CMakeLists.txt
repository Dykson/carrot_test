cmake_minimum_required(VERSION 3.20)
project(carrot_media_codec_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SDL3_DIR
    "${CMAKE_SOURCE_DIR}/third_party/SDL3-3.4.10/cmake")
include_directories("third_party/glad-4.6/include")
find_package(SDL3 QUIET)

add_library(carrot_codecs
    src/ima_adpcm.cpp
    src/mjpeg.cpp
    src/wav.cpp
)
target_include_directories(carrot_codecs PUBLIC include third_party/stb)
target_compile_options(carrot_codecs PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(codec_tool src/main.cpp)
target_link_libraries(codec_tool PRIVATE carrot_codecs)

if(SDL3_FOUND)
    target_sources(codec_tool PRIVATE src/player.cpp)
    target_link_libraries(codec_tool PRIVATE SDL3::SDL3)
    target_compile_definitions(codec_tool PRIVATE CARROT_WITH_PLAYER=1)
else()
    message(STATUS "SDL3/OpenGL/glad were not found: codec_tool player mode will be disabled")
endif()
