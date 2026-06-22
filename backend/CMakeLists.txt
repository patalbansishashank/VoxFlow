cmake_minimum_required(VERSION 3.16)
project(voxflow-backend CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)

set(VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor")

if(NOT EXISTS "${VENDOR_DIR}/miniaudio.h")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h"
        "${VENDOR_DIR}/miniaudio.h"
        STATUS download_status)
    list(GET download_status 0 status_code)
    if(NOT status_code EQUAL 0)
        message(FATAL_ERROR "Failed to download miniaudio.h")
    endif()
endif()

if(NOT EXISTS "${VENDOR_DIR}/json.hpp")
    file(MAKE_DIRECTORY "${VENDOR_DIR}/nlohmann")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp"
        "${VENDOR_DIR}/nlohmann/json.hpp"
        STATUS download_status)
    list(GET download_status 0 status_code)
    if(NOT status_code EQUAL 0)
        message(FATAL_ERROR "Failed to download json.hpp")
    endif()
endif()

set(SOURCES
    src/main.cpp
    src/audio.cpp
    src/api.cpp
    src/clipboard.cpp
    src/json_rpc.cpp
    src/config.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${VENDOR_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    CURL::libcurl
    ${WAYLAND_CLIENT_LIBRARIES}
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wpedantic
)

install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
