/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/grpc/grpc_translator.h"

#include <gtest/gtest.h>

namespace opensomeip::gateway::grpc {

TEST(GrpcTranslatorTest, SomeipReturnCodeToGrpcStatus) {
    GrpcTranslator translator;

    EXPECT_EQ(translator.someip_to_grpc_status(someip::ReturnCode::E_OK), 0);
    EXPECT_EQ(translator.someip_to_grpc_status(someip::ReturnCode::E_NOT_OK), 13);
    EXPECT_EQ(translator.someip_to_grpc_status(someip::ReturnCode::E_UNKNOWN_SERVICE), 5);
    EXPECT_EQ(translator.someip_to_grpc_status(someip::ReturnCode::E_UNKNOWN_METHOD), 12);
    EXPECT_EQ(translator.someip_to_grpc_status(someip::ReturnCode::E_NOT_READY), 14);
    EXPECT_EQ(translator.someip_to_grpc_status(someip::ReturnCode::E_MALFORMED_MESSAGE), 3);
}

TEST(GrpcTranslatorTest, GrpcStatusToSomeipReturnCode) {
    GrpcTranslator translator;

    EXPECT_EQ(translator.grpc_to_someip_return_code(0), someip::ReturnCode::E_OK);
    EXPECT_EQ(translator.grpc_to_someip_return_code(13), someip::ReturnCode::E_NOT_OK);
    EXPECT_EQ(translator.grpc_to_someip_return_code(5), someip::ReturnCode::E_UNKNOWN_SERVICE);
    EXPECT_EQ(translator.grpc_to_someip_return_code(12), someip::ReturnCode::E_UNKNOWN_METHOD);
    EXPECT_EQ(translator.grpc_to_someip_return_code(14), someip::ReturnCode::E_NOT_READY);
    EXPECT_EQ(translator.grpc_to_someip_return_code(4), someip::ReturnCode::E_NOT_OK);
}

TEST(GrpcTranslatorTest, WrapSomeipForGrpc) {
    GrpcTranslator translator;

    someip::MessageId msg_id(0x1234, 0x0001);
    someip::RequestId req_id(0x00AB, 0x00CD);
    someip::Message msg(msg_id, req_id, someip::MessageType::REQUEST);
    msg.set_payload({0xCA, 0xFE, 0xBA, 0xBE});

    auto wire = translator.wrap_someip_for_grpc(msg);
    EXPECT_GT(wire.size(), 4u);

    auto restored = translator.unwrap_grpc_to_someip(wire);
    EXPECT_EQ(restored.get_service_id(), 0x1234);
    EXPECT_EQ(restored.get_method_id(), 0x0001);
    EXPECT_EQ(restored.get_payload().size(), 4u);
    EXPECT_EQ(restored.get_payload()[0], 0xCA);
}

TEST(GrpcConfigTest, Defaults) {
    GrpcConfig cfg;
    EXPECT_EQ(cfg.server_listen_address, "0.0.0.0:50051");
    EXPECT_FALSE(cfg.server_tls.enabled);
    EXPECT_TRUE(cfg.cloud_targets.empty());
    EXPECT_EQ(cfg.max_concurrent_streams, 100);
    EXPECT_EQ(cfg.someip_bridge_client_id, 0x7001);
}

}  // namespace opensomeip::gateway::grpc
