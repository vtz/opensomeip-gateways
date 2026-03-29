# Copyright (c) 2025 Vinicius Tadeu Zein
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)


find_package(rclcpp QUIET CONFIG)
find_package(std_msgs QUIET CONFIG)

set(OPENSOMEIP_GATEWAY_ROS2_WITH_RCLCPP FALSE)
if(rclcpp_FOUND AND std_msgs_FOUND)
    set(OPENSOMEIP_GATEWAY_ROS2_WITH_RCLCPP TRUE)
endif()

add_library(opensomeip-gateway-ros2
    src/ros2_gateway.cpp
    src/ros2_translator.cpp
)

target_include_directories(opensomeip-gateway-ros2
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(opensomeip-gateway-ros2
    PUBLIC
        opensomeip-gateway-common
)

if(OPENSOMEIP_GATEWAY_ROS2_WITH_RCLCPP)
    target_compile_definitions(opensomeip-gateway-ros2
        PRIVATE
            OPENSOMEIP_GATEWAY_ROS2_HAS_RCLCPP=1
    )
    target_link_libraries(opensomeip-gateway-ros2
        PUBLIC
            rclcpp::rclcpp
            std_msgs::std_msgs__rosidl_generator_cpp
    )
endif()

if(BUILD_TESTS)
    add_executable(test_ros2_gateway
        tests/test_ros2_gateway.cpp
    )
    target_link_libraries(test_ros2_gateway
        PRIVATE
            opensomeip-gateway-ros2
            GTest::gtest_main
    )
    add_test(NAME Ros2GatewayTests COMMAND test_ros2_gateway)
endif()

if(BUILD_EXAMPLES AND OPENSOMEIP_GATEWAY_ROS2_WITH_RCLCPP)
    add_subdirectory(examples)
endif()
