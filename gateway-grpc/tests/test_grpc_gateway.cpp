/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

// Default build of this TU only needs grpc_config.h (no gRPC/protobuf headers).
// GrpcTranslator and GrpcGateway tests are compiled only when HAVE_GRPC is defined
// (see gateway-grpc/CMakeLists.txt when gRPC and Protobuf are found).

#include "opensomeip/gateway/grpc/grpc_config.h"

#include <gtest/gtest.h>

#ifdef HAVE_GRPC
#include "opensomeip/gateway/grpc/grpc_gateway.h"
#include "opensomeip/gateway/grpc/grpc_translator.h"
#endif

namespace opensomeip::gateway::grpc {

namespace {

void expect_default_grpc_config_core_fields(const GrpcConfigCore& cfg) {
    EXPECT_EQ(cfg.gateway_name, "grpc-gateway");
    EXPECT_EQ(cfg.server_listen_address, "0.0.0.0:50051");
    EXPECT_FALSE(cfg.server_tls.enabled);
    EXPECT_TRUE(cfg.cloud_targets.empty());
    EXPECT_EQ(cfg.max_concurrent_streams, 100);
    EXPECT_EQ(cfg.someip_bridge_client_id, 0x7001);
    EXPECT_EQ(cfg.rpc_response_timeout.count(), 5000);
    EXPECT_FALSE(cfg.enable_sd_client);
    EXPECT_FALSE(cfg.enable_sd_server);
    EXPECT_FALSE(cfg.enable_e2e_bridge);
}

}  // namespace

// Always available: validates default field values shared by GrpcConfig / GrpcConfigCore.
TEST(GrpcConfigTest, CoreDefaultsMatchGrpcConfigSemantics) {
    GrpcConfigCore cfg;
    expect_default_grpc_config_core_fields(cfg);
}

#ifdef HAVE_GRPC

TEST(GrpcConfigTest, FullGrpcConfigInheritsCoreDefaults) {
    GrpcConfig cfg;
    expect_default_grpc_config_core_fields(cfg);
}

TEST(GrpcTranslatorTest, ReturnCodeToGrpcStatusCode) {
    using SC = ::grpc::StatusCode;
    EXPECT_EQ(GrpcTranslator::return_code_to_grpc_status_code(someip::ReturnCode::E_OK), SC::OK);
    EXPECT_EQ(GrpcTranslator::return_code_to_grpc_status_code(someip::ReturnCode::E_NOT_OK),
              SC::INTERNAL);
    EXPECT_EQ(GrpcTranslator::return_code_to_grpc_status_code(someip::ReturnCode::E_UNKNOWN_SERVICE),
              SC::NOT_FOUND);
    EXPECT_EQ(GrpcTranslator::return_code_to_grpc_status_code(someip::ReturnCode::E_UNKNOWN_METHOD),
              SC::UNIMPLEMENTED);
    EXPECT_EQ(GrpcTranslator::return_code_to_grpc_status_code(someip::ReturnCode::E_NOT_READY),
              SC::FAILED_PRECONDITION);
    EXPECT_EQ(GrpcTranslator::return_code_to_grpc_status_code(someip::ReturnCode::E_MALFORMED_MESSAGE),
              SC::INVALID_ARGUMENT);
}

TEST(GrpcTranslatorTest, GrpcStatusCodeToReturnCode) {
    using SC = ::grpc::StatusCode;
    EXPECT_EQ(GrpcTranslator::grpc_status_code_to_return_code(SC::OK), someip::ReturnCode::E_OK);
    EXPECT_EQ(GrpcTranslator::grpc_status_code_to_return_code(SC::INTERNAL),
              someip::ReturnCode::E_NOT_OK);
    EXPECT_EQ(GrpcTranslator::grpc_status_code_to_return_code(SC::NOT_FOUND),
              someip::ReturnCode::E_UNKNOWN_SERVICE);
    EXPECT_EQ(GrpcTranslator::grpc_status_code_to_return_code(SC::UNIMPLEMENTED),
              someip::ReturnCode::E_UNKNOWN_METHOD);
    EXPECT_EQ(GrpcTranslator::grpc_status_code_to_return_code(SC::UNAVAILABLE),
              someip::ReturnCode::E_NOT_REACHABLE);
    EXPECT_EQ(GrpcTranslator::grpc_status_code_to_return_code(SC::DEADLINE_EXCEEDED),
              someip::ReturnCode::E_TIMEOUT);
    EXPECT_EQ(GrpcTranslator::grpc_status_code_to_return_code(::grpc::StatusCode::DO_NOT_USE),
              someip::ReturnCode::E_NOT_OK);
}

TEST(GrpcTranslatorTest, MessageEnvelopeRoundTrip) {
    GrpcTranslator translator;

    someip::MessageId msg_id(0x1234, 0x0001);
    someip::RequestId req_id(0x00AB, 0x00CD);
    someip::Message msg(msg_id, req_id, someip::MessageType::REQUEST);
    msg.set_payload({0xCA, 0xFE, 0xBA, 0xBE});

    ::opensomeip::gateway::grpc::v1::SomeIpEnvelope env;
    translator.message_to_envelope(msg, env);

    someip::Message restored;
    ASSERT_TRUE(translator.envelope_to_message(env, restored));
    EXPECT_EQ(restored.get_service_id(), 0x1234);
    EXPECT_EQ(restored.get_method_id(), 0x0001);
    EXPECT_EQ(restored.get_payload().size(), 4u);
    EXPECT_EQ(restored.get_payload()[0], 0xCA);
}

TEST(GrpcGatewayTest, ConstructsWithDefaultConfig) {
    GrpcConfig cfg;
    GrpcGateway gateway(cfg);
    EXPECT_EQ(gateway.get_name(), cfg.gateway_name);
    EXPECT_EQ(gateway.config().server_listen_address, "0.0.0.0:50051");
}

#endif  // HAVE_GRPC

}  // namespace opensomeip::gateway::grpc
