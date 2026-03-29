/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_GRPC_GRPC_TRANSLATOR_H
#define OPENSOMEIP_GATEWAY_GRPC_GRPC_TRANSLATOR_H

#include <grpcpp/support/status.h>
#include <grpcpp/support/status_code_enum.h>

#include <cstdint>
#include <string>

#include "gateway.pb.h"
#include "rpc/rpc_types.h"
#include "someip/message.h"
#include "someip/types.h"

namespace opensomeip {
namespace gateway {
namespace grpc {

/**
 * @brief Bidirectional mapping between SOME/IP ReturnCode / RpcResult and gRPC status semantics,
 *        plus conversion between @c someip::Message and protobuf envelopes.
 */
class GrpcTranslator {
public:
    GrpcTranslator() = default;

    static ::grpc::StatusCode return_code_to_grpc_status_code(someip::ReturnCode code);
    static someip::ReturnCode grpc_status_code_to_return_code(::grpc::StatusCode code);

    static ::grpc::Status return_code_to_grpc_status(someip::ReturnCode code,
                                                     const std::string& message = {});

    static someip::ReturnCode rpc_result_to_return_code(someip::rpc::RpcResult result);
    static ::grpc::StatusCode rpc_result_to_grpc_status_code(someip::rpc::RpcResult result);

    bool envelope_to_message(const ::opensomeip::gateway::grpc::v1::SomeIpEnvelope& env,
                             someip::Message& out) const;

    void message_to_envelope(const someip::Message& msg,
                             ::opensomeip::gateway::grpc::v1::SomeIpEnvelope& env) const;

    static std::string grpc_status_code_name(::grpc::StatusCode code);
};

}  // namespace grpc
}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_GRPC_GRPC_TRANSLATOR_H
