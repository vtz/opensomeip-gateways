/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_ICEORYX2_GATEWAY_H
#define OPENSOMEIP_GATEWAY_ICEORYX2_GATEWAY_H

#include <atomic>
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

#include "events/event_publisher.h"
#include "events/event_subscriber.h"
#include "rpc/rpc_client.h"
#include "rpc/rpc_server.h"
#include "sd/sd_client.h"
#include "sd/sd_server.h"
#include "transport/endpoint.h"
#include "transport/transport.h"
#include "transport/udp_transport.h"

namespace opensomeip {
namespace gateway {
namespace iceoryx2 {

/**
 * Shared-memory / QoS hints for iceoryx2 ServiceBuilder (interpreted when the
 * real iceoryx2 runtime is linked; otherwise stored for diagnostics/config).
 */
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

    /**
     * When true, pub/sub and request/response samples are exchanged through an
     * in-process bus (useful for examples/tests without libiceoryx2 installed).
     */
    bool use_inprocess_shm_simulation{false};
};

using Iceoryx2OutboundHook =
    std::function<void(const std::string& iceoryx2_service_name,
                       const std::vector<uint8_t>& sample)>;

class Iceoryx2InprocessBus;

/**
 * Bridges SOME/IP (messages, events, RPC, SD) with iceoryx2-style named services.
 *
 * Without the optional iceoryx2 runtime link, samples are delivered through an
 * outbound hook and/or the in-process simulation bus (see Iceoryx2Config).
 */
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

    /** Observe or forward every iceoryx2-bound sample (works without libiceoryx2). */
    void set_iceoryx2_outbound_hook(Iceoryx2OutboundHook hook);

    /**
     * Simulate an incoming iceoryx2 sample (subscriber or RPC response path).
     * Intended for tests and single-threaded demos (not fully synchronized).
     */
    void inject_iceoryx2_sample(const std::string& iceoryx2_service_name,
                                const std::vector<uint8_t>& sample);

    uint64_t get_sd_offer_count() const { return sd_offers_sent_.load(); }
    uint64_t get_sd_discovery_notifications() const { return sd_finds_handled_.load(); }

private:
    using ServiceKey = std::pair<uint16_t, uint16_t>;

    someip::Result publish_toward_iceoryx2(const std::string& iceoryx2_name,
                                          const std::vector<uint8_t>& sample,
                                          const ServiceMapping* mapping);

    void handle_incoming_envelope(const Iceoryx2Envelope& env,
                                  const std::string& iceoryx2_name);

    someip::Result bridge_pub_sub_someip_to_external(const someip::Message& msg,
                                                     const ServiceMapping& mapping);

    someip::Result bridge_rpc_someip_to_external(const someip::Message& msg,
                                                  const ServiceMapping& mapping);

    void setup_rpc_server_for_mapping(const ServiceMapping& mapping);
    void setup_event_subscriptions_for_mapping(const ServiceMapping& mapping);
    someip::Result setup_sd_proxy_for_mapping(const ServiceMapping& mapping);

    static ServiceKey make_key(uint16_t service_id, uint16_t instance_id) {
        return {service_id, instance_id};
    }

    Iceoryx2Config config_;
    Iceoryx2MessageTranslator translator_{};

    std::mutex hook_mutex_;
    Iceoryx2OutboundHook outbound_hook_;

    std::map<ServiceKey, std::unique_ptr<someip::events::EventPublisher>> event_publishers_;
    std::unique_ptr<someip::events::EventSubscriber> event_subscriber_;
    std::unique_ptr<someip::rpc::RpcClient> rpc_client_;
    std::map<uint16_t, std::unique_ptr<someip::rpc::RpcServer>> rpc_servers_;

    std::unique_ptr<someip::sd::SdClient> sd_client_;
    std::unique_ptr<someip::sd::SdServer> sd_server_;

    std::unique_ptr<someip::transport::UdpTransport> udp_transport_;
    std::unique_ptr<someip::transport::ITransportListener> udp_listener_;

    std::atomic<uint64_t> sd_offers_sent_{0};
    std::atomic<uint64_t> sd_finds_handled_{0};

    std::unique_ptr<Iceoryx2InprocessBus> inprocess_bus_;
};

}  // namespace iceoryx2
}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_ICEORYX2_GATEWAY_H
