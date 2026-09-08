cmake_minimum_required(VERSION 3.25)

project(VALInvite VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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

add_executable(VALInviteOffline
    tools/offline_recognizer.cpp
    src/recognizer.cpp
)

add_executable(VALInviteRegression
    tools/recognizer_regression.cpp
    src/recognizer.cpp
)

target_include_directories(VALInvite PRIVATE include "${CMAKE_SOURCE_DIR}")
target_include_directories(VALInviteOffline PRIVATE include "${CMAKE_SOURCE_DIR}")
target_include_directories(VALInviteRegression PRIVATE include "${CMAKE_SOURCE_DIR}")
target_compile_features(VALInvite PRIVATE cxx_std_20)
target_compile_features(VALInviteOffline PRIVATE cxx_std_20)
target_compile_features(VALInviteRegression PRIVATE cxx_std_20)
target_compile_definitions(VALInvite PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_compile_definitions(VALInviteOffline PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_compile_definitions(VALInviteRegression PRIVATE
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
    target_compile_options(VALInviteOffline PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
    target_compile_options(VALInviteRegression PRIVATE /W4 /WX /permissive- /EHsc /utf-8)
else()
    target_compile_options(VALInvite PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(VALInviteOffline PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(VALInviteRegression PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
