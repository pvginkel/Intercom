cmake_minimum_required(VERSION 3.16)

message(STATUS "CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(PROJECT_VER "${GIT_COMMIT_HASH}")

# Hardware version. Build a specific board revision with:
#   idf.py -DHARDWARE_VERSION=2 build
# Defaults to the current board when not supplied (e.g. local `idf.py build`).
if(NOT DEFINED HARDWARE_VERSION)
    set(HARDWARE_VERSION 2)
endif()
message(STATUS "Hardware version set to: ${HARDWARE_VERSION}")
add_compile_definitions(HARDWARE_VERSION=${HARDWARE_VERSION})

set(
    SDKCONFIG_DEFAULTS
    ../esp-libs/esp-mdm/sdkconfig.defaults
    ../esp-libs/esp-network-support/sdkconfig.defaults
    ../esp-libs/esp-mqtt-support/sdkconfig.defaults
    ../esp-libs/esp-light-algorithms/sdkconfig.defaults
    sdkconfig.defaults
)

message(STATUS "Firmware version set to: ${PROJECT_VER}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(intercom_v${HARDWARE_VERSION})
