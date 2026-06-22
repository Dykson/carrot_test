cmake_minimum_required(VERSION 3.16)
project(carrot_ima_adpcm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ima_adpcm
    src/ImaAdpcm.cpp
    src/WavFile.cpp
)
target_include_directories(ima_adpcm PUBLIC include)

target_compile_options(ima_adpcm PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(ima_adpcm_tool src/main.cpp)
target_link_libraries(ima_adpcm_tool PRIVATE ima_adpcm)

add_executable(ima_adpcm_tests tests/ImaAdpcmTests.cpp)
target_link_libraries(ima_adpcm_tests PRIVATE ima_adpcm)

enable_testing()
add_test(NAME ima_adpcm_roundtrip COMMAND ima_adpcm_tests)
