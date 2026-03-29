<!--
  Copyright (c) 2025 Vinicius Tadeu Zein
  SPDX-License-Identifier: Apache-2.0
-->

# Gateway Architecture

## Overview

Every gateway in this repository follows the same architectural pattern, ensuring consistency and making it straightforward to add new gateways or contribute to existing ones.

## Core Abstraction: `IGateway`

All gateways implement the `IGateway` interface from the common library:

```cpp
namespace opensomeip::gateway {

class IGateway {
public:
    virtual Result start() = 0;
    virtual Result stop() = 0;
    virtual bool is_running() const = 0;

    virtual Result on_someip_message(const someip::Message& msg) = 0;
    virtual std::string get_name() const = 0;
    virtual std::string get_protocol() const = 0;
    virtual GatewayStats get_stats() const = 0;
};

}
```

## Message Flow

### SOME/IP → External Protocol

```
opensomeip Transport                     External Protocol
      │                                        ▲
      ▼                                        │
  ITransportListener::on_message_received()    │
      │                                        │
      ▼                                        │
  Gateway::on_someip_message()                 │
      │                                        │
      ▼                                        │
  MessageTranslator::translate_to_external()   │
      │                                        │
      └────────────────────────────────────────┘
```

### External Protocol → SOME/IP

```
External Protocol                        opensomeip Transport
      │                                        ▲
      ▼                                        │
  Protocol-specific callback/listener          │
      │                                        │
      ▼                                        │
  MessageTranslator::translate_to_someip()     │
      │                                        │
      ▼                                        │
  ITransport::send_message()                   │
      │                                        │
      └────────────────────────────────────────┘
```

## Configuration Model

Every gateway uses YAML configuration with a common structure:

```yaml
gateway:
  name: "descriptive-gateway-name"
  log_level: info  # debug, info, warn, error

  someip:
    local_address: "0.0.0.0"
    local_port: 30500
    sd_multicast: "239.255.255.250"
    sd_port: 30490

  # Protocol-specific section
  <protocol>:
    # ... protocol-specific settings ...

  service_mappings:
    - someip:
        service_id: 0x1234
        instance_id: 0x0001
      <protocol>:
        # ... protocol-specific mapping ...
      direction: bidirectional  # someip_to_<protocol>, <protocol>_to_someip
```

## Service Discovery Bridging

Each gateway bridges SOME/IP Service Discovery with the external protocol's native discovery mechanism:

| Protocol | Discovery Mechanism | Bridge Strategy |
|----------|-------------------|-----------------|
| iceoryx2 | iceoryx2 discovery | Liveliness-based |
| MQTT | Retained messages + LWT | Birth/death topics |
| gRPC | Health checks + reflection | Health service |
| ROS2 | DDS discovery | Node graph |
| Zenoh | Liveliness tokens | Token lifecycle |
| D-Bus | Name ownership | Bus name acquire/release |
| DDS | SPDP/SEDP | Participant discovery |

## Threading Model

Each gateway runs three categories of threads:

1. **SOME/IP thread**: Handles opensomeip transport receive loop
2. **Protocol thread(s)**: Handles external protocol I/O (event loop, callbacks, etc.)
3. **Translation**: Can be inline (in either thread) or use a dedicated queue

Thread safety is ensured by the gateway base class's internal mutex for state transitions and statistics.

## Error Handling

Gateways follow opensomeip's `Result` enum pattern for error reporting. Translation errors are logged and counted in statistics but do not crash the gateway — the principle is "best-effort forwarding with observability."

## Metrics

Every gateway exposes `GatewayStats`:

```cpp
struct GatewayStats {
    uint64_t messages_someip_to_external{0};
    uint64_t messages_external_to_someip{0};
    uint64_t translation_errors{0};
    uint64_t bytes_someip_to_external{0};
    uint64_t bytes_external_to_someip{0};
    std::chrono::steady_clock::time_point started_at;
    std::chrono::milliseconds uptime() const;
};
```
