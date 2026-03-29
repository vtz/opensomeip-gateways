<!--
  Copyright (c) 2025 Vinicius Tadeu Zein
  SPDX-License-Identifier: Apache-2.0
-->

# Coding Guidelines

These guidelines are aligned with the [opensomeip](https://github.com/vtz/opensomeip) core project.

## Language Standard

- **C++17** — all gateway code must compile with `-std=c++17`
- No compiler-specific extensions unless behind a platform macro

## Naming Conventions

| Entity | Convention | Example |
|--------|-----------|---------|
| Namespace | `lower_case` | `opensomeip::gateway::mqtt` |
| Class | `CamelCase` | `MqttGateway` |
| Function | `snake_case` | `translate_message()` |
| Variable | `snake_case` | `message_count` |
| Constant | `UPPER_CASE` | `MAX_BUFFER_SIZE` |
| Enum | `CamelCase` | `TranslationMode` |
| Enum value | `UPPER_CASE` | `OPAQUE`, `TYPED` |
| File | `snake_case` | `mqtt_gateway.h` |
| Member variable | `snake_case_` (trailing underscore) | `client_id_` |

## File Organization

```
gateway-<name>/
├── CMakeLists.txt
├── README.md
├── include/opensomeip/gateway/<name>/
│   ├── <name>_gateway.h        # Main gateway class
│   ├── <name>_config.h         # Configuration types
│   └── <name>_translator.h     # Message translation
├── src/
│   ├── <name>_gateway.cpp
│   ├── <name>_config.cpp
│   └── <name>_translator.cpp
├── tests/
│   └── test_<name>_gateway.cpp
└── examples/
    ├── CMakeLists.txt
    ├── <name>_example.cpp
    └── <name>_config.yaml
```

## Code Style

- Enforced by `.clang-format` (Google-based, 4-space indent, 100-column limit)
- Braces: Stroustrup style
- Include order: system headers, then project headers (sorted within groups)

## Error Handling

- Use `someip::Result` for operations that can fail
- Never throw exceptions from gateway public APIs
- Log errors with context (service ID, message ID, etc.)

## Thread Safety

- Gateway state transitions protected by internal mutex
- Statistics updates use atomic operations or mutex
- Document thread safety in class-level comments

## Copyright Header

Every source file must include the Apache 2.0 copyright header:

```cpp
/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/
```
