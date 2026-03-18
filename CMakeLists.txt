cmake_minimum_required(VERSION 3.20)
project(mp4togif LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC  REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(SWSCALE  REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(AVUTIL   REQUIRED IMPORTED_TARGET libavutil)

add_executable(mp4togif
    src/main.cpp
    src/util/bmp.cpp
    src/ffmpeg/demuxer.cpp
    src/ffmpeg/decoder.cpp
    src/ffmpeg/scaler.cpp
)

target_link_libraries(mp4togif PRIVATE
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::SWSCALE
    PkgConfig::AVUTIL
)

# GUI executable (WIN32 = subsystem:windows, no console)
add_executable(mp4togif_gui WIN32
    src/gui/gui.cpp
    src/util/bmp.cpp
    src/ffmpeg/demuxer.cpp
    src/ffmpeg/decoder.cpp
    src/ffmpeg/scaler.cpp
)

target_link_libraries(mp4togif_gui PRIVATE
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::SWSCALE
    PkgConfig::AVUTIL
    z
    gdi32
    comctl32
    comdlg32
)
