cmake_minimum_required(VERSION 3.21)

project(VideoCutter
    VERSION 2.0.0
    LANGUAGES CXX
)

# Генерация compile_commands.json для работы автодополнения в .clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

# Автоматически настраивает AUTOMOC, AUTORCC и AUTOUIC (для .ui файлов)
qt_standard_project_setup()

# Флаг WIN32 убирает консольное окно при запуске приложения
qt_add_executable(VideoCutter WIN32
    src/main.cpp
    src/resource.rc

    # UI
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.ui
    src/ui/TimelineWidget.h
    src/ui/TimelineWidget.cpp

    # Video & FFmpeg
    src/video/VideoInfo.h
    src/video/VideoInfo.cpp
    src/video/VideoProcessor.h
    src/video/VideoProcessor.cpp
)

# Позволяет подключать заголовки относительно папки src/
target_include_directories(VideoCutter PRIVATE
    src
)

target_link_libraries(VideoCutter PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
)

# Автоматическое копирование необходимых DLL от Qt после сборки (windeployqt)
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${Qt6_DIR}/../../../bin"
        REQUIRED
    )

    add_custom_command(TARGET VideoCutter POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            "$<IF:$<CONFIG:Debug>,--debug,--release>"
            --dir "$<TARGET_FILE_DIR:VideoCutter>"
            "$<TARGET_FILE:VideoCutter>"
        COMMENT "Deploying Qt libraries..."
        VERBATIM
    )
endif()

# Копирование утилит FFmpeg в папку bin рядом с собранным VideoCutter.exe
add_custom_command(TARGET VideoCutter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:VideoCutter>/bin"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/bin/ffmpeg.exe"
        "$<TARGET_FILE_DIR:VideoCutter>/bin/ffmpeg.exe"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/bin/ffprobe.exe"
        "$<TARGET_FILE_DIR:VideoCutter>/bin/ffprobe.exe"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/bin/ffplay.exe"
        "$<TARGET_FILE_DIR:VideoCutter>/bin/ffplay.exe"

    COMMENT "Copying FFmpeg utilities..."
    VERBATIM
)
