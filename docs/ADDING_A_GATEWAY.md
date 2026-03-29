<!--
  Copyright (c) 2025 Vinicius Tadeu Zein
  SPDX-License-Identifier: Apache-2.0
-->

# Adding a New Gateway

This guide walks through creating a new protocol gateway for OpenSOME/IP.

## Step 1: Create the Directory Structure

```bash
mkdir -p gateway-<name>/include/opensomeip/gateway/<name>
mkdir -p gateway-<name>/src
mkdir -p gateway-<name>/tests
mkdir -p gateway-<name>/examples
```

## Step 2: Define the Gateway Class

Create `gateway-<name>/include/opensomeip/gateway/<name>/<name>_gateway.h`:

```cpp
#include "opensomeip/gateway/gateway_base.h"

namespace opensomeip::gateway::<name> {

class <Name>Gateway : public IGateway {
public:
    explicit <Name>Gateway(const <Name>Config& config);
    ~<Name>Gateway() override;

    Result start() override;
    Result stop() override;
    bool is_running() const override;
    Result on_someip_message(const someip::Message& msg) override;
    std::string get_name() const override;
    std::string get_protocol() const override;
    GatewayStats get_stats() const override;

private:
    // SOME/IP side (opensomeip objects)
    // External protocol side (protocol-specific client/server)
    // Translation layer
};

}
```

## Step 3: Implement Message Translation

Create a translator that converts between SOME/IP messages and the external protocol's native format. The translator should support at least:

- **Opaque mode**: Pass raw payload bytes without interpretation
- **Typed mode**: Deserialize SOME/IP payload and re-encode in the target format

## Step 4: Add CMake Build

Create `gateway-<name>/CMakeLists.txt`:

```cmake
find_package(<ExternalLib> REQUIRED)

add_library(opensomeip-gateway-<name>
    src/<name>_gateway.cpp
    src/<name>_translator.cpp
)
target_include_directories(opensomeip-gateway-<name> PUBLIC include)
target_link_libraries(opensomeip-gateway-<name>
    PUBLIC opensomeip-gateway-common
    PRIVATE <ExternalLib>::<Target>
)
```

Add the CMake option and `add_subdirectory` call in the root `CMakeLists.txt`.

## Step 5: Write Tests

At minimum, test:

1. Configuration parsing
2. Message translation (SOME/IP → external and external → SOME/IP)
3. Gateway lifecycle (start/stop)
4. Error handling (malformed messages, connection failures)

## Step 6: Create Examples

Provide at least one example demonstrating bidirectional communication. Include a YAML configuration file showing all available options.

## Step 7: Write Documentation

Create `gateway-<name>/README.md` covering:

- What the gateway does and why
- Prerequisites and dependencies
- Build instructions
- Configuration reference
- Architecture diagram
- Example usage

## Checklist

- [ ] Gateway class extends `IGateway`
- [ ] Independent CMake build (no cross-gateway dependencies)
- [ ] Unit tests for translation logic
- [ ] At least one working example
- [ ] README with configuration reference
- [ ] YAML example configuration
- [ ] Added to root CMakeLists.txt and CMakePresets.json
- [ ] Added to root README gateway table
