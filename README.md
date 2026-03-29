<!--
  Copyright (c) 2025 Vinicius Tadeu Zein

  SPDX-License-Identifier: Apache-2.0
-->

# OpenSOME/IP Gateways

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![CMake](https://img.shields.io/badge/CMake-3.20+-blue.svg)](https://cmake.org/)

Protocol gateways for [OpenSOME/IP](https://github.com/vtz/opensomeip) — bidirectional bridges between SOME/IP and widely-used communication middlewares for automotive, IoT, robotics, and cloud applications.

> **Keywords**: SOME/IP, gateway, bridge, protocol translation, automotive middleware, iceoryx2, MQTT, gRPC, ROS2, Zenoh, D-Bus, DDS, vehicle-to-cloud, IPC, SOA

## Overview

Modern vehicle architectures need to bridge in-vehicle SOME/IP networks with cloud services, local high-performance IPC, robotics stacks, and IoT infrastructure. **OpenSOME/IP Gateways** provides standardized, open-source protocol bridges that keep the [core opensomeip library](https://github.com/vtz/opensomeip) lean while offering first-class integration with external ecosystems.

```
                           ┌─────────────────┐
                           │   opensomeip    │
                           │   (core stack)  │
                           └───────┬─────────┘
                                   │
                    ┌──────────────┼──────────────┐
                    │              │              │
              ┌─────▼─────┐  ┌─────▼─────┐  ┌─────▼─────┐
              │  iceoryx2 │  │   MQTT    │  │   gRPC    │  ...
              │  gateway  │  │  gateway  │  │  gateway  │
              └─────┬─────┘  └─────┬─────┘  └─────┬─────┘
                    │              │              │
              ┌─────▼─────┐  ┌─────▼─────┐  ┌─────▼─────┐
              │ Zero-copy │  │  Cloud    │  │ Backend   │
              │ local IPC │  │  Broker   │  │ Services  │
              └───────────┘  └───────────┘  └───────────┘
```

### Design Principles

- **Independent gateways** — each gateway builds and deploys independently; no cross-gateway dependencies
- **Dependency isolation** — external dependencies (Paho, gRPC, iceoryx2, etc.) stay within their gateway
- **Common patterns** — shared `IGateway` base class, YAML configuration, consistent lifecycle
- **Bidirectional** — every gateway supports both SOME/IP → external and external → SOME/IP directions
- **Production-oriented** — TLS, reconnect, buffering, metrics where applicable

## Available Gateways

| Gateway | Protocol | Status | Use Case |
|---------|----------|--------|----------|
| [gateway-iceoryx2](gateway-iceoryx2/) | [Eclipse iceoryx2](https://github.com/eclipse-iceoryx/iceoryx2) | 🟡 Initial | Zero-copy local IPC for intra-ECU communication |
| [gateway-mqtt](gateway-mqtt/) | [MQTT 5.0/3.1.1](https://mqtt.org/) | 🟡 Initial | Vehicle-to-cloud telemetry, fleet management, IoT |
| [gateway-grpc](gateway-grpc/) | [gRPC](https://grpc.io/) | 🟡 Initial | Service-oriented cloud/backend integration |
| [gateway-ros2](gateway-ros2/) | [ROS2](https://docs.ros.org/) | 🟡 Initial | Autonomous driving, robotics, ADAS |
| [gateway-zenoh](gateway-zenoh/) | [Eclipse Zenoh](https://zenoh.io/) | 🟡 Initial | Edge-to-cloud pub/sub, location-transparent comms |
| [gateway-dbus](gateway-dbus/) | [D-Bus](https://www.freedesktop.org/wiki/Software/dbus/) | 🟡 Initial | Linux automotive platforms (AGL), system services |
| [gateway-dds](gateway-dds/) | [OMG DDS](https://www.omg.org/dds/) | 🟡 Initial | AUTOSAR Adaptive, defense, aerospace |

## Quick Start

### Prerequisites

- C++17-compatible compiler (GCC 9+, Clang 10+)
- CMake 3.20+
- [opensomeip](https://github.com/vtz/opensomeip) source tree at `../some-ip` (sibling directory) or installed system-wide
- Gateway-specific dependencies (see each gateway's README)

### Build a Specific Gateway

```bash
git clone https://github.com/vtz/opensomeip-gateways.git
cd opensomeip-gateways

# Build with the MQTT gateway enabled
mkdir build && cd build
cmake .. -DBUILD_GATEWAY_MQTT=ON
make -j$(nproc)

# Run tests
ctest --output-on-failure
```

### Using CMake Presets

```bash
# Build a specific gateway
cmake --preset mqtt
cmake --build --preset mqtt

# Build all gateways (requires all dependencies)
cmake --preset all-gateways
cmake --build --preset all-gateways
```

### Integration in Your Project

```cmake
# In your CMakeLists.txt
add_subdirectory(vendor/opensomeip-gateways)
target_link_libraries(your_app PRIVATE opensomeip-gateway-mqtt)
```

## Architecture

Every gateway follows the same architectural pattern:

```
┌────────────────────────────────────────────────────────┐
│                   Gateway Process                      │
│                                                        │
│  ┌─────────────────┐         ┌──────────────────────┐  │
│  │  SOME/IP Side   │         │  External Protocol   │  │
│  │  (opensomeip)   │         │  Side                │  │
│  │  - Transport    │         │  - Protocol client   │  │
│  │  - SD           │◄───────►│  - Pub/sub or RPC    │  │
│  │  - RPC          │ Trans-  │  - Discovery         │  │
│  │  - Events       │ lation  │  - Security          │  │
│  └─────────────────┘ Layer   └──────────────────────┘  │
│                        ▲                               │
│                        │                               │
│                ┌───────┴───────┐                       │
│                │ Configuration │                       │
│                │ (YAML)        │                       │
│                └───────────────┘                       │
└────────────────────────────────────────────────────────┘
```

See [docs/architecture/gateway-architecture.md](docs/architecture/gateway-architecture.md) for the detailed design.

## Project Structure

```
opensomeip-gateways/
├── CMakeLists.txt              # Root build configuration
├── CMakePresets.json            # Build presets per gateway
├── README.md                    # This file
├── LICENSE                      # Apache 2.0
├── CONTRIBUTING.md              # Contribution guidelines
├── CHANGELOG.md                 # Version history
├── VERSION                      # Semantic version (0.1.0)
├── common/                      # Shared gateway utilities
│   ├── include/opensomeip/gateway/
│   │   ├── gateway_base.h       # IGateway abstract base
│   │   ├── config.h             # YAML configuration utilities
│   │   └── translator.h         # Payload translation helpers
│   └── src/
├── gateway-iceoryx2/            # SOME/IP ↔ iceoryx2
├── gateway-mqtt/                # SOME/IP ↔ MQTT
├── gateway-grpc/                # SOME/IP ↔ gRPC
├── gateway-ros2/                # SOME/IP ↔ ROS2
├── gateway-zenoh/               # SOME/IP ↔ Zenoh
├── gateway-dbus/                # SOME/IP ↔ D-Bus
├── gateway-dds/                 # SOME/IP ↔ DDS
├── docs/                        # Documentation
│   ├── architecture/
│   ├── BUILD.md
│   ├── CODING_GUIDELINES.md
│   └── ADDING_A_GATEWAY.md
├── examples/                    # Cross-gateway examples
└── scripts/                     # Development scripts
```

## Documentation

- [Build Guide](docs/BUILD.md) — detailed build instructions for each platform and gateway
- [Architecture](docs/architecture/gateway-architecture.md) — gateway design patterns and common infrastructure
- [Adding a Gateway](docs/ADDING_A_GATEWAY.md) — step-by-step guide for contributors
- [Coding Guidelines](docs/CODING_GUIDELINES.md) — C++17 coding standards
- [Contributing](CONTRIBUTING.md) — development workflow and PR process

## Related Projects

- [opensomeip](https://github.com/vtz/opensomeip) — Core SOME/IP protocol stack (C++17)
- [Eclipse iceoryx2](https://github.com/eclipse-iceoryx/iceoryx2) — Zero-copy IPC
- [Eclipse Zenoh](https://zenoh.io/) — Edge-to-cloud pub/sub
- [Eclipse Paho](https://www.eclipse.org/paho/) — MQTT client libraries
- [gRPC](https://grpc.io/) — High-performance RPC
- [ROS2](https://docs.ros.org/) — Robot Operating System
- [Eclipse Cyclone DDS](https://github.com/eclipse-cyclonedds/cyclonedds) — DDS implementation

## License

OpenSOME/IP Gateways is licensed under the Apache License 2.0 — see [LICENSE](LICENSE) for details.

## Support & Community

- **Core stack**: [github.com/vtz/opensomeip](https://github.com/vtz/opensomeip)
- **Issues**: Bug reports and feature requests on [GitHub Issues](https://github.com/vtz/opensomeip-gateways/issues)
- **Discussions**: Technical questions on GitHub Discussions

---

*OpenSOME/IP Gateways — Bridging automotive SOME/IP with the world.*
