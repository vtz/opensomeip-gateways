# SOME/IP ↔ D-Bus gateway

This gateway mirrors selected SOME/IP services onto D-Bus so desktop and vehicle services can be observed and driven with the same tooling used for system services (similar in spirit to how NetworkManager exposes a well-known bus name and object hierarchy).

## Architecture

- **Common library**: `GatewayBase`, `ServiceMapping`, `GatewayDirection`, `TranslationMode`, `MessageTranslator`, and `GatewayStats` from `opensomeip-gateway-common`.
- **opensomeip**: `Message`, `MessageId`, `RequestId`, `MessageType`, `Serializer`, `Deserializer`, `EventPublisher`, `EventSubscriber`, `RpcClient`, `RpcServer`, `SdClient`, `SdServer`, `UdpTransport`, `Endpoint`, `Result`.
- **D-Bus transport**: When `libsystemd` is available at build time, the implementation uses **sd-bus** inside a private `DbusGateway::Impl` (vtable for `InvokeRpc`, `sd_bus_emit_signal` for outbound signals). Without systemd, the same API compiles but D-Bus I/O is a no-op so CI and macOS builds can still link tests.

Data flow:

1. **SOME/IP → D-Bus**: `NOTIFICATION` / `TP_NOTIFICATION` and `REQUEST` messages matching a `ServiceMapping` with `SOMEIP_TO_EXTERNAL` or `BIDIRECTIONAL` are emitted as D-Bus signals `SomeipNotification` or `SomeipRequest` with signature `(qay)` — method or event id (`q`) and opaque payload (`ay`).
2. **D-Bus → SOME/IP**: The `InvokeRpc` method (`qay` in, `ay` out) forwards to `emit_external_rpc`, which builds a SOME/IP `REQUEST` and invokes `ExternalMessageCallback` / `SomeipOutboundSink` when the mapping allows `EXTERNAL_TO_SOMEIP` or `BIDIRECTIONAL`.

Optional attachments match other gateways: UDP bridge into `on_someip_message`, SD client/server `initialize`/`shutdown`, event publisher registry, event-group subscription.

## D-Bus interface design

Per mapped SOME/IP service id:

- **Interface**: `com.opensomeip.Service.<service_hex>` (four-digit lowercase hex), e.g. `com.opensomeip.Service.4500`.
- **Object path**: `<object_path_prefix>/svc_<sid>/inst_<iid>` with lowercase hex segments.
- **Well-known name**: `<bus_name_prefix>.svc.<sid>.inst.<iid>`.

Introspection XML (for documentation or export when `enable_introspection` is used in your tooling) is produced by `DbusTranslator::generate_introspection_xml`, including `InvokeRpc`, `SomeipNotification`, and `SomeipRequest`.

Typed SOME/IP fields are described in an XML comment via `build_signature_from_someip_types`; the wire format remains opaque `ay` on D-Bus unless you add your own marshalling.

## Features

- Configurable system vs session bus, name/path prefixes, RPC client id, SOME/IP endpoint defaults.
- Pimpl-isolated sd-bus registration and signal emission.
- `poll_dbus(timeout)` for integrating with your own run loop.
- GoogleTest coverage for translator, config defaults, lifecycle, stats, and RPC injection.

## Build

From the `opensomeip-gateways` tree (with `opensomeip` installed or `../some-ip` available, as in the super-project `CMakeLists.txt`):

```bash
cmake -S . -B build -DBUILD_GATEWAY_DBUS=ON -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build -R DbusGateway -V
```

On Linux, install `libsystemd` development packages so `pkg-config` can find `libsystemd` and enable `HAVE_SYSTEMD`.

## Examples

- `examples/dbus_system_bridge.cpp` — vehicle-style telematics service `0x4500`, `Serializer`-built notification payload, callbacks for outbound SOME/IP, short `poll_dbus` loop.
- `examples/dbus_config.yaml` — reference YAML aligned with `DbusConfig` / `ServiceMapping` fields.

Run the example (requires a system D-Bus and appropriate permissions if using the system bus):

```bash
./build/gateway-dbus/examples/dbus_system_bridge
```
