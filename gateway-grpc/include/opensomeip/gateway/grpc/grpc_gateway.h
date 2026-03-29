/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_GRPC_GRPC_GATEWAY_H
#define OPENSOMEIP_GATEWAY_GRPC_GRPC_GATEWAY_H

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_builder.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "e2e/e2e_config.h"
#include "e2e/e2e_protection.h"
#include "events/event_subscriber.h"
#include "opensomeip/gateway/gateway_base.h"
#include "opensomeip/gateway/grpc/grpc_translator.h"
#include "rpc/rpc_client.h"
#include "rpc/rpc_server.h"
#include "sd/sd_client.h"
#include "sd/sd_server.h"
#include "someip/message.h"

namespace opensomeip {
namespace gateway {
namespace grpc {

namespace detail {
class SomeIpGatewayGrpcService;
}

struct GrpcTlsServerOptions {
    bool enabled{false};
    std::string cert_chain_pem_path;
    std::string private_key_pem_path;
    std::string client_ca_pem_path;
    bool require_client_cert{false};
};

struct GrpcTlsClientOptions {
    bool enabled{false};
    std::string root_ca_pem_path;
    std::string client_cert_chain_pem_path;
    std::string client_private_key_pem_path;
    std::string target_authority_override;
};

struct GrpcCloudTarget {
    std::string name;
    std::string grpc_uri;
    GrpcTlsClientOptions tls;
};

struct GrpcConfig {
    std::string gateway_name{"grpc-gateway"};
    std::string server_listen_address{"0.0.0.0:50051"};
    GrpcTlsServerOptions server_tls;
    std::vector<GrpcCloudTarget> cloud_targets;
    int max_concurrent_streams{100};

    uint16_t someip_bridge_client_id{0x7001};
    std::chrono::milliseconds rpc_response_timeout{5000};

    bool enable_sd_client{false};
    someip::sd::SdConfig sd_client_config{};

    bool enable_sd_server{false};
    someip::sd::SdConfig sd_server_config{};

    bool enable_e2e_bridge{false};
    someip::e2e::E2EConfig e2e_config{};

    ::grpc::ChannelArguments default_channel_args{};
};

struct EventStreamKey {
    uint32_t service_id{0};
    uint32_t instance_id{0};
    uint32_t event_group_id{0};

    bool operator<(const EventStreamKey& o) const {
        if (service_id != o.service_id) {
            return service_id < o.service_id;
        }
        if (instance_id != o.instance_id) {
            return instance_id < o.instance_id;
        }
        return event_group_id < o.event_group_id;
    }
};

struct EventStreamSink {
    std::mutex mu;
    std::condition_variable cv;
    std::queue<::opensomeip::gateway::grpc::v1::EventFrame> pending;
    std::atomic<bool> closed{false};
};

class GrpcGateway : public GatewayBase {
public:
    explicit GrpcGateway(const GrpcConfig& config);
    ~GrpcGateway() override;

    GrpcGateway(const GrpcGateway&) = delete;
    GrpcGateway& operator=(const GrpcGateway&) = delete;
    GrpcGateway(GrpcGateway&&) = delete;
    GrpcGateway& operator=(GrpcGateway&&) = delete;

    someip::Result start() override;
    someip::Result stop() override;

    someip::Result on_someip_message(const someip::Message& msg) override;

    const GrpcConfig& config() const { return config_; }
    GrpcTranslator& translator() { return translator_; }
    const GrpcTranslator& translator() const { return translator_; }

    someip::rpc::RpcClient& rpc_client() { return *rpc_client_; }
    const someip::rpc::RpcClient& rpc_client() const { return *rpc_client_; }

    someip::events::EventSubscriber& event_subscriber() { return *event_subscriber_; }
    const someip::events::EventSubscriber& event_subscriber() const { return *event_subscriber_; }

    void register_vehicle_rpc_server(uint16_t service_id, someip::rpc::RpcServer& server);

    someip::sd::SdClient* sd_client() { return sd_client_.get(); }
    someip::sd::SdServer* sd_server() { return sd_server_.get(); }

    std::shared_ptr<::grpc::Channel> channel_for_target(const std::string& target_name) const;

    void register_event_stream(const EventStreamKey& key, const std::shared_ptr<EventStreamSink>& sink);
    void unregister_event_stream(const EventStreamKey& key, const std::shared_ptr<EventStreamSink>& sink);
    void deliver_event_frame(const std::shared_ptr<EventStreamSink>& sink,
                             const ::opensomeip::gateway::grpc::v1::EventFrame& frame);

    void broadcast_notification(uint32_t service_id, uint32_t instance_id,
                                const ::opensomeip::gateway::grpc::v1::EventFrame& frame);

private:
    friend class detail::SomeIpGatewayGrpcService;

    std::string read_file_or_empty(const std::string& path) const;
    std::shared_ptr<::grpc::ServerCredentials> build_server_credentials() const;
    std::shared_ptr<::grpc::ChannelCredentials> build_channel_credentials(
        const GrpcTlsClientOptions& tls) const;

    mutable std::mutex channels_mu_;
    mutable std::map<std::string, std::shared_ptr<::grpc::Channel>> channels_;

    GrpcConfig config_;
    GrpcTranslator translator_;

    std::unique_ptr<someip::rpc::RpcClient> rpc_client_;
    std::unique_ptr<someip::events::EventSubscriber> event_subscriber_;
    std::unique_ptr<someip::sd::SdClient> sd_client_;
    std::unique_ptr<someip::sd::SdServer> sd_server_;
    std::unique_ptr<someip::e2e::E2EProtection> e2e_protection_;

    std::mutex vehicle_servers_mu_;
    std::map<uint16_t, someip::rpc::RpcServer*> vehicle_rpc_servers_;

    std::unique_ptr<detail::SomeIpGatewayGrpcService> grpc_service_;
    std::unique_ptr<::grpc::Server> grpc_server_;

    std::mutex stream_mu_;
    std::multimap<EventStreamKey, std::weak_ptr<EventStreamSink>> stream_subscribers_;
};

}  // namespace grpc
}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_GRPC_GRPC_GATEWAY_H
