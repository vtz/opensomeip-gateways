/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_DBUS_DBUS_GATEWAY_H
#define OPENSOMEIP_GATEWAY_DBUS_DBUS_GATEWAY_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "opensomeip/gateway/config.h"
#include "opensomeip/gateway/dbus/dbus_translator.h"
#include "opensomeip/gateway/gateway_base.h"
#include "common/result.h"
#include "events/event_publisher.h"
#include "events/event_subscriber.h"
#include "rpc/rpc_client.h"
#include "rpc/rpc_server.h"
#include "sd/sd_client.h"
#include "sd/sd_server.h"
#include "someip/message.h"
#include "transport/endpoint.h"
#include "transport/udp_transport.h"

namespace opensomeip {
namespace gateway {
namespace dbus {

enum class DbusBusType : uint8_t {
    SYSTEM,
    SESSION
};

/**
 * @brief D-Bus-facing settings, SOME/IP bind/SD-style endpoint options, and RPC client id.
 */
struct DbusConfig {
    DbusBusType bus_type{DbusBusType::SYSTEM};
    std::string bus_name_prefix{"com.opensomeip"};
    std::string object_path_prefix{"/com/opensomeip"};
    bool enable_introspection{true};

    /** UDP bind address/port when @ref enable_someip_udp_bridge is used. */
    SomeipEndpointConfig someip{};

    /** Client id passed to SOME/IP event subscriber and RPC client helpers. */
    uint16_t rpc_client_id{0x4200};

    /** Fallback instance when matching @ref ServiceMapping for inbound SOME/IP. */
    uint16_t default_someip_instance_id{0x0001};
};

using SomeipOutboundSink = std::function<void(const someip::Message&)>;

/**
 * @brief SOME/IP ↔ D-Bus gateway: notifications as signals, D-Bus method calls as SOME/IP RPC
 *        payloads via the external callback, with optional UDP and SD/RPC attachments.
 */
class DbusGateway : public GatewayBase {
public:
    explicit DbusGateway(DbusConfig config);
    ~DbusGateway() override;

    DbusGateway(const DbusGateway&) = delete;
    DbusGateway& operator=(const DbusGateway&) = delete;
    DbusGateway(DbusGateway&&) = delete;
    DbusGateway& operator=(DbusGateway&&) = delete;

    const DbusConfig& get_dbus_config() const { return config_; }
    DbusConfig& get_dbus_config() { return config_; }

    DbusTranslator& translator() { return translator_; }
    const DbusTranslator& translator() const { return translator_; }

    void set_someip_outbound_sink(SomeipOutboundSink sink);

    void enable_someip_udp_bridge(const someip::transport::Endpoint& bind_ep,
                                  const someip::transport::UdpTransportConfig& cfg = {});

    void attach_rpc_client(const std::shared_ptr<someip::rpc::RpcClient>& rpc);
    void attach_rpc_server(const std::shared_ptr<someip::rpc::RpcServer>& server);
    void attach_sd_client(const std::shared_ptr<someip::sd::SdClient>& sd);
    void attach_sd_server(const std::shared_ptr<someip::sd::SdServer>& sd_server);

    bool register_event_publisher(uint16_t service_id, uint16_t instance_id,
                                  std::unique_ptr<someip::events::EventPublisher> publisher);

    bool subscribe_someip_eventgroup(uint16_t service_id, uint16_t instance_id,
                                     uint16_t eventgroup_id);

    someip::Result start() override;
    someip::Result stop() override;

    someip::Result on_someip_message(const someip::Message& msg) override;

    /**
     * @brief Wait/process D-Bus I/O; required for D-Bus method calls to reach the gateway
     *        when using the systemd sd-bus backend.
     */
    someip::Result poll_dbus(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});

    /** @brief Inject a D-Bus-origin RPC as SOME/IP (used by sd-bus vtable or tests). */
    someip::Result emit_external_rpc(uint16_t service_id, uint16_t instance_id,
                                     uint16_t method_id, const std::vector<uint8_t>& payload);

    /** @brief Skip sd-bus connection (unit tests / headless CI). */
    void test_set_dbus_enabled(bool enabled);

private:
    class Impl;

    DbusConfig config_;
    DbusTranslator translator_;
    SomeipOutboundSink someip_outbound_sink_;
    mutable std::mutex sink_mutex_;

    std::shared_ptr<someip::rpc::RpcClient> rpc_client_;
    std::shared_ptr<someip::rpc::RpcServer> rpc_server_;
    std::shared_ptr<someip::sd::SdClient> sd_client_;
    std::shared_ptr<someip::sd::SdServer> sd_server_;

    std::unordered_map<uint64_t, std::unique_ptr<someip::events::EventPublisher>> event_publishers_;
    std::unique_ptr<someip::events::EventSubscriber> event_subscriber_;

    std::unique_ptr<someip::transport::UdpTransport> udp_transport_;
    class UdpBridgeListener;
    std::unique_ptr<UdpBridgeListener> udp_listener_;

    std::atomic<uint16_t> next_session_{1};
    std::unique_ptr<Impl> impl_;
};

}  // namespace dbus
}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_DBUS_DBUS_GATEWAY_H
