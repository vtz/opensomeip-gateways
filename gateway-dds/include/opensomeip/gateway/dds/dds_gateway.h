/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_DDS_DDS_GATEWAY_H
#define OPENSOMEIP_GATEWAY_DDS_DDS_GATEWAY_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "opensomeip/gateway/dds/dds_translator.h"
#include "opensomeip/gateway/gateway_base.h"
#include "common/result.h"
#include "e2e/e2e_config.h"
#include "e2e/e2e_protection.h"
#include "events/event_publisher.h"
#include "events/event_subscriber.h"
#include "rpc/rpc_client.h"
#include "rpc/rpc_server.h"
#include "sd/sd_client.h"
#include "sd/sd_server.h"
#include "sd/sd_types.h"
#include "someip/message.h"
#include "transport/endpoint.h"
#include "transport/transport.h"
#include "transport/udp_transport.h"

namespace opensomeip {
namespace gateway {
namespace dds {

struct DdsGatewayImpl;

/**
 * @brief SOME/IP listener / bridge network settings used together with the DDS participant.
 */
struct DdsSomeipSettings {
    uint16_t default_someip_instance_id{0x0001};

    bool enable_udp_listener{false};
    someip::transport::Endpoint udp_listen_endpoint{"0.0.0.0", 30500,
                                                    someip::transport::TransportProtocol::UDP};
    someip::transport::UdpTransportConfig udp_config{};

    /** When true, RPC-like SOME/IP messages map to reliable DDS QoS hints. */
    bool assume_tcp_semantics_for_rpc{true};
};

/**
 * @brief CycloneDDS / DDS stack configuration and SOME/IP-side options.
 */
struct DdsConfig {
    int32_t domain_id{0};
    std::string participant_name{"opensomeip-dds-gateway"};
    /** Optional Cyclone DDS XML QoS file; when set, CYCLONEDDS_URI may be pointed at it at start. */
    std::string qos_profile_file;

    DdsSomeipSettings someip{};

    uint16_t rpc_client_id{0x7100};

    bool enable_sd_client{false};
    someip::sd::SdConfig sd_client_config{};

    bool enable_sd_server{false};
    someip::sd::SdConfig sd_server_config{};

    bool use_e2e{false};
    std::optional<someip::e2e::E2EConfig> e2e_config{};
};

using SomeipOutboundSink = std::function<void(const someip::Message&)>;

/**
 * @brief SOME/IP ↔ DDS gateway: forwards SOME/IP toward Cyclone DDS DataWriters and injects
 *        DataReader samples toward SOME/IP via @ref set_someip_outbound_sink or attached services.
 *
 * DDS types and entities live in the private implementation (pimpl) so translation units that
 * include this header do not require DDS SDK headers.
 */
class DdsGateway : public GatewayBase {
public:
    explicit DdsGateway(DdsConfig config);
    ~DdsGateway() override;

    DdsGateway(const DdsGateway&) = delete;
    DdsGateway& operator=(const DdsGateway&) = delete;
    DdsGateway(DdsGateway&&) = delete;
    DdsGateway& operator=(DdsGateway&&) = delete;

    const DdsConfig& config() const { return config_; }
    DdsTranslator& translator() { return translator_; }
    const DdsTranslator& translator() const { return translator_; }

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
     * @brief Simulate a DDS sample on the inbound path (tests / single-process demos).
     */
    someip::Result inject_dds_sample(const std::string& dds_topic, const std::vector<uint8_t>& payload,
                                     uint16_t service_id, uint16_t instance_id, uint16_t method_id,
                                     uint8_t someip_message_type_byte);

private:
    friend struct DdsGatewayImpl;

    /** Pimpl invokes protected GatewayBase counters / user sink. */
    void deliver_inbound_from_dds(const someip::Message& reconstructed, std::size_t byte_count);

    class UdpBridgeListener;

    std::string resolve_dds_topic(const someip::Message& msg, const ServiceMapping& mapping) const;

    DdsConfig config_;
    DdsTranslator translator_;

    std::unique_ptr<struct DdsGatewayImpl> impl_;

    SomeipOutboundSink someip_outbound_sink_;
    mutable std::mutex sink_mutex_;

    std::shared_ptr<someip::rpc::RpcClient> rpc_client_;
    std::shared_ptr<someip::rpc::RpcServer> rpc_server_;
    std::shared_ptr<someip::sd::SdClient> sd_client_;
    std::shared_ptr<someip::sd::SdServer> sd_server_;

    std::unordered_map<uint64_t, std::unique_ptr<someip::events::EventPublisher>> event_publishers_;
    std::unique_ptr<someip::events::EventSubscriber> event_subscriber_;

    std::unique_ptr<someip::transport::UdpTransport> udp_transport_;
    std::unique_ptr<UdpBridgeListener> udp_listener_;

    someip::e2e::E2EProtection e2e_;
};

}  // namespace dds
}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_DDS_DDS_GATEWAY_H
