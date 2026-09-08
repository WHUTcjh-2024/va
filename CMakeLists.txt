cmake_minimum_required(VERSION 3.25)

project(VALInvite VERSION 0.1.0 LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "VALInvite is a Windows-only application.")
endif()

add_executable(VALInvite WIN32
    src/main.cpp
    src/app.cpp
    src/capture.cpp
    src/calibrate.cpp
    src/recognizer.cpp
    src/input.cpp
    src/ui.cpp
    src/timer.cpp
    src/config.cpp
)

target_include_directories(VALInvite PRIVATE include)
target_compile_features(VALInvite PRIVATE cxx_std_20)
target_compile_definitions(VALInvite PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(VALInvite PRIVATE
    d3d11
    dxgi
    windowsapp
    user32
    gdi32
    shell32
)

if (MSVC)
    target_compile_options(VALInvite PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
else()
    target_compile_options(VALInvite PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
