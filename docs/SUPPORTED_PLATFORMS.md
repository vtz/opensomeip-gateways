# Supported Platforms & Architectures

OpenSOME/IP Gateways targets Linux-class host systems where external protocol SDKs are available. Unlike the core `opensomeip` stack (which runs on bare-metal RTOS targets), gateways require POSIX networking and depend on userspace libraries such as Paho MQTT, gRPC, iceoryx2, etc.

## Platform Matrix

| Platform | Architecture | Compiler | CI Tested | Notes |
|----------|-------------|----------|-----------|-------|
| Ubuntu (latest) | x86_64 | GCC | :white_check_mark: | Primary CI platform |
| Ubuntu (latest) | x86_64 | Clang | :white_check_mark: | |
| Fedora 42 | x86_64 | GCC | :white_check_mark: | Container-based CI |
| Fedora 42 | x86_64 | Clang | :white_check_mark: | |
| macOS (latest) | arm64 (Apple Silicon) | AppleClang | :white_check_mark: | CI + local dev verified |

## CI Pipeline

The CI runs on every push to `main` and on pull requests:

| Job | Platform | Compilers | What It Does |
|-----|----------|-----------|--------------|
| Build | Ubuntu | GCC, Clang | Build + test all gateways (common, iceoryx2, MQTT, gRPC, ROS2, D-Bus) |
| Build (Fedora) | Fedora 42 | GCC, Clang | Fedora container build + test |
| macOS | macOS | AppleClang | macOS native build + test |
| Coverage | Ubuntu | GCC | gcovr code coverage report |
| Sanitizers | Ubuntu | GCC, Clang | AddressSanitizer + UndefinedBehaviorSanitizer |
| Test Results | — | — | Aggregate JUnit XML and publish check results |

### Gateway SDK Dependencies

CI installs all required SDKs from system packages and builds every gateway
with the real libraries linked.

| Gateway | External SDK | CI Package (Ubuntu) | CI Package (Fedora) |
|---------|-------------|---------------------|---------------------|
| common | None | — | — |
| iceoryx2 | iceoryx2-cxx | Not packaged (inprocess sim) | Not packaged (inprocess sim) |
| MQTT | Paho MQTT C++ | `libpaho-mqttpp-dev` | `paho-mqtt-cpp-devel` |
| gRPC | gRPC + Protobuf | `libgrpc++-dev` | `grpc-devel` |
| ROS2 | rclcpp + std_msgs | Not packaged (callback mode) | Not packaged (callback mode) |
| D-Bus | libsystemd | `libsystemd-dev` | `systemd-devel` |
| Zenoh | zenohc | Not packaged | Not packaged |
| DDS | CycloneDDS | `cyclonedds-dev` | `cyclonedds-devel` |

**Notes:**

- **iceoryx2**: Uses inprocess shared-memory simulation for testing. The iceoryx2-cxx
  library is a Rust-based project without distro packages; full runtime testing requires
  building it from source.
- **ROS2**: Uses callback-based publish/subscribe without rclcpp. Full ROS2 runtime
  testing requires a complete ROS2 installation.
- **Zenoh**: The zenohc library is not available in distro package managers. A dedicated
  CI job with manual SDK installation is planned.

## Compiler Requirements

| Compiler | Minimum Version | C++ Standard |
|----------|----------------|--------------|
| GCC | 9.0 | C++17 |
| Clang | 10.0 | C++17 |
| AppleClang | 12.0 | C++17 |

## Architecture Notes

- **Endianness**: The gateways inherit SOME/IP big-endian wire format handling from the core opensomeip library. All listed platforms are little-endian; byte swapping is handled transparently.
- **Threading**: Gateways use `std::thread`, `std::mutex`, and `std::atomic` from C++17. POSIX threads are required.
- **Networking**: BSD sockets are used for the SOME/IP UDP transport. External protocol SDKs (MQTT, gRPC, etc.) handle their own networking.
- **Cross-compilation**: Not currently supported for gateways. The external SDKs would also need to be cross-compiled for the target platform.
