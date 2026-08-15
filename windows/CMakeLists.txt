cmake_minimum_required(VERSION 3.25)

project(BlinkRestWindows LANGUAGES CXX)

set(BLINKREST_VERSION "0.0.0" CACHE STRING "Blink Rest semantic version")
if(NOT BLINKREST_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "BLINKREST_VERSION must use X.Y.Z semantic-version form")
endif()
string(REPLACE "." ";" BLINKREST_VERSION_PARTS "${BLINKREST_VERSION}")
list(GET BLINKREST_VERSION_PARTS 0 BLINKREST_VERSION_MAJOR)
list(GET BLINKREST_VERSION_PARTS 1 BLINKREST_VERSION_MINOR)
list(GET BLINKREST_VERSION_PARTS 2 BLINKREST_VERSION_PATCH)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(blinkrest_core
    src/core.cpp
)

target_include_directories(blinkrest_core
    PUBLIC
        include
)

if(MSVC)
    target_compile_options(blinkrest_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(blinkrest_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()

if(WIN32)
    configure_file(
        resources/version.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/blinkrest_version.rc
        @ONLY
    )
    add_executable(blinkrest_win32 WIN32
        resources/blinkrest.rc
        ${CMAKE_CURRENT_BINARY_DIR}/blinkrest_version.rc
        src/win32/main.cpp
        src/win32/overlay.cpp
        src/win32/settings.cpp
        src/win32/startup.cpp
        src/win32/tray_popover.cpp
        src/win32/warning.cpp
    )
    target_link_libraries(blinkrest_win32 PRIVATE
        blinkrest_core
        advapi32
        d2d1
        dwrite
        ole32
        shell32
        user32
        wtsapi32
    )
    target_include_directories(blinkrest_win32 PRIVATE
        resources
    )
    target_compile_definitions(blinkrest_win32 PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(MSVC)
        target_compile_options(blinkrest_win32 PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()

add_executable(blinkrest_core_tests
    tests/core_tests.cpp
)

target_link_libraries(blinkrest_core_tests PRIVATE blinkrest_core)

if(MSVC)
    target_compile_options(blinkrest_core_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(blinkrest_core_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME blinkrest_core_tests COMMAND blinkrest_core_tests)
