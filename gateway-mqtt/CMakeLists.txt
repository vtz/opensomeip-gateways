# Copyright (c) 2025 Vinicius Tadeu Zein
# SPDX-License-Identifier: Apache-2.0

find_package(PahoMqttCpp CONFIG REQUIRED)

add_library(opensomeip-gateway-mqtt
    src/mqtt_gateway.cpp
    src/mqtt_translator.cpp
)

target_include_directories(opensomeip-gateway-mqtt
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(opensomeip-gateway-mqtt
    PUBLIC
        opensomeip-gateway-common
        PahoMqttCpp::paho-mqttpp3
)

if(BUILD_TESTS)
    add_executable(test_mqtt_gateway
        tests/test_mqtt_gateway.cpp
    )
    target_link_libraries(test_mqtt_gateway
        PRIVATE
            opensomeip-gateway-mqtt
            GTest::gtest_main
    )
    add_test(NAME MqttGatewayTests COMMAND test_mqtt_gateway)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
