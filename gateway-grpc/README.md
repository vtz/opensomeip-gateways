<!--
  Copyright (c) 2025 Vinicius Tadeu Zein
  SPDX-License-Identifier: Apache-2.0
-->

# SOME/IP ↔ gRPC Gateway

Bidirectional bridge between SOME/IP (via [opensomeip](https://github.com/vtz/opensomeip)) and [gRPC](https://grpc.io/) for service-oriented cloud/backend integration.

## Architecture

The gateway operates in **dual-role** mode:

```
┌──────────────┐                              ┌──────────────┐
│ Vehicle ECU  │  SOME/IP                     │  Cloud gRPC  │
│ (radar, VCU) │◄────────────────►┌──────────┐│  Services    │
│              │                  │ Gateway  ││  (digital    │
└──────────────┘                  │ gRPC     ││   twin, OTA) │
                                  │ Server + ││              │
                                  │ Client   │└──────────────┘
                                  └──────────┘
```

**gRPC Server**: Exposes vehicle SOME/IP services to cloud clients
**gRPC Client**: Calls cloud gRPC services on behalf of the vehicle

## Features

- **Unary RPC bridge**: SOME/IP methods ↔ gRPC unary calls
- **Server-streaming**: SOME/IP events → gRPC server streams
- **Error code mapping**: SOME/IP ReturnCode ↔ gRPC StatusCode
- **TLS/mTLS**: Mutual TLS for both server and client sides
- **Generic protobuf wrapper**: Works without per-service `.proto` definitions
- **SD integration**: Service discovery proxy

## Error Code Mapping

| SOME/IP ReturnCode | gRPC StatusCode |
|---|---|
| E_OK | OK (0) |
| E_NOT_OK | INTERNAL (13) |
| E_UNKNOWN_SERVICE | NOT_FOUND (5) |
| E_UNKNOWN_METHOD | UNIMPLEMENTED (12) |
| E_NOT_READY | UNAVAILABLE (14) |
| E_MALFORMED_MESSAGE | INVALID_ARGUMENT (3) |

## Build

```bash
cd opensomeip-gateways
mkdir build && cd build
cmake .. -DBUILD_GATEWAY_GRPC=ON
make -j$(nproc)
```

## Example

```bash
./bin/grpc_digital_twin
```
