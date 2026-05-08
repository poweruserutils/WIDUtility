cmake_minimum_required(VERSION 3.20)
project(WIDUtility LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "WID Utility is Windows-only.")
endif()

add_executable(wid WIN32
    src/main.cpp
    src/app.manifest

    src/gui/MainWindow.cpp

    src/util/Log.cpp
    src/util/Process.cpp
    src/util/Paths.cpp

    src/core/ChangeQueue.cpp
    src/core/Iso.cpp
    src/core/Wim.cpp
    src/core/Hive.cpp
    src/core/Components.cpp
    src/core/Apps.cpp
    src/core/Tweaks.cpp
    src/core/Commands.cpp
    src/core/Unattend.cpp
    src/core/Pipeline.cpp
)

target_include_directories(wid PRIVATE src)

target_compile_definitions(wid PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(wid PRIVATE /W4 /permissive- /utf-8)
    # Match the level declared in src/app.manifest so mt.exe can merge them.
    target_link_options(wid PRIVATE
        "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()

target_link_libraries(wid PRIVATE
    comctl32
    user32
    gdi32
    shell32
    ole32
    uxtheme
)

set_target_properties(wid PROPERTIES
    OUTPUT_NAME "WIDUtility"
    VS_DPI_AWARE "PerMonitor"
)
