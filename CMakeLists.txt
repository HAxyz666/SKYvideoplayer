cmake_minimum_required(VERSION 4.1.1)
project(SKYvideoplayer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Gui)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Svg ShaderTools)

qt_standard_project_setup(REQUIRES 6.5)

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
pkg_check_modules(AVFILTER REQUIRED IMPORTED_TARGET libavfilter)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

if(UNIX AND NOT APPLE)
    pkg_check_modules(LIBVA IMPORTED_TARGET libva)
    pkg_check_modules(LIBDRM IMPORTED_TARGET libdrm)
endif()

set(CORE_SOURCES
    src/core/MediaEngine.cpp
    src/core/AVSyncController.cpp
    src/core/AudioOutput.cpp
    src/core/VideoRenderItem.cpp
    src/core/PacketQueue.cpp
    src/core/FrameQueue.cpp
    src/core/DemuxThread.cpp
    src/core/VideoDecodeThread.cpp
    src/core/AudioDecodeThread.cpp
    src/core/SubtitleDecodeThread.cpp
)

set(CORE_HEADERS
    src/core/MediaEngine.h
    src/core/AVSyncController.h
    src/core/AudioOutput.h
    src/core/VideoRenderItem.h
    src/core/PacketQueue.h
    src/core/FrameQueue.h
    src/core/DemuxThread.h
    src/core/VideoDecodeThread.h
    src/core/AudioDecodeThread.h
    src/core/SubtitleDecodeThread.h
)

set(LOGIC_SOURCES
    src/logic/Applicationcontroller.cpp
    src/logic/PlaylistModel.cpp
    src/logic/RecentFilesModel.cpp
    src/logic/SettingsManager.cpp
)

set(LOGIC_HEADERS
    src/logic/Applicationcontroller.h
    src/logic/PlaylistItem.h
    src/logic/PlaylistModel.h
    src/logic/PlaybackMode.h
    src/logic/RecentFilesModel.h
    src/logic/SettingsManager.h
)

qt6_add_executable(SKYvideoplayer
    main.cpp
    ${CORE_SOURCES}
    ${CORE_HEADERS}
    ${LOGIC_SOURCES}
    ${LOGIC_HEADERS}
)

set_target_properties(SKYvideoplayer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

qt6_add_qml_module(SKYvideoplayer
    URI SKYvideoplayer
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/AppLayout.qml
        qml/AppSidebar.qml
        qml/PlayerController.qml
        qml/ProgressBar.qml
        qml/PlaylistPanel.qml
        qml/RecentHistory.qml
        qml/SubtitleSelector.qml
        qml/VolumeControl.qml
    SOURCES
        src/core/VideoRenderItem.cpp
)

qt6_add_resources(SKYvideoplayer "icons"
    BASE "resources"
    PREFIX "/icons"
    FILES
        resources/logos/LogoD.svg
        resources/logos/LogoL.svg
)

qt6_add_shaders(SKYvideoplayer "shaders"
    BASE "resources/shaders"
    PREFIX "/shaders"
    GLSL "440"
    NOHLSL
    NOMSL
    FILES
        resources/shaders/yuv.vert
        resources/shaders/yuv.frag
)

target_include_directories(SKYvideoplayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

get_target_property(_qt6gui_inc Qt6::Gui INTERFACE_INCLUDE_DIRECTORIES)
foreach(_incdir IN LISTS _qt6gui_inc)
    if(EXISTS "${_incdir}/QtGui/${Qt6_VERSION}/QtGui/rhi")
        target_include_directories(SKYvideoplayer PRIVATE
            "${_incdir}/QtGui/${Qt6_VERSION}/QtGui/rhi"
            "${_incdir}/QtGui/${Qt6_VERSION}/QtGui"
        )
        break()
    endif()
endforeach()

target_include_directories(SKYvideoplayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logic
)
target_link_libraries(SKYvideoplayer PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Gui
    Qt6::Svg
    PkgConfig::AVCODEC
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    PkgConfig::SWRESAMPLE
    PkgConfig::AVFILTER
    PkgConfig::SDL2
)

if(UNIX AND NOT APPLE AND LIBVA_FOUND AND LIBDRM_FOUND)
    target_link_libraries(SKYvideoplayer PRIVATE
        PkgConfig::LIBVA
        PkgConfig::LIBDRM
    )
    target_compile_definitions(SKYvideoplayer PRIVATE ENABLE_HWACCEL)
endif()
