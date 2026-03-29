# SOME/IP ↔ DDS gateway

Bridges **AUTOSAR Classic** SOME/IP traffic to **DDS** (Cyclone DDS by default) so services can be consumed from **AUTOSAR Adaptive**-style applications or cloud analytics that already speak DDS.

## Architecture

- **Common layer**: `GatewayBase`, `ServiceMapping`, `GatewayDirection`, `TranslationMode`, `MessageTranslator`, and `GatewayStats` from `opensomeip-gateway-common`.
- **This gateway**: `DdsGateway` extends `GatewayBase`, owns opensomeip building blocks (`EventPublisher`, `EventSubscriber`, `RpcClient`, `RpcServer`, `SdClient`, `SdServer`, `UdpTransport`, `E2EProtection`) and forwards payloads through Cyclone DDS **DataWriters** / **DataReaders**.
- **Pimpl**: DDS entities and threads live in `DdsGatewayImpl` inside `dds_gateway.cpp`, so including `dds_gateway.h` does **not** pull in DDS headers.
- **IDL**: `idl/vehicle_data.idl` defines sample vehicle types plus `vehicle::SomeipBridgeSample` (service/instance/method ids + opaque payload) used as the wire type for gateway topics.

```
Classic ECU — SOME/IP —+--> UdpTransport / events / RPC —> DdsGateway::on_someip_message
                       |                                      |
                       |                                      v
                       |                              Cyclone DDS Writer
                       |                                      |
Adaptive / cloud <-----+-------------------------------- Cyclone DDS Reader
                       (inject_dds_sample / DataReader thread -> sink)
```

## SOME/IP transport semantics → DDS QoS (summary)

| SOME/IP pattern | Typical mapping | DDS hints (`DdsQosProfile`) |
|-----------------|-----------------|----------------------------|
| Fire-and-forget event / UDP multicast-style notification | Best-effort delivery | `BestEffort`, `Volatile`, shallow history |
| RPC request/response, TCP SOME/IP, or SD | Ordered delivery expected | `Reliable`, `Volatile`, deeper history |
| Service mapping: event-group-only bridge | Many notifications, loss tolerable | Writer: `BestEffort` |
| Service mapping: method IDs present | Request/response style | Writer: `Reliable`, `TransientLocal` (configurable) |
| Inbound from DDS | Adaptive writes commands/results | Reader: `Reliable`, `Volatile` |

Exact QoS is applied in `dds_gateway.cpp` (`apply_dds_qos_profile`) from `DdsTranslator::qos_for_someip_message` and `qos_for_service_mapping`. Tune per deployment via Cyclone XML (`qos_profile_file` / `CYCLONEDDS_URI`).

## Topic naming

`DdsTranslator::build_dds_topic` produces hierarchical names such as:

`opensomeip/svc/0x6001/inst/0x0001/event/0x8001`

`opensomeip/svc/0x6001/inst/0x0001/method/0x0001`

Override with `ServiceMapping::external_identifier` when integrating with an existing DDS topic graph.

## IDL examples

- `vehicle::VehicleSpeed`, `vehicle::VehiclePosition` — typed vehicle data for native DDS consumers.
- `vehicle::SomeipBridgeSample` — gateway envelope (`service_id`, `instance_id`, `method_or_event_id`, `message_type`, `payload`).

Regenerated code is produced by **idlc** via CMake (`idlc_generate`).

## Build

Configure the superproject with the DDS gateway enabled and Cyclone DDS installed:

```bash
cmake -S /path/to/opensomeip-gateways -B build \
  -DBUILD_GATEWAY_DDS=ON \
  -DCMAKE_PREFIX_PATH="/path/to/cyclonedds/install"
cmake --build build --target opensomeip-gateway-dds
```

Tests and example:

```bash
cmake --build build --target test_dds_gateway dds_adaptive_bridge
ctest --test-dir build -R DdsGatewayTests
```

`examples/dds_config.yaml` documents YAML-shaped configuration for operators (the library uses `DdsConfig` in code; load YAML with your preferred parser if needed).

## DDS implementations

This target is wired to **Eclipse Cyclone DDS** (`find_package(CycloneDDS)` + `CycloneDDS::ddsc`). Other DDS implementations (Fast DDS, RTI Connext, etc.) can be supported by replacing the pimpl layer while keeping `DdsTranslator` and `DdsConfig` stable.

## Example

See `examples/dds_adaptive_bridge.cpp` and run `dds_adaptive_bridge` with Cyclone DDS available on the host.
