/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include <gtest/gtest.h>

#include "opensomeip/gateway/zenoh/zenoh_gateway.h"
#include "opensomeip/gateway/zenoh/zenoh_translator.h"
#include "someip/message.h"

namespace opensomeip::gateway::zenoh {

TEST(ZenohTranslatorKeys, RpcEventAndLivelinessRoundTrip) {
    const std::string prefix = "vehicle/ecu1/someip";
    const std::string rpc = ZenohTranslator::build_rpc_key(prefix, 0x1234, 1, 0x0042);
    EXPECT_EQ(rpc, "vehicle/ecu1/someip/0x1234/0x0001/rpc/0x0042");

    const std::string ev = ZenohTranslator::build_key_expr(prefix, 0x1234, 1, 0x8001, false);
    EXPECT_EQ(ev, "vehicle/ecu1/someip/0x1234/0x0001/0x8001/event");

    const std::string liv = ZenohTranslator::build_liveliness_key(prefix, 0x1234, 1);
    EXPECT_EQ(liv, "vehicle/ecu1/someip/liveliness/0x1234/0x0001");

    auto p = ZenohTranslator::parse_someip_key(rpc, prefix);
    ASSERT_TRUE(p.valid);
    EXPECT_TRUE(p.is_rpc_path);
    EXPECT_EQ(p.service_id, 0x1234);
    EXPECT_EQ(p.instance_id, 1);
    EXPECT_EQ(p.method_or_event_id, 0x0042);

    auto pe = ZenohTranslator::parse_someip_key(ev, prefix);
    ASSERT_TRUE(pe.valid);
    EXPECT_FALSE(pe.is_rpc_path);
    EXPECT_EQ(pe.method_or_event_id, 0x8001);

    auto pl = ZenohTranslator::parse_someip_key(liv, prefix);
    ASSERT_TRUE(pl.valid);
    EXPECT_TRUE(pl.is_liveliness);
}

TEST(ZenohTranslatorKeys, InstancePatternMatchesZenohWildcardRules) {
    const std::string pat = ZenohTranslator::build_instance_pattern("demo", 0xABCD, 0x0001);
    EXPECT_EQ(pat, "demo/0xabcd/0x0001/**");
}

TEST(ZenohConfig, Json5ContainsModeConnectListenAndShm) {
    ZenohConfig c;
    c.mode = ZenohSessionMode::CLIENT;
    c.connect_endpoints.push_back("tcp/edge-zenoh:7447");
    c.listen_endpoints.push_back("tcp/0.0.0.0:0");
    c.shm.enabled = true;
    c.shm.threshold_bytes = 4096;
    const std::string j = c.to_json5();
    EXPECT_NE(j.find("\"mode\":\"client\""), std::string::npos);
    EXPECT_NE(j.find("edge-zenoh:7447"), std::string::npos);
    EXPECT_NE(j.find("\"enabled\":true"), std::string::npos);
    EXPECT_NE(j.find("\"threshold\":4096"), std::string::npos);
}

TEST(ZenohTranslatorPayload, CborRoundTripPreservesPayload) {
    ZenohTranslator t(ZenohPayloadEncoding::CBOR);
    someip::Message m(someip::MessageId(0x1111, 0x2222), someip::RequestId(0x3333, 0x4444),
                      someip::MessageType::NOTIFICATION);
    m.set_payload(std::vector<uint8_t>{0x01, 0x02, 0xDE, 0xAD});
    auto enc = t.encode_payload(m);
    someip::Message out;
    ASSERT_TRUE(t.decode_payload(enc, out));
    EXPECT_EQ(out.get_payload(), m.get_payload());
}

TEST(SomeipMessageApi, SerializeDeserializeRoundTrip) {
    someip::Message m(someip::MessageId(0x6003, 0x0001), someip::RequestId(0x0042, 0x0007),
                    someip::MessageType::REQUEST, someip::ReturnCode::E_OK);
    m.set_payload(std::vector<uint8_t>{9, 8, 7});
    auto bytes = m.serialize();
    someip::Message m2;
    ASSERT_TRUE(m2.deserialize(bytes, false));
    EXPECT_EQ(m2.get_service_id(), 0x6003);
    EXPECT_EQ(m2.get_method_id(), 0x0001);
    EXPECT_EQ(m2.get_client_id(), 0x0042);
    EXPECT_EQ(m2.get_session_id(), 0x0007);
    EXPECT_EQ(m2.get_message_type(), someip::MessageType::REQUEST);
    EXPECT_EQ(m2.get_payload(), m.get_payload());
}

TEST(MessageTranslatorBase, BuildTopicMatchesHexConvention) {
    MessageTranslator tr;
    const std::string topic = MessageTranslator::build_topic("fleet", 0x100, 0x1, 0x8001);
    EXPECT_EQ(topic, "fleet/0x0100/0x0001/0x8001");
}

}  // namespace opensomeip::gateway::zenoh
