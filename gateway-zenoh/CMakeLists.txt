# Copyright (c) 2025 Vinicius Tadeu Zein
# SPDX-License-Identifier: Apache-2.0

find_package(zenohc CONFIG QUIET)
if(NOT zenohc_FOUND)
    find_package(zenohc REQUIRED CONFIG PATHS
        "$ENV{ZENOH_HOME}/lib/cmake/zenohc"
        /usr/lib/cmake/zenohc
        /usr/local/lib/cmake/zenohc
    )
endif()

if(NOT DEFINED ZENOH_CPP_INCLUDE_DIR)
    find_path(ZENOH_CPP_INCLUDE_DIR NAMES zenoh.hxx
        PATHS
            "${zenohc_INCLUDE_DIRS}"
            /usr/include
            /usr/local/include
    )
endif()
if(NOT ZENOH_CPP_INCLUDE_DIR)
    message(FATAL_ERROR "zenoh.hxx not found; set ZENOH_CPP_INCLUDE_DIR")
endif()

add_library(opensomeip-gateway-zenoh
    src/zenoh_gateway.cpp
    src/zenoh_translator.cpp
)

target_include_directories(opensomeip-gateway-zenoh
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${ZENOH_CPP_INCLUDE_DIR}
)

set(_zenoh_link_targets)
if(TARGET zenohc::lib)
    list(APPEND _zenoh_link_targets zenohc::lib)
elseif(TARGET zenohc::shared)
    list(APPEND _zenoh_link_targets zenohc::shared)
else()
    message(FATAL_ERROR "zenohc package has no zenohc::lib / zenohc::shared target")
endif()

target_link_libraries(opensomeip-gateway-zenoh
    PUBLIC
        opensomeip-gateway-common
    PRIVATE
        ${_zenoh_link_targets}
)

if(BUILD_TESTS)
    add_executable(test_zenoh_gateway
        tests/test_zenoh_gateway.cpp
        src/zenoh_translator.cpp
    )
    target_include_directories(test_zenoh_gateway PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(test_zenoh_gateway
        PRIVATE
            opensomeip-gateway-common
            GTest::gtest_main
    )
    add_test(NAME ZenohGatewayTests COMMAND test_zenoh_gateway)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
