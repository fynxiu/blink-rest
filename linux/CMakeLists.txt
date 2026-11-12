cmake_minimum_required(VERSION 3.25)
project(BlinkRestLinux VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The Linux desktop target must be built on Linux.")
endif()

set(BLINKREST_VERSION "0.1.0" CACHE STRING "Blink Rest semantic version")
if(NOT BLINKREST_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "BLINKREST_VERSION must use X.Y.Z semantic-version form")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Gui DBus)

add_library(blinkrest_core STATIC ../windows/src/core.cpp)
target_include_directories(blinkrest_core PUBLIC ../windows/include)
target_compile_options(blinkrest_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(blinkrest
    src/main.cpp
    src/host.cpp
    src/host.hpp
    src/overlay.cpp
    src/overlay.hpp
    src/settings.cpp
    src/settings.hpp
)
target_link_libraries(blinkrest PRIVATE blinkrest_core Qt6::Widgets Qt6::DBus)
target_compile_options(blinkrest PRIVATE -Wall -Wextra -Wpedantic -Werror)

enable_testing()
add_executable(blinkrest_core_tests ../windows/tests/core_tests.cpp)
target_link_libraries(blinkrest_core_tests PRIVATE blinkrest_core)
target_compile_options(blinkrest_core_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME core_contract COMMAND blinkrest_core_tests)

add_executable(blinkrest_linux_settings_tests
    tests/settings_tests.cpp
    src/settings.cpp
    src/settings.hpp
)
target_link_libraries(blinkrest_linux_settings_tests PRIVATE blinkrest_core Qt6::Core)
target_compile_options(blinkrest_linux_settings_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME linux_settings COMMAND blinkrest_linux_settings_tests)

add_test(NAME linux_gui_smoke COMMAND blinkrest --smoke)
set_tests_properties(linux_gui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)

include(GNUInstallDirs)
install(TARGETS blinkrest RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES packaging/blinkrest.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES ../Design/AppIcon.svg
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
    RENAME blinkrest.svg)

set(CPACK_PACKAGE_NAME "blinkrest")
set(CPACK_PACKAGE_VENDOR "Blink Rest")
set(CPACK_PACKAGE_CONTACT "fynxiu")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Periodic eye-break reminder for Linux desktops")
set(CPACK_PACKAGE_VERSION "${BLINKREST_VERSION}")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_GENERATOR "DEB")
include(CPack)
