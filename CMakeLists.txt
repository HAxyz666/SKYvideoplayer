cmake_minimum_required(VERSION 3.20)
project(SKYvideoplayer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Gui)
qt_standard_project_setup(REQUIRES 6.5)

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

set(CORE_SOURCES
    src/core/MediaEngine.cpp
    src/core/AVSyncController.cpp
    src/core/AudioOutput.cpp
    src/core/VideoRenderItem.cpp
)

set(CORE_HEADERS
    src/core/MediaEngine.h
    src/core/AVSyncController.h
    src/core/AudioOutput.h
    src/core/VideoRenderItem.h
)

qt6_add_executable(SKYvideoplayer
    main.cpp
    ${CORE_SOURCES}
    ${CORE_HEADERS}
)

set_target_properties(SKYvideoplayer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

qt6_add_qml_module(SKYvideoplayer
    URI SKYvideoplayer
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

target_include_directories(SKYvideoplayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

target_link_libraries(SKYvideoplayer PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Gui
    PkgConfig::AVCODEC
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    PkgConfig::SWRESAMPLE
    PkgConfig::SDL2
)
