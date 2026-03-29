/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_GRPC_CONFIG_H
#define OPENSOMEIP_GATEWAY_GRPC_CONFIG_H

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "sd/sd_types.h"
#include "e2e/e2e_config.h"

namespace opensomeip {
namespace gateway {
namespace grpc {

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

struct GrpcConfigCore {
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
};

}  // namespace grpc
}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_GRPC_CONFIG_H
