cmake_minimum_required(VERSION 4.1.1)
project(SKYvideoplayer VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Gui Concurrent Qml Svg ShaderTools)

qt_standard_project_setup(REQUIRES 6.5)

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

find_library(LIBSONIC_LIBRARIES sonic REQUIRED)
find_path(LIBSONIC_INCLUDE_DIRS sonic.h)

if(UNIX AND NOT APPLE)
    pkg_check_modules(LIBVA IMPORTED_TARGET libva)
    pkg_check_modules(LIBDRM IMPORTED_TARGET libdrm)
endif()

set(CORE_SOURCES
    src/core/MediaEngine.cpp
    src/core/AVSyncController.cpp
    src/core/AudioOutput.cpp
    src/core/PacketQueue.cpp
    src/core/FrameQueue.cpp
    src/core/DemuxThread.cpp
    src/core/VideoDecodeThread.cpp
    src/core/AudioDecodeThread.cpp
    src/core/SubtitleDecodeThread.cpp
    src/core/NetworkStreamManager.cpp
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
    src/core/NetworkStreamManager.h
)

set(LOGIC_SOURCES
    src/logic/Applicationcontroller.cpp
    src/logic/PlaybackHistory.cpp
    src/logic/PlaylistModel.cpp
    src/logic/RecentFilesModel.cpp
    src/logic/SettingsManager.cpp
)

set(LOGIC_HEADERS
    src/logic/Applicationcontroller.h
    src/logic/PlaybackHistory.h
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

# 翻译：.ts → .qm 自动编译
find_program(LRELEASE_EXECUTABLE lrelease)
if(LRELEASE_EXECUTABLE)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/translations/zh_CN.qm"
        COMMAND ${LRELEASE_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/translations/zh_CN.ts"
            -qm "${CMAKE_CURRENT_SOURCE_DIR}/translations/zh_CN.qm"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/translations/zh_CN.ts"
        COMMENT "Compiling zh_CN.ts → zh_CN.qm"
    )
endif()

qt6_add_resources(SKYvideoplayer "translations"
    BASE "translations"
    PREFIX "/i18n"
    FILES translations/zh_CN.qm
)

set_source_files_properties(qml/TimeUtils.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt6_add_qml_module(SKYvideoplayer
    URI SKYvideoplayer
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/AppLayout.qml
        qml/AppSidebar.qml
        qml/ControlBar.qml
        qml/PlayerController.qml
        qml/PlayerView.qml
        qml/ProgressBar.qml
        qml/PlaylistPanel.qml
        qml/RecentHistory.qml
        qml/SubtitleSelector.qml
        qml/AudioSelector.qml
        qml/VolumeControl.qml
        qml/NetworkDialog.qml
        qml/SettingsDialog.qml
        qml/SubtitleStyleDialog.qml
        qml/SubtitleOverlay.qml
        qml/LoadingOverlay.qml
        qml/ResumeToast.qml
        qml/SpeedPopup.qml
        qml/TimeUtils.qml
        qml/VideoAdjustPopup.qml
    SOURCES
        src/core/VideoRenderItem.cpp
)

qt6_add_resources(SKYvideoplayer "icons"
    BASE "resources"
    PREFIX "/icons"
    FILES
        resources/logos/LogoD.svg
        resources/logos/LogoD.png
        resources/logos/LogoL.svg
        resources/icons/exit_fullscreen.svg
        resources/icons/play_list.svg
        resources/icons/fullscreen.svg
        resources/icons/exit_play.svg
        resources/icons/pause.svg
        resources/icons/play.svg
        resources/icons/prev_file.svg
        resources/icons/next_file.svg
        resources/icons/video_adjust.svg
        resources/icons/screenshot.svg
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logic
    ${LIBSONIC_INCLUDE_DIRS}
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

target_link_libraries(SKYvideoplayer PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Gui
    Qt6::Concurrent
    Qt6::Svg
    PkgConfig::AVCODEC
    PkgConfig::AVFORMAT
    PkgConfig::AVUTIL
    PkgConfig::SWRESAMPLE
    PkgConfig::SWSCALE
    PkgConfig::SDL2
    ${LIBSONIC_LIBRARIES}
)

if(UNIX AND NOT APPLE AND LIBVA_FOUND AND LIBDRM_FOUND)
    target_link_libraries(SKYvideoplayer PRIVATE
        PkgConfig::LIBVA
        PkgConfig::LIBDRM
    )
    target_compile_definitions(SKYvideoplayer PRIVATE ENABLE_HWACCEL)
endif()
