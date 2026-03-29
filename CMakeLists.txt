# Copyright (c) 2025 Vinicius Tadeu Zein
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PROJECT_VERSION_RAW)
string(STRIP "${PROJECT_VERSION_RAW}" PROJECT_VERSION)

project(opensomeip-gateways
    VERSION ${PROJECT_VERSION}
    LANGUAGES CXX
    DESCRIPTION "Protocol gateways for OpenSOME/IP"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Global options ───────────────────────────────────────────────────────────

option(BUILD_TESTS    "Build test suites"    ON)
option(BUILD_EXAMPLES "Build example programs" ON)

# Each gateway is independently toggleable
option(BUILD_GATEWAY_ICEORYX2 "Build SOME/IP <-> iceoryx2 gateway" OFF)
option(BUILD_GATEWAY_MQTT     "Build SOME/IP <-> MQTT gateway"     OFF)
option(BUILD_GATEWAY_GRPC     "Build SOME/IP <-> gRPC gateway"     OFF)
option(BUILD_GATEWAY_ROS2     "Build SOME/IP <-> ROS2 gateway"     OFF)
option(BUILD_GATEWAY_ZENOH    "Build SOME/IP <-> Zenoh gateway"    OFF)
option(BUILD_GATEWAY_DBUS     "Build SOME/IP <-> D-Bus gateway"    OFF)
option(BUILD_GATEWAY_DDS      "Build SOME/IP <-> DDS gateway"      OFF)

# ── opensomeip core dependency ───────────────────────────────────────────────

if(NOT TARGET opensomeip)
    find_package(opensomeip QUIET)
    if(NOT opensomeip_FOUND)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../some-ip/CMakeLists.txt")
            set(BUILD_TESTS_SAVED ${BUILD_TESTS})
            set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(SOMEIP_DEV_TOOLS OFF CACHE BOOL "" FORCE)
            add_subdirectory(
                "${CMAKE_CURRENT_SOURCE_DIR}/../some-ip"
                "${CMAKE_CURRENT_BINARY_DIR}/opensomeip"
                EXCLUDE_FROM_ALL
            )
            set(BUILD_TESTS ${BUILD_TESTS_SAVED} CACHE BOOL "" FORCE)
        else()
            message(FATAL_ERROR
                "opensomeip not found. Either:\n"
                "  1. Install opensomeip system-wide, or\n"
                "  2. Place the opensomeip source tree at ../some-ip\n"
            )
        endif()
    endif()
endif()

# ── Testing ──────────────────────────────────────────────────────────────────

if(BUILD_TESTS)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# ── Common gateway library (always built) ────────────────────────────────────

add_subdirectory(common)

# ── Gateways (conditionally built) ──────────────────────────────────────────

if(BUILD_GATEWAY_ICEORYX2)
    add_subdirectory(gateway-iceoryx2)
endif()

if(BUILD_GATEWAY_MQTT)
    add_subdirectory(gateway-mqtt)
endif()

if(BUILD_GATEWAY_GRPC)
    add_subdirectory(gateway-grpc)
endif()

if(BUILD_GATEWAY_ROS2)
    add_subdirectory(gateway-ros2)
endif()

if(BUILD_GATEWAY_ZENOH)
    add_subdirectory(gateway-zenoh)
endif()

if(BUILD_GATEWAY_DBUS)
    add_subdirectory(gateway-dbus)
endif()

if(BUILD_GATEWAY_DDS)
    add_subdirectory(gateway-dds)
endif()

# ── Summary ──────────────────────────────────────────────────────────────────

message(STATUS "")
message(STATUS "opensomeip-gateways v${PROJECT_VERSION}")
message(STATUS "  Tests:    ${BUILD_TESTS}")
message(STATUS "  Examples: ${BUILD_EXAMPLES}")
message(STATUS "  Gateways:")
message(STATUS "    iceoryx2: ${BUILD_GATEWAY_ICEORYX2}")
message(STATUS "    MQTT:     ${BUILD_GATEWAY_MQTT}")
message(STATUS "    gRPC:     ${BUILD_GATEWAY_GRPC}")
message(STATUS "    ROS2:     ${BUILD_GATEWAY_ROS2}")
message(STATUS "    Zenoh:    ${BUILD_GATEWAY_ZENOH}")
message(STATUS "    D-Bus:    ${BUILD_GATEWAY_DBUS}")
message(STATUS "    DDS:      ${BUILD_GATEWAY_DDS}")
message(STATUS "")
