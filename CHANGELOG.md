<!--
  Copyright (c) 2025 Vinicius Tadeu Zein

  See the NOTICE file(s) distributed with this work for additional
  information regarding copyright ownership.

  This program and the accompanying materials are made available under the
  terms of the Apache License Version 2.0 which is available at
  https://www.apache.org/licenses/LICENSE-2.0

  SPDX-License-Identifier: Apache-2.0
-->

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-03-28

### Added

- Repository initialization with build system, documentation, and CI
- Common gateway library with `IGateway` base class, configuration, and translation utilities
- SOME/IP ↔ iceoryx2 gateway (zero-copy local IPC bridge)
- SOME/IP ↔ MQTT gateway (vehicle-to-cloud bridge)
- SOME/IP ↔ gRPC gateway (service-oriented cloud integration)
- SOME/IP ↔ ROS2 gateway (autonomous driving / robotics bridge)
- SOME/IP ↔ Zenoh gateway (edge-to-cloud pub/sub bridge)
- SOME/IP ↔ D-Bus gateway (Linux automotive platform bridge)
- SOME/IP ↔ DDS gateway (AUTOSAR Adaptive / defense / aerospace bridge)
- Examples for each gateway demonstrating bidirectional message translation
- Unit tests for common library and each gateway
