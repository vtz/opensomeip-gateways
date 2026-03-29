/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/grpc/grpc_translator.h"

#include <grpcpp/support/status.h>
#include <sstream>
#include <unordered_map>

namespace opensomeip {
namespace gateway {
namespace grpc {

namespace {

using SC = ::grpc::StatusCode;

const std::unordered_map<someip::ReturnCode, SC> kReturnToGrpc{
    {someip::ReturnCode::E_OK, SC::OK},
    {someip::ReturnCode::E_NOT_OK, SC::INTERNAL},
    {someip::ReturnCode::E_UNKNOWN_SERVICE, SC::NOT_FOUND},
    {someip::ReturnCode::E_UNKNOWN_METHOD, SC::UNIMPLEMENTED},
    {someip::ReturnCode::E_NOT_READY, SC::FAILED_PRECONDITION},
    {someip::ReturnCode::E_NOT_REACHABLE, SC::UNAVAILABLE},
    {someip::ReturnCode::E_TIMEOUT, SC::DEADLINE_EXCEEDED},
    {someip::ReturnCode::E_WRONG_PROTOCOL_VERSION, SC::FAILED_PRECONDITION},
    {someip::ReturnCode::E_WRONG_INTERFACE_VERSION, SC::FAILED_PRECONDITION},
    {someip::ReturnCode::E_MALFORMED_MESSAGE, SC::INVALID_ARGUMENT},
    {someip::ReturnCode::E_WRONG_MESSAGE_TYPE, SC::INVALID_ARGUMENT},
    {someip::ReturnCode::E_E2E_REPEATED, SC::ABORTED},
    {someip::ReturnCode::E_E2E_WRONG_SEQUENCE, SC::ABORTED},
    {someip::ReturnCode::E_E2E, SC::DATA_LOSS},
    {someip::ReturnCode::E_E2E_NOT_AVAILABLE, SC::UNAVAILABLE},
    {someip::ReturnCode::E_E2E_NO_NEW_DATA, SC::FAILED_PRECONDITION},
};

const std::unordered_map<SC, someip::ReturnCode> kGrpcToReturn{
    {SC::OK, someip::ReturnCode::E_OK},
    {SC::CANCELLED, someip::ReturnCode::E_NOT_OK},
    {SC::UNKNOWN, someip::ReturnCode::E_NOT_OK},
    {SC::INVALID_ARGUMENT, someip::ReturnCode::E_MALFORMED_MESSAGE},
    {SC::DEADLINE_EXCEEDED, someip::ReturnCode::E_TIMEOUT},
    {SC::NOT_FOUND, someip::ReturnCode::E_UNKNOWN_SERVICE},
    {SC::ALREADY_EXISTS, someip::ReturnCode::E_NOT_OK},
    {SC::PERMISSION_DENIED, someip::ReturnCode::E_NOT_OK},
    {SC::RESOURCE_EXHAUSTED, someip::ReturnCode::E_NOT_READY},
    {SC::FAILED_PRECONDITION, someip::ReturnCode::E_NOT_READY},
    {SC::ABORTED, someip::ReturnCode::E_E2E},
    {SC::OUT_OF_RANGE, someip::ReturnCode::E_MALFORMED_MESSAGE},
    {SC::UNIMPLEMENTED, someip::ReturnCode::E_UNKNOWN_METHOD},
    {SC::INTERNAL, someip::ReturnCode::E_NOT_OK},
    {SC::UNAVAILABLE, someip::ReturnCode::E_NOT_REACHABLE},
    {SC::DATA_LOSS, someip::ReturnCode::E_E2E},
    {SC::UNAUTHENTICATED, someip::ReturnCode::E_NOT_OK},
};

}  // namespace

::grpc::StatusCode GrpcTranslator::return_code_to_grpc_status_code(someip::ReturnCode code) {
    auto it = kReturnToGrpc.find(code);
    if (it != kReturnToGrpc.end()) {
        return it->second;
    }
    return SC::UNKNOWN;
}

someip::ReturnCode GrpcTranslator::grpc_status_code_to_return_code(::grpc::StatusCode code) {
    auto it = kGrpcToReturn.find(code);
    if (it != kGrpcToReturn.end()) {
        return it->second;
    }
    return someip::ReturnCode::E_NOT_OK;
}

::grpc::Status GrpcTranslator::return_code_to_grpc_status(someip::ReturnCode code,
                                                        const std::string& message) {
    auto sc = return_code_to_grpc_status_code(code);
    std::string msg = message.empty() ? std::string(someip::to_string(code)) : message;
    return ::grpc::Status(static_cast<::grpc::StatusCode>(static_cast<int>(sc)), msg);
}

someip::ReturnCode GrpcTranslator::rpc_result_to_return_code(someip::rpc::RpcResult result) {
    switch (result) {
        case someip::rpc::RpcResult::SUCCESS:
            return someip::ReturnCode::E_OK;
        case someip::rpc::RpcResult::TIMEOUT:
            return someip::ReturnCode::E_TIMEOUT;
        case someip::rpc::RpcResult::NETWORK_ERROR:
            return someip::ReturnCode::E_NOT_REACHABLE;
        case someip::rpc::RpcResult::INVALID_PARAMETERS:
            return someip::ReturnCode::E_MALFORMED_MESSAGE;
        case someip::rpc::RpcResult::METHOD_NOT_FOUND:
            return someip::ReturnCode::E_UNKNOWN_METHOD;
        case someip::rpc::RpcResult::SERVICE_NOT_AVAILABLE:
            return someip::ReturnCode::E_UNKNOWN_SERVICE;
        case someip::rpc::RpcResult::INTERNAL_ERROR:
        default:
            return someip::ReturnCode::E_NOT_OK;
    }
}

::grpc::StatusCode GrpcTranslator::rpc_result_to_grpc_status_code(someip::rpc::RpcResult result) {
    return return_code_to_grpc_status_code(rpc_result_to_return_code(result));
}

bool GrpcTranslator::envelope_to_message(const ::opensomeip::gateway::grpc::v1::SomeIpEnvelope& env,
                                         someip::Message& out) const {
    if (!env.serialized_someip_frame().empty()) {
        std::vector<uint8_t> raw(env.serialized_someip_frame().begin(),
                                 env.serialized_someip_frame().end());
        return out.deserialize(raw, false);
    }

    someip::MessageId mid(static_cast<uint16_t>(env.service_id()),
                          static_cast<uint16_t>(env.method_or_event_id()));
    someip::RequestId rid(static_cast<uint16_t>(env.client_id()),
                          static_cast<uint16_t>(env.session_id()));
    out = someip::Message(mid, rid, static_cast<someip::MessageType>(env.message_type()),
                          static_cast<someip::ReturnCode>(env.return_code()));
    out.set_payload(std::vector<uint8_t>(env.payload().begin(), env.payload().end()));
    return out.has_valid_header();
}

void GrpcTranslator::message_to_envelope(const someip::Message& msg,
                                         ::opensomeip::gateway::grpc::v1::SomeIpEnvelope& env) const {
    env.set_service_id(msg.get_service_id());
    env.set_method_or_event_id(msg.get_method_id());
    env.set_client_id(msg.get_client_id());
    env.set_session_id(msg.get_session_id());
    env.set_message_type(static_cast<uint32_t>(msg.get_message_type()));
    env.set_return_code(static_cast<uint32_t>(msg.get_return_code()));
    env.set_payload(msg.get_payload().data(), msg.get_payload().size());
    env.set_instance_id(0);
}

std::string GrpcTranslator::grpc_status_code_name(::grpc::StatusCode code) {
    std::ostringstream oss;
    oss << static_cast<int>(code);
    return oss.str();
}

}  // namespace grpc
}  // namespace gateway
}  // namespace opensomeip
