<!--
  Copyright (c) 2025 Vinicius Tadeu Zein
  SPDX-License-Identifier: Apache-2.0
-->

# Build Guide

## Prerequisites

- C++17-compatible compiler (GCC 9+, Clang 10+)
- CMake 3.20+
- opensomeip source tree (sibling directory `../some-ip` or installed system-wide)

## Building the Common Library Only

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
ctest --output-on-failure
```

## Building Individual Gateways

Each gateway has its own CMake option and external dependencies:

| Gateway | CMake Option | Dependencies |
|---------|-------------|--------------|
| iceoryx2 | `BUILD_GATEWAY_ICEORYX2` | iceoryx2 C++ bindings |
| MQTT | `BUILD_GATEWAY_MQTT` | Eclipse Paho MQTT C++ |
| gRPC | `BUILD_GATEWAY_GRPC` | gRPC C++, Protobuf |
| ROS2 | `BUILD_GATEWAY_ROS2` | ROS2 (rclcpp, std_msgs) |
| Zenoh | `BUILD_GATEWAY_ZENOH` | zenoh-c or zenoh-cpp |
| D-Bus | `BUILD_GATEWAY_DBUS` | sd-bus (systemd) or GDBus |
| DDS | `BUILD_GATEWAY_DDS` | Eclipse Cyclone DDS |

Example for MQTT:

```bash
# Install Paho MQTT C++
sudo apt install libpaho-mqttpp-dev libpaho-mqtt-dev

# Build
mkdir build && cd build
cmake .. -DBUILD_GATEWAY_MQTT=ON
make -j$(nproc)
```

## Using CMake Presets

```bash
cmake --preset mqtt
cmake --build --preset mqtt
```

## opensomeip Discovery

The build system looks for opensomeip in this order:

1. `find_package(opensomeip)` — system-installed
2. `../some-ip/CMakeLists.txt` — sibling directory (development setup)

For development, clone both repos side by side:

```bash
git clone https://github.com/vtz/opensomeip.git some-ip
git clone https://github.com/vtz/opensomeip-gateways.git
cd opensomeip-gateways
mkdir build && cd build
cmake ..
```
