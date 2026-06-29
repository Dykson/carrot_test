cmake_minimum_required(VERSION 3.20)
project(carrot_media_codec_test LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CARROT_BUILD_PLAYER "Build SDL3/OpenGL player support when dependencies are available" ON)
option(CARROT_BUILD_SELFTEST "Build codec smoke/self tests" OFF)

add_library(carrot_codecs
    src/ima_adpcm.cpp
    src/mjpeg.cpp
    src/wav.cpp
)
target_include_directories(carrot_codecs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)
target_compile_options(carrot_codecs PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(codec_tool src/main.cpp)
target_link_libraries(codec_tool PRIVATE carrot_codecs)

if(CARROT_BUILD_SELFTEST)
    add_executable(carrot_selftest src/selftest.cpp)
    target_link_libraries(carrot_selftest PRIVATE carrot_codecs)
endif()

if(CARROT_BUILD_PLAYER)
    set(SDL3_DIR "${CMAKE_SOURCE_DIR}/third_party/SDL3-3.4.10/cmake")
    find_package(SDL3 QUIET)
    find_path(CARROT_GLAD_INCLUDE_DIR glad/gl.h PATHS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad-4.6/include)
    if(SDL3_FOUND AND CARROT_GLAD_INCLUDE_DIR)
        target_sources(codec_tool PRIVATE src/player.cpp)
        target_link_libraries(codec_tool PRIVATE SDL3::SDL3)
        target_include_directories(codec_tool PRIVATE ${CARROT_GLAD_INCLUDE_DIR})
        target_compile_definitions(codec_tool PRIVATE CARROT_WITH_PLAYER=1)
    else()
        message(STATUS "SDL3 and/or glad were not found: codec_tool player mode will be disabled")
    endif()
endif()
