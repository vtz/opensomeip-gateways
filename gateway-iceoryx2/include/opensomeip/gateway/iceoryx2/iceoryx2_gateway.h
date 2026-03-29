/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_ICEORYX2_GATEWAY_H
#define OPENSOMEIP_GATEWAY_ICEORYX2_GATEWAY_H

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "opensomeip/gateway/gateway_base.h"
#include "opensomeip/gateway/iceoryx2/iceoryx2_translator.h"
#include "someip/message.h"

// With OPENSOMEIP_GATEWAY_HAS_ICEORYX2, include your iceoryx2 C++ headers alongside this gateway.

#include "events/event_publisher.h"
#include "events/event_subscriber.h"
#include "rpc/rpc_client.h"
#include "rpc/rpc_server.h"
#include "sd/sd_client.h"
#include "sd/sd_server.h"
#include "transport/endpoint.h"
#include "transport/tcp_transport.h"
#include "transport/transport.h"
#include "transport/udp_transport.h"

namespace opensomeip {
namespace gateway {
namespace iceoryx2 {

struct Iceoryx2PendingRpc;

struct Iceoryx2ShmSettings {
    size_t max_sample_bytes{65536};
    size_t subscriber_max_buffer_size{8};
    size_t publisher_history_size{0};
    size_t max_publishers{8};
    size_t max_subscribers{16};
    bool enable_safe_overflow{true};
};

struct Iceoryx2Config {
    std::string gateway_name{"iceoryx2-gateway"};
    std::string service_name_prefix{"someip"};
    Iceoryx2ShmSettings shm{};
    uint16_t someip_client_id{0x4200};
    bool enable_sd_proxy{true};
    someip::sd::SdConfig sd_config{};
    bool enable_someip_udp_listener{false};
    someip::transport::Endpoint someip_listen_endpoint{"0.0.0.0", 30500,
                                                       someip::transport::TransportProtocol::UDP};
    someip::transport::UdpTransportConfig udp_config{};
    bool enable_someip_tcp_transport{false};
    someip::transport::TcpTransportConfig tcp_config{};
    uint16_t default_someip_instance_id{0x0001};
    std::chrono::milliseconds rpc_bridge_timeout{std::chrono::seconds{5}};
    bool use_inprocess_shm_simulation{false};
    size_t simulated_outbound_queue_cap{1024};
};

using Iceoryx2OutboundHook =
    std::function<void(const std::string& iceoryx2_service_name,
                       const std::vector<uint8_t>& sample)>;
using SomeipOutboundSink = std::function<void(const someip::Message&)>;

class Iceoryx2Gateway : public GatewayBase {
public:
    explicit Iceoryx2Gateway(Iceoryx2Config config);
    ~Iceoryx2Gateway() override;

    Iceoryx2Gateway(const Iceoryx2Gateway&) = delete;
    Iceoryx2Gateway& operator=(const Iceoryx2Gateway&) = delete;
    Iceoryx2Gateway(Iceoryx2Gateway&&) = delete;
    Iceoryx2Gateway& operator=(Iceoryx2Gateway&&) = delete;

    someip::Result start() override;
    someip::Result stop() override;
    someip::Result on_someip_message(const someip::Message& msg) override;

    const Iceoryx2Config& get_config() const { return config_; }

    Iceoryx2Translator& translator() { return translator_; }
    const Iceoryx2Translator& translator() const { return translator_; }

    void set_someip_outbound_sink(SomeipOutboundSink sink);
    void set_iceoryx2_outbound_hook(Iceoryx2OutboundHook hook);

    void inject_iceoryx2_sample(const std::string& iceoryx2_service_name,
                                const std::vector<uint8_t>& sample);

    uint64_t get_sd_offer_count() const { return sd_offers_sent_.load(); }
    uint64_t get_sd_discovery_notifications() const { return sd_finds_handled_.load(); }

    /** When use_inprocess_shm_simulation is on, outbound samples are queued for tests. */
    size_t simulated_outbound_depth() const;
    bool pop_simulated_outbound(std::string* iceoryx2_name, std::vector<uint8_t>* sample);

private:
    using ServiceKey = std::pair<uint16_t, uint16_t>;

    const ServiceMapping* resolve_mapping(const someip::Message& msg) const;

    someip::Result publish_toward_iceoryx2(const std::string& iceoryx2_name,
                                          const std::vector<uint8_t>& sample,
                                          const ServiceMapping* mapping);

    void handle_incoming_envelope(const Iceoryx2Envelope& env,
                                  const std::string& iceoryx2_name);

    someip::Result bridge_pub_sub_someip_to_external(const someip::Message& msg,
                                                     const ServiceMapping& mapping);
    someip::Result bridge_rpc_someip_to_external(const someip::Message& msg,
                                                  const ServiceMapping& mapping);

    void forward_someip_event_notification(const someip::events::EventNotification& n,
                                           const ServiceMapping& mapping);

    someip::rpc::RpcResult rpc_handler_bridge_to_iceoryx2(const ServiceMapping& mapping,
                                                          uint16_t method_id,
                                                          uint16_t client_id,
                                                          uint16_t session_id,
                                                          const std::vector<uint8_t>& in,
                                                          std::vector<uint8_t>& out);

    void setup_rpc_server_for_mapping(const ServiceMapping& mapping);
    void setup_event_subscriptions_for_mapping(const ServiceMapping& mapping);
    someip::Result setup_sd_proxy_for_mapping(const ServiceMapping& mapping);

    void complete_pending_rpc(const std::string& correlation_id,
                              const std::vector<uint8_t>& response_payload,
                              someip::rpc::RpcResult rpc_result);

    static ServiceKey make_key(uint16_t service_id, uint16_t instance_id) {
        return {service_id, instance_id};
    }

    Iceoryx2Config config_;
    Iceoryx2Translator translator_{};
    std::mutex hook_mutex_;
    Iceoryx2OutboundHook outbound_hook_;
    std::mutex sink_mutex_;
    SomeipOutboundSink someip_outbound_sink_;

    std::map<ServiceKey, std::unique_ptr<someip::events::EventPublisher>> event_publishers_;
    std::unique_ptr<someip::events::EventSubscriber> event_subscriber_;
    std::unique_ptr<someip::rpc::RpcClient> rpc_client_;
    std::map<uint16_t, std::unique_ptr<someip::rpc::RpcServer>> rpc_servers_;

    std::unique_ptr<someip::sd::SdClient> sd_client_;
    std::unique_ptr<someip::sd::SdServer> sd_server_;

    std::unique_ptr<someip::transport::UdpTransport> udp_transport_;
    std::unique_ptr<someip::transport::ITransportListener> udp_listener_;
    std::unique_ptr<someip::transport::TcpTransport> tcp_transport_;

    std::atomic<uint64_t> sd_offers_sent_{0};
    std::atomic<uint64_t> sd_finds_handled_{0};

    mutable std::mutex simulated_out_mutex_;
    std::deque<std::pair<std::string, std::vector<uint8_t>>> simulated_outbound_;

    std::mutex pending_rpc_mutex_;
    std::map<std::string, std::shared_ptr<Iceoryx2PendingRpc>> pending_by_correlation_;
};

}  // namespace iceoryx2
}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_ICEORYX2_GATEWAY_H
