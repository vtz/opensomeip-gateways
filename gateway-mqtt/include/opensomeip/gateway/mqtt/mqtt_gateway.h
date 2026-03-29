/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_MQTT_GATEWAY_H
#define OPENSOMEIP_GATEWAY_MQTT_GATEWAY_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "opensomeip/gateway/gateway_base.h"
#include "opensomeip/gateway/mqtt/mqtt_translator.h"
#include "common/result.h"
#include "e2e/e2e_config.h"
#include "e2e/e2e_protection.h"
#include "events/event_publisher.h"
#include "events/event_subscriber.h"
#include "events/event_types.h"
#include "rpc/rpc_client.h"
#include "rpc/rpc_server.h"
#include "sd/sd_client.h"
#include "sd/sd_server.h"
#include "someip/message.h"
#include "transport/endpoint.h"
#include "transport/transport.h"
#include "transport/udp_transport.h"

namespace opensomeip {
namespace gateway {

class MqttGatewayInternalCallback;

struct BufferedMqttPublish {
    std::string topic;
    std::vector<uint8_t> payload;
    int qos{0};
    bool retain{false};
};

/**
 * @brief Fixed-capacity queue for MQTT publishes while the broker is unreachable.
 */
class OfflineMqttRingBuffer {
public:
    explicit OfflineMqttRingBuffer(std::size_t capacity);

    bool push(BufferedMqttPublish item);
    bool pop(BufferedMqttPublish& out);
    std::size_t size() const;
    std::size_t capacity() const;
    void clear();

private:
    std::size_t capacity_;
    std::deque<BufferedMqttPublish> queue_;
    mutable std::mutex mutex_;
};

struct MqttTlsConfig {
    bool enable{false};
    std::string trust_store;   //!< CA file (PEM)
    std::string key_store;       //!< Client certificate (PEM)
    std::string private_key;   //!< Client private key (PEM)
    std::string private_key_password;
    bool enable_server_cert_auth{true};
};

struct MqttLastWill {
    bool enable{false};
    std::string topic;
    std::vector<uint8_t> payload;
    int qos{0};
    bool retain{false};
};

/**
 * @brief Eclipse Paho MQTT C++ client settings and gateway behaviour.
 */
struct MqttConfig {
    std::string broker_uri{"tcp://localhost:1883"};
    std::string client_id{"opensomeip-mqtt-gateway"};

    std::string topic_prefix{"vehicle"};
    std::string vin{"UNKNOWN"};

    /** Used when matching @ref ServiceMapping for inbound SOME/IP messages without instance id. */
    uint16_t default_someip_instance_id{0x0001};

    int mqtt_protocol_version{4}; //!< 4 = MQTT 3.1.1, 5 = MQTT v5 when supported by Paho

    int keep_alive_seconds{60};
    bool clean_session{true};
    std::chrono::milliseconds connect_timeout{std::chrono::seconds{10}};

    bool auto_reconnect{true};
    std::chrono::milliseconds reconnect_min_delay{std::chrono::seconds{1}};
    std::chrono::milliseconds reconnect_max_delay{std::chrono::seconds{60}};

    MqttTlsConfig tls;
    MqttLastWill last_will;

    int default_publish_qos{1};
    int default_subscribe_qos{1};

    /** Per event/method id → QoS; ids not listed use defaults. */
    std::unordered_map<uint16_t, int> qos_by_event_id;
    std::unordered_map<uint16_t, int> qos_by_method_id;

    MqttPayloadEncoding outbound_encoding{MqttPayloadEncoding::JSON};
    MqttPayloadEncoding inbound_encoding{MqttPayloadEncoding::RAW};

    std::size_t offline_buffer_capacity{256};

    bool use_mqtt_v5_request_response{true};

    /** Optional E2E protect/validate on SOME/IP path when framing. */
    bool use_e2e{false};
    std::optional<someip::e2e::E2EConfig> e2e_config;

    /** When true, @ref flush_offline_buffer runs automatically after connect. */
    bool flush_offline_on_connect{true};
};

using SomeipOutboundSink = std::function<void(const someip::Message&)>;

/**
 * @brief SOME/IP ↔ MQTT gateway using opensomeip services and Eclipse Paho MQTT C++.
 */
class MqttGateway : public GatewayBase {
public:
    explicit MqttGateway(MqttConfig mqtt_config);
    ~MqttGateway() override;

    MqttGateway(const MqttGateway&) = delete;
    MqttGateway& operator=(const MqttGateway&) = delete;
    MqttGateway(MqttGateway&&) = delete;
    MqttGateway& operator=(MqttGateway&&) = delete;

    const MqttConfig& get_mqtt_config() const { return mqtt_config_; }
    MqttConfig& get_mqtt_config() { return mqtt_config_; }

    MqttTranslator& translator() { return translator_; }
    const MqttTranslator& translator() const { return translator_; }

    void set_someip_outbound_sink(SomeipOutboundSink sink);

    /**
     * @brief Optional UDP SOME/IP transport: received messages are passed to @ref on_someip_message.
     */
    void enable_someip_udp_bridge(const someip::transport::Endpoint& bind_ep,
                                  const someip::transport::UdpTransportConfig& cfg = {});

    void attach_rpc_client(const std::shared_ptr<someip::rpc::RpcClient>& rpc);
    void attach_rpc_server(const std::shared_ptr<someip::rpc::RpcServer>& server);
    void attach_sd_client(const std::shared_ptr<someip::sd::SdClient>& sd);
    void attach_sd_server(const std::shared_ptr<someip::sd::SdServer>& sd_server);

    /**
     * @brief Register per-(service,instance) event publisher for cloud→SOME/IP event injection.
     */
    bool register_event_publisher(uint16_t service_id, uint16_t instance_id,
                                  std::unique_ptr<someip::events::EventPublisher> publisher);

    /**
     * @brief Subscribe SOME/IP event groups; notifications are mirrored to MQTT.
     */
    bool subscribe_someip_eventgroup(uint16_t service_id, uint16_t instance_id,
                                     uint16_t eventgroup_id);

    someip::Result start() override;
    someip::Result stop() override;

    someip::Result on_someip_message(const someip::Message& msg) override;

    std::string route_someip_to_mqtt_topic(const someip::Message& msg,
                                           const ServiceMapping& mapping) const;

    int qos_for_outbound_event(uint16_t event_or_method_id) const;
    int qos_for_outbound_rpc(uint16_t method_id) const;

    std::size_t offline_buffer_occupancy() const;
    someip::Result flush_offline_buffer();

    /** For tests: skip real TCP connection and mark internal “connected” state. */
    void test_set_mqtt_connected(bool connected);

private:
    friend class MqttGatewayInternalCallback;

    struct PendingMqttRpc {
        uint16_t service_id{0};
        uint16_t instance_id{0};
        uint16_t method_id{0};
        someip::MessageType response_type{someip::MessageType::RESPONSE};
    };

    void on_mqtt_delivery_complete(int token_id);
    void on_mqtt_connection_lost(const std::string& cause);
    void on_mqtt_connected();
    void on_mqtt_message_arrived(const std::string& topic, const void* payload,
                                 std::size_t len, int qos, bool duplicate, bool retain);

    someip::Result publish_mqtt(const std::string& topic, const std::vector<uint8_t>& payload,
                                int qos, bool retain, const std::string& response_topic,
                                const std::vector<uint8_t>& correlation_data);

    void schedule_reconnect();
    void discover_services_with_sd();

    void handle_inbound_mqtt_command(const std::string& topic, const std::vector<uint8_t>& body);
    void handle_inbound_mqtt_rpc_response(const std::string& topic,
                                          const std::vector<uint8_t>& body,
                                          const std::vector<uint8_t>& correlation);

    const ServiceMapping* find_mapping_by_topic_prefix(const std::string& topic) const;

    MqttConfig mqtt_config_;
    MqttTranslator translator_;
    SomeipOutboundSink someip_outbound_sink_;
    mutable std::mutex sink_mutex_;

    std::unique_ptr<class MqttGatewayPahoContext> paho_;

    OfflineMqttRingBuffer offline_buffer_;
    std::atomic<bool> mqtt_connected_{false};
    std::atomic<bool> stop_reconnect_{false};
    std::chrono::milliseconds current_backoff_;

    std::shared_ptr<someip::rpc::RpcClient> rpc_client_;
    std::shared_ptr<someip::rpc::RpcServer> rpc_server_;
    std::shared_ptr<someip::sd::SdClient> sd_client_;
    std::shared_ptr<someip::sd::SdServer> sd_server_;

    std::unordered_map<uint64_t, std::unique_ptr<someip::events::EventPublisher>> event_publishers_;
    std::unique_ptr<someip::events::EventSubscriber> event_subscriber_;
    uint16_t someip_client_id_{0x4200};

    std::unique_ptr<someip::transport::UdpTransport> udp_transport_;
    std::unique_ptr<GatewayUdpBridgeListener> udp_listener_;

    std::mutex pending_rpc_mutex_;
    std::unordered_map<std::string, PendingMqttRpc> pending_by_correlation_;

    someip::e2e::E2EProtection e2e_;
};

}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_MQTT_GATEWAY_H
