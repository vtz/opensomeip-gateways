# Copyright (c) 2025 Vinicius Tadeu Zein
# SPDX-License-Identifier: Apache-2.0

add_executable(dbus_system_bridge dbus_system_bridge.cpp)
target_link_libraries(dbus_system_bridge PRIVATE opensomeip-gateway-dbus)
