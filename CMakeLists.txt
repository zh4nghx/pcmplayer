cmake_minimum_required(VERSION 3.15)
project(pcmplay LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(pcmplay
  src/main.c
  src/audio.c
  src/source.c
  src/console.c
  src/util.c
  src/out.c
)

target_compile_features(pcmplay PRIVATE c_std_11)
target_compile_definitions(pcmplay PRIVATE _WIN32_WINNT=0x0601)

if(MSVC)
  target_compile_options(pcmplay PRIVATE /W4)
  target_compile_definitions(pcmplay PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(pcmplay PRIVATE -Wall -Wextra)
endif()

target_link_libraries(pcmplay PRIVATE ole32)
