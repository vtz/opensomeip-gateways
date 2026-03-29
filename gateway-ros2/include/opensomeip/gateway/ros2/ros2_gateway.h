/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_ROS2_ROS2_GATEWAY_H
#define OPENSOMEIP_GATEWAY_ROS2_ROS2_GATEWAY_H

#include <cstdint>
#include <functional>
#include <optional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "opensomeip/gateway/gateway_base.h"
#include "opensomeip/gateway/ros2/ros2_translator.h"
#include "common/result.h"
#include "e2e/e2e_config.h"
#include "e2e/e2e_protection.h"
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
namespace ros2 {

class Ros2GatewayImpl;

/**
 * @brief Runtime settings for the ROS 2 bridge process and SOME/IP stack hooks.
 */
struct Ros2Config {
    std::string node_name{"opensomeip_ros2_gateway"};
    /** Leading slash recommended, e.g. ``/vehicle``. */
    std::string ros_namespace{"/vehicle"};

    std::string someip_bind_address{"0.0.0.0"};
    uint16_t someip_bind_port{30500};

    uint16_t rpc_client_id{0x5100};
    /** Used when matching @ref ServiceMapping if the SOME/IP stack has no separate instance field. */
    uint16_t default_someip_instance_id{0x0001};

    std::string topic_prefix{"someip"};

    bool enable_udp_transport{false};
    bool enable_rpc_client{true};
    bool enable_rpc_server{false};
    bool enable_sd_client{false};
    bool enable_sd_server{false};
    bool enable_event_subscriber{true};

    someip::sd::SdConfig sd_client_config{};
    someip::sd::SdConfig sd_server_config{};

    bool use_e2e{false};
    std::optional<someip::e2e::E2EConfig> e2e_config;

    /** Default SOME/IP transport assumption for outbound ROS QoS. */
    SomeipTransportKind default_someip_transport{SomeipTransportKind::UDP_UNICAST};
};

using SomeipOutboundSink = std::function<void(const someip::Message&)>;
using Ros2PublishCallback =
    std::function<void(const std::string& topic, const std::vector<uint8_t>& payload)>;

/**
 * @brief SOME/IP ↔ ROS 2 gateway using opensomeip services and optional rclcpp.
 *
 * Owns or attaches @c someip::events::EventPublisher, @c EventSubscriber,
 * @c RpcClient, @c RpcServer, @c SdClient, @c SdServer, @c UdpTransport, and
 * @c E2EProtection. ROS 2 publishers/subscriptions live behind @c Ros2GatewayImpl.
 */
class Ros2Gateway : public GatewayBase {
public:
    explicit Ros2Gateway(Ros2Config config);
    ~Ros2Gateway() override;

    Ros2Gateway(const Ros2Gateway&) = delete;
    Ros2Gateway& operator=(const Ros2Gateway&) = delete;
    Ros2Gateway(Ros2Gateway&&) = delete;
    Ros2Gateway& operator=(Ros2Gateway&&) = delete;

    const Ros2Config& config() const { return config_; }
    Ros2Translator& translator() { return translator_; }
    const Ros2Translator& translator() const { return translator_; }

    void set_someip_outbound_sink(SomeipOutboundSink sink);
    void set_ros2_publish_callback(Ros2PublishCallback cb);

    void enable_someip_udp_bridge(const someip::transport::Endpoint& bind_ep,
                                  const someip::transport::UdpTransportConfig& cfg = {});

    void attach_rpc_client(const std::shared_ptr<someip::rpc::RpcClient>& rpc);
    void attach_rpc_server(uint16_t service_id, const std::shared_ptr<someip::rpc::RpcServer>& server);
    void attach_sd_client(const std::shared_ptr<someip::sd::SdClient>& sd);
    void attach_sd_server(const std::shared_ptr<someip::sd::SdServer>& sd_server);

    bool register_event_publisher(uint16_t service_id, uint16_t instance_id,
                                  std::unique_ptr<someip::events::EventPublisher> publisher);

    bool subscribe_someip_eventgroup(uint16_t service_id, uint16_t instance_id,
                                     uint16_t eventgroup_id);

    someip::Result start() override;
    someip::Result stop() override;

    someip::Result on_someip_message(const someip::Message& msg) override;

    std::string route_someip_to_ros2_topic(const someip::Message& msg,
                                           const ServiceMapping& mapping) const;

    /**
     * @brief Reverse path: ROS (or test hook) injects bytes that become SOME/IP RPC or outbound sink.
     */
    someip::Result inject_ros2_message(const std::string& ros2_topic,
                                       const std::vector<uint8_t>& payload);

    someip::rpc::RpcClient* rpc_client() { return rpc_client_.get(); }
    const someip::rpc::RpcClient* rpc_client() const { return rpc_client_.get(); }

private:
    friend class Ros2GatewayImpl;

    class UdpBridgeListener;

    const ServiceMapping* find_mapping_by_ros_topic(const std::string& topic) const;
    uint16_t command_method_for_mapping(const ServiceMapping& mapping) const;

    void publish_to_ros2_side(const std::string& topic, const Ros2QosProfile& qos,
                              const std::vector<uint8_t>& bytes);

    void emit_ros2_publish(const std::string& topic, const std::vector<uint8_t>& payload);

    Ros2Config config_;
    Ros2Translator translator_;
    SomeipOutboundSink someip_outbound_sink_;
    Ros2PublishCallback ros2_publish_callback_;
    mutable std::mutex sink_mutex_;

    std::unique_ptr<Ros2GatewayImpl> impl_;

    std::shared_ptr<someip::rpc::RpcClient> rpc_client_;
    std::unordered_map<uint16_t, std::shared_ptr<someip::rpc::RpcServer>> rpc_servers_;
    std::shared_ptr<someip::sd::SdClient> sd_client_;
    std::shared_ptr<someip::sd::SdServer> sd_server_;

    std::unordered_map<uint64_t, std::unique_ptr<someip::events::EventPublisher>> event_publishers_;
    std::unique_ptr<someip::events::EventSubscriber> event_subscriber_;

    std::unique_ptr<someip::transport::UdpTransport> udp_transport_;
    std::unique_ptr<UdpBridgeListener> udp_listener_;

    someip::e2e::E2EProtection e2e_;
};

}  // namespace ros2
}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_ROS2_ROS2_GATEWAY_H
