# Copyright (c) 2025 Vinicius Tadeu Zein
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

add_executable(ros2_adas_bridge ros2_adas_bridge.cpp)
target_link_libraries(ros2_adas_bridge
    PRIVATE
        opensomeip-gateway-ros2
)
