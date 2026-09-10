cmake_minimum_required(VERSION 3.20)
project(miquidle LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(SYSTEMD IMPORTED_TARGET libsystemd)

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# Generate ext-idle-notify protocol client files
set(IDLE_NOTIFY_XML "${CMAKE_CURRENT_SOURCE_DIR}/protocols/ext-idle-notify-v1.xml")

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} client-header ${IDLE_NOTIFY_XML} ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-client-protocol.h
    DEPENDS ${IDLE_NOTIFY_XML}
)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-client-protocol.c
    COMMAND ${WAYLAND_SCANNER} private-code ${IDLE_NOTIFY_XML} ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-client-protocol.c
    DEPENDS ${IDLE_NOTIFY_XML}
)

set(SOURCES
    src/main.cpp
    src/config.cpp
    src/process.cpp
    src/idle_app.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-client-protocol.h
    ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-client-protocol.c
)

if (SYSTEMD_FOUND)
    list(APPEND SOURCES src/dbus_manager.cpp)
    add_compile_definitions(HAVE_SYSTEMD=1)
endif()

add_executable(miquidle ${SOURCES})

target_include_directories(miquidle PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(miquidle PRIVATE
    PkgConfig::WAYLAND_CLIENT
    Threads::Threads
)

if (SYSTEMD_FOUND)
    target_link_libraries(miquidle PRIVATE PkgConfig::SYSTEMD)
endif()

target_compile_options(miquidle PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

include(GNUInstallDirs)
install(TARGETS miquidle RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES assets/miquidle.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/miquidle)
