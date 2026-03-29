# Copyright (c) 2025 Vinicius Tadeu Zein
# SPDX-License-Identifier: Apache-2.0

add_library(opensomeip-gateway-common
    src/gateway_base.cpp
    src/config.cpp
    src/translator.cpp
)

target_include_directories(opensomeip-gateway-common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(opensomeip-gateway-common
    PUBLIC
        opensomeip
)

if(BUILD_TESTS)
    add_executable(test_gateway_common
        tests/test_gateway_base.cpp
        tests/test_config.cpp
        tests/test_translator.cpp
    )
    target_link_libraries(test_gateway_common
        PRIVATE
            opensomeip-gateway-common
            GTest::gtest_main
    )
    add_test(NAME GatewayCommonTests COMMAND test_gateway_common)
endif()
