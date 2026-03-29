<!--
  Copyright (c) 2025 Vinicius Tadeu Zein
  SPDX-License-Identifier: Apache-2.0
-->

# SOME/IP ↔ Zenoh Gateway

Bidirectional bridge between SOME/IP (via [opensomeip](https://github.com/vtz/opensomeip)) and [Eclipse Zenoh](https://zenoh.io/) for edge-to-cloud pub/sub and location-transparent communication.

## Architecture

```
┌──────────────┐   SOME/IP   ┌───────────┐   Zenoh    ┌──────────┐
│  ECU A       │◄───────────►│  Gateway   │◄──────────►│  Edge    │
│  (SOMEIP)    │             │  SOMEIP ↔  │            │  Compute │
└──────────────┘             │  Zenoh     │            │  (Zenoh) │
                             └─────┬─────┘            └─────┬────┘
                                   │ Zenoh                   │
                             ┌─────▼─────┐            ┌─────▼────┐
                             │  Zenoh    │◄──────────►│  Cloud   │
                             │  Router   │   Zenoh    │  Backend │
                             └───────────┘            └──────────┘
```

## Features

- **Pub/Sub bridge**: SOME/IP events ↔ Zenoh publications (key expressions)
- **Request-Response bridge**: SOME/IP RPC ↔ Zenoh queryables
- **SD proxy**: SOME/IP-SD ↔ Zenoh liveliness tokens
- **Session modes**: Peer, client, router
- **Shared-memory**: Optional SHM transport for local high-performance paths
- **Payload encoding**: Raw, JSON, CBOR
- **E2E validation**: Optional SOME/IP E2E check before forwarding

## Key Expression Mapping

```
{key_prefix}/{service_id}/{instance_id}/event/{event_group_id}
{key_prefix}/{service_id}/{instance_id}/method/{method_id}
{key_prefix}/liveliness/{service_id}/{instance_id}
```

## Build

```bash
cd opensomeip-gateways
mkdir build && cd build
cmake .. -DBUILD_GATEWAY_ZENOH=ON
make -j$(nproc)
```

## Example

```bash
./bin/zenoh_edge_bridge
```
