<!--
  Copyright (c) 2025 Vinicius Tadeu Zein
  SPDX-License-Identifier: Apache-2.0
-->

# SOME/IP ↔ MQTT Gateway

Bidirectional bridge between SOME/IP (via [opensomeip](https://github.com/vtz/opensomeip)) and [MQTT](https://mqtt.org/) for vehicle-to-cloud telemetry, fleet management, remote diagnostics, and IoT integration.

## Architecture

```
┌──────────────┐                              ┌──────────────┐
│ Vehicle ECU  │  SOME/IP (UDP/TCP)           │  Cloud       │
│ (speed, GPS, │◄────────────────►┌──────────┐│  MQTT Broker │
│  door lock)  │                  │ Gateway  ││  (Mosquitto, │
└──────────────┘                  │ SOMEIP ↔ ││  AWS IoT,    │
                                  │ MQTT     ││  HiveMQ)     │
┌──────────────┐                  │          │└──────────────┘
│ Offline      │◄── ring buffer ──│          │
│ Buffer       │                  └──────────┘ ┌──────────────┐
└──────────────┘                               │ Fleet Mgmt   │
                                               │ Dashboard    │
                                               └──────────────┘
```

## Features

- **Pub/Sub bridge**: SOME/IP events → MQTT topics (and reverse)
- **Request-Response**: SOME/IP RPC ↔ MQTT v5 request-response pattern
- **TLS/mTLS**: Secure vehicle-to-cloud connections
- **QoS mapping**: Per-service/event QoS configuration (0/1/2)
- **Offline buffering**: Ring buffer for messages during connectivity loss
- **Reconnect**: Exponential backoff with configurable min/max delay
- **Last Will**: Automatic online/offline status via MQTT LWT
- **Payload encoding**: Raw binary, JSON, SOME/IP-framed
- **E2E protection**: Optional SOME/IP E2E validation before forwarding
- **VIN-based topics**: `vehicle/{vin}/someip/{service}/{instance}/...`

## MQTT Topic Structure

```
vehicle/{vin}/someip/{service_id}/{instance_id}/event/{event_group_id}
vehicle/{vin}/someip/{service_id}/{instance_id}/method/{method_id}/request
vehicle/{vin}/someip/{service_id}/{instance_id}/method/{method_id}/response
vehicle/{vin}/someip/sd/status           # LWT: online/offline
```

## QoS Mapping

| Data Type | SOME/IP Transport | MQTT QoS | Rationale |
|-----------|-------------------|----------|-----------|
| Telemetry (periodic) | UDP events | QoS 0 | Best-effort, high frequency |
| Control commands | RPC | QoS 1 | At-least-once delivery |
| Diagnostic / safety | TCP RPC | QoS 2 | Exactly-once, critical |

## opensomeip APIs Used

| API | Usage |
|-----|-------|
| `Message`, `Serializer`, `Deserializer` | Payload encoding/decoding |
| `EventPublisher` / `EventSubscriber` | SOME/IP event bridge |
| `RpcClient` / `RpcServer` | SOME/IP method call bridge |
| `SdClient` / `SdServer` | Service discovery |
| `UdpTransport` / `Endpoint` | Optional SOME/IP UDP listener |
| `E2EProtection` | Optional E2E validation |

## Build

```bash
# Install Paho MQTT C++ (optional - gateway works without it for testing)
sudo apt install libpaho-mqttpp-dev libpaho-mqtt-dev

cd opensomeip-gateways
mkdir build && cd build
cmake .. -DBUILD_GATEWAY_MQTT=ON
make -j$(nproc)
ctest --output-on-failure -R Mqtt
```

## Configuration

See [examples/mqtt_config.yaml](examples/mqtt_config.yaml) for the full reference.

## Example

```bash
./bin/mqtt_vehicle_telemetry
```

Demonstrates vehicle speed events published to MQTT, RPC bridging, and offline buffering.
