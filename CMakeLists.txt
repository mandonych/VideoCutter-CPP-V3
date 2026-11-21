cmake_minimum_required(VERSION 3.25)

project(VideoCutter
    VERSION 1.1
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(VideoCutter WIN32
    src/main.cpp
    src/VideoInfo.cpp
    src/VideoProcessor.cpp
    src/UI.cpp
    src/Theme.cpp
    src/App.cpp
    src/resource.rc
)

target_link_libraries(VideoCutter PRIVATE
    comdlg32
    gdiplus
    comctl32
    dwmapi
    shell32
)

target_compile_definitions(VideoCutter PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(VideoCutter PRIVATE
        /W4
        /utf-8
        /permissive-
    )
    target_link_options(VideoCutter PRIVATE
        "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/app.manifest"
    )
endif()



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
        "${CMAKE_SOURCE_DIR}/VideoCutter_icon_256.png"
        "$<TARGET_FILE_DIR:VideoCutter>/VideoCutter_icon_256.png"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/README.md"
        "$<TARGET_FILE_DIR:VideoCutter>/README.md"
)
