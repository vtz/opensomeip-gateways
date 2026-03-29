# Copyright (c) 2025 Vinicius Tadeu Zein
# SPDX-License-Identifier: Apache-2.0

find_package(CycloneDDS REQUIRED CONFIG)

idlc_generate(
    TARGET opensomeip_gateway_dds_idl
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/idl/vehicle_data.idl"
)

add_library(opensomeip-gateway-dds
    src/dds_gateway.cpp
    src/dds_translator.cpp
)

target_include_directories(opensomeip-gateway-dds
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(opensomeip-gateway-dds
    PUBLIC
        opensomeip-gateway-common
    PRIVATE
        opensomeip_gateway_dds_idl
        CycloneDDS::ddsc
)

if(BUILD_TESTS)
    add_executable(test_dds_gateway
        tests/test_dds_gateway.cpp
    )
    target_link_libraries(test_dds_gateway
        PRIVATE
            opensomeip-gateway-dds
            GTest::gtest_main
    )
    add_test(NAME DdsGatewayTests COMMAND test_dds_gateway)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
