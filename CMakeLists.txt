cmake_minimum_required(VERSION 3.20)
project(carrot_media_codec_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 QUIET)
find_package(OpenGL QUIET)
find_package(glad QUIET)

add_library(carrot_codecs
    src/ima_adpcm.cpp
    src/mjpeg.cpp
    src/wav.cpp
    src/stb.cpp
)
target_include_directories(carrot_codecs PUBLIC include third_party/stb)
target_compile_options(carrot_codecs PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(codec_tool src/main.cpp)
target_link_libraries(codec_tool PRIVATE carrot_codecs)

if(SDL2_FOUND AND OpenGL_FOUND)
    add_executable(simple_player src/player.cpp)
    target_link_libraries(simple_player PRIVATE carrot_codecs SDL2::SDL2 OpenGL::GL)
    target_compile_definitions(simple_player PRIVATE CARROT_WITH_PLAYER=1)
    if(TARGET glad::glad)
        target_link_libraries(simple_player PRIVATE glad::glad)
        target_compile_definitions(simple_player PRIVATE CARROT_WITH_GLAD=1)
    endif()
else()
    message(STATUS "SDL2/OpenGL were not found: simple_player target is skipped")
endif()
