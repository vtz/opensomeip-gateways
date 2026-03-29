<!--
  Copyright (c) 2025 Vinicius Tadeu Zein
  SPDX-License-Identifier: Apache-2.0
-->

# SOME/IP ↔ iceoryx2 Gateway

Bidirectional bridge between SOME/IP (via [opensomeip](https://github.com/vtz/opensomeip)) and [Eclipse iceoryx2](https://github.com/eclipse-iceoryx/iceoryx2) zero-copy shared-memory IPC.

## Architecture

```
┌──────────────┐                              ┌──────────────┐
│ SOME/IP ECU  │  Ethernet / UDP              │ Local App    │
│ (remote)     │◄────────────────►┌──────────┐│ (iceoryx2    │
│              │                  │ Gateway  ││  subscriber) │
└──────────────┘                  │ SOMEIP ↔ │└──────────────┘
                                  │ iceoryx2 │
┌──────────────┐                  │          │┌──────────────┐
│ SD Client    │◄─── SD proxy ───►│          ││ Local App    │
│ (discovers   │                  │          ││ (iceoryx2    │
│  services)   │                  └──────────┘│  publisher)  │
└──────────────┘                              └──────────────┘
```

## Features

- **Pub/Sub bridge**: SOME/IP events ↔ iceoryx2 publisher/subscriber
- **Request-Response bridge**: SOME/IP RPC methods ↔ iceoryx2 client/server
- **SD proxy**: SOME/IP Service Discovery offers bridged to iceoryx2 service names
- **In-process simulation**: Works without iceoryx2 installed (for testing/demos)
- **Configurable per-service mappings**: YAML or programmatic

## opensomeip APIs Used

| API | Usage |
|-----|-------|
| `Message`, `MessageId`, `RequestId` | Message creation and parsing |
| `Serializer` / `Deserializer` | Payload encoding for envelope format |
| `EventPublisher` / `EventSubscriber` | SOME/IP event pub/sub bridge |
| `RpcClient` / `RpcServer` | SOME/IP method call bridge |
| `SdClient` / `SdServer` | Service discovery proxy |
| `UdpTransport` / `Endpoint` | Optional UDP listener for raw SOME/IP frames |

## Build

```bash
cd opensomeip-gateways
mkdir build && cd build
cmake .. -DBUILD_GATEWAY_ICEORYX2=ON
make -j$(nproc)
ctest --output-on-failure -R Iceoryx2
```

## Configuration

See [examples/iceoryx2_config.yaml](examples/iceoryx2_config.yaml) for the full reference.

## Example

```bash
./bin/iceoryx2_bridge_example
```

Demonstrates SOME/IP notifications flowing to iceoryx2, RPC requests bridging, and incoming iceoryx2 samples injected back as SOME/IP messages.
