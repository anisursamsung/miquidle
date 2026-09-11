cmake_minimum_required(VERSION 3.20)
project(miquidle LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;$ORIGIN/../lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(MIQUTOOLKIT REQUIRED IMPORTED_TARGET miqutoolkit)
pkg_check_modules(SYSTEMD IMPORTED_TARGET libsystemd)

set(SOURCES
    src/main.cpp
    src/config.cpp
    src/process.cpp
    src/idle_app.cpp
)

if (SYSTEMD_FOUND)
    list(APPEND SOURCES src/dbus_manager.cpp)
    add_compile_definitions(HAVE_SYSTEMD=1)
endif()

add_executable(miquidle ${SOURCES})

target_include_directories(miquidle PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(miquidle PRIVATE
    PkgConfig::MIQUTOOLKIT
    Threads::Threads
)

if (SYSTEMD_FOUND)
    target_link_libraries(miquidle PRIVATE PkgConfig::SYSTEMD)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../miqutoolkit/build/libmiqutoolkit.so")
    target_link_libraries(miquidle PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../miqutoolkit/build/libmiqutoolkit.so")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../miqutoolkit/include")
    target_include_directories(miquidle BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../miqutoolkit/include")
endif()

target_compile_options(miquidle PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

include(GNUInstallDirs)
install(TARGETS miquidle RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES assets/miquidle.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/miquidle)
