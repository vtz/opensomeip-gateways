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
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "e2e/e2e_protection.h"
#include "events/event_subscriber.h"
#include "opensomeip/gateway/gateway_base.h"
#include "opensomeip/gateway/grpc/grpc_config.h"
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

/**
 * @brief Runtime configuration for the dual-role gRPC gateway (vehicle/edge server + cloud client).
 */
struct GrpcConfig : GrpcConfigCore {
    grpc::ChannelArguments default_channel_args{};
};

/**
 * @brief SOME/IP ↔ gRPC bridge: exposes vehicle SOME/IP over gRPC and forwards cloud calls using
 *        gRPC client channels. Uses @c someip::rpc::RpcClient, @c someip::rpc::RpcServer,
 *        @c someip::events::EventSubscriber, optional @c someip::sd::SdClient / SdServer, and
 *        optional @c someip::e2e::E2EProtection.
 */
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

    /**
     * @brief Register an RpcServer instance for a vehicle-side service (e.g. digital twin radar).
     *        The gateway does not take ownership; lifetime must exceed @c GrpcGateway.
     */
    void register_vehicle_rpc_server(uint16_t service_id, someip::rpc::RpcServer& server);

    someip::sd::SdClient* sd_client() { return sd_client_.get(); }
    someip::sd::SdServer* sd_server() { return sd_server_.get(); }

    std::shared_ptr<grpc::Channel> channel_for_target(const std::string& target_name) const;

private:
    friend class detail::SomeIpGatewayGrpcService;

    std::string read_file_or_empty(const std::string& path) const;
    std::shared_ptr<grpc::ServerCredentials> build_server_credentials() const;
    std::shared_ptr<grpc::ChannelCredentials> build_channel_credentials(
        const GrpcTlsClientOptions& tls) const;

    mutable std::mutex channels_mu_;
    mutable std::map<std::string, std::shared_ptr<grpc::Channel>> channels_;

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
    std::unique_ptr<grpc::Server> grpc_server_;

    std::mutex stream_mu_;
};

}  // namespace grpc
}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_GRPC_GRPC_GATEWAY_H
