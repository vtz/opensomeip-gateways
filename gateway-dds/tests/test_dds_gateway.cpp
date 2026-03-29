/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/dds/dds_gateway.h"
#include "opensomeip/gateway/dds/dds_translator.h"

#include <gtest/gtest.h>

#include "someip/message.h"

namespace opensomeip::gateway::dds {

TEST(DdsConfigTest, Defaults) {
    DdsConfig cfg;
    EXPECT_EQ(cfg.domain_id, 0);
    EXPECT_FALSE(cfg.participant_name.empty());
    EXPECT_TRUE(cfg.qos_profile_file.empty());
    EXPECT_EQ(cfg.rpc_client_id, 0x7100u);
    EXPECT_FALSE(cfg.enable_sd_client);
    EXPECT_FALSE(cfg.enable_sd_server);
    EXPECT_FALSE(cfg.use_e2e);
    EXPECT_EQ(cfg.someip.default_someip_instance_id, 0x0001u);
}

TEST(DdsTranslatorTest, BuildTopicFlat) {
    const auto t = DdsTranslator::build_dds_topic(0x1234, 0x0001, 0x8001);
    EXPECT_NE(t.find("0x1234"), std::string::npos);
    EXPECT_NE(t.find("0x0001"), std::string::npos);
    EXPECT_NE(t.find("0x8001"), std::string::npos);
}

TEST(DdsTranslatorTest, BuildTopicWithMessageClass) {
    someip::MessageId mid(0x2000, 0x8002);
    someip::RequestId rid(0x0001, 0x0001);
    someip::Message note(mid, rid, someip::MessageType::NOTIFICATION);
    const auto t = DdsTranslator::build_dds_topic(0x2000, 0x0001, 0x8002, note);
    EXPECT_NE(t.find("event"), std::string::npos);

    someip::Message req(mid, rid, someip::MessageType::REQUEST);
    const auto t2 = DdsTranslator::build_dds_topic(0x2000, 0x0001, 0x0002, req);
    EXPECT_NE(t2.find("method"), std::string::npos);
}

TEST(DdsTranslatorTest, QosMappingNotificationVsRpc) {
    someip::MessageId mid(0x1, 0x2);
    someip::RequestId rid(0x3, 0x4);

    someip::Message note(mid, rid, someip::MessageType::NOTIFICATION);
    const auto qn = DdsTranslator::qos_for_someip_message(note, false);
    EXPECT_EQ(qn.reliability, DdsReliabilityKind::BestEffort);

    someip::Message request(mid, rid, someip::MessageType::REQUEST);
    const auto qr = DdsTranslator::qos_for_someip_message(request, false);
    EXPECT_EQ(qr.reliability, DdsReliabilityKind::Reliable);
}

TEST(DdsTranslatorTest, QosMappingServiceMapping) {
    ServiceMapping m;
    m.someip_event_group_ids = {0x01};
    m.someip_method_ids = {};
    m.direction = GatewayDirection::SOMEIP_TO_EXTERNAL;
    const auto qw = DdsTranslator::qos_for_service_mapping(m, true);
    EXPECT_EQ(qw.reliability, DdsReliabilityKind::BestEffort);

    m.someip_method_ids = {0x10};
    m.someip_event_group_ids = {};
    const auto qw2 = DdsTranslator::qos_for_service_mapping(m, true);
    EXPECT_EQ(qw2.reliability, DdsReliabilityKind::Reliable);
}

TEST(DdsTranslatorTest, EncodeDecodeOpaque) {
    someip::MessageId mid(0x3333, 0x4444);
    someip::RequestId rid(0x0001, 0x0002);
    someip::Message msg(mid, rid, someip::MessageType::REQUEST);
    msg.set_payload({0xDE, 0xAD});

    auto enc = DdsTranslator::encode_outbound(msg, TranslationMode::OPAQUE);
    EXPECT_EQ(enc, msg.get_payload());

    someip::Message round;
    const std::vector<uint8_t> wire = msg.serialize();
    ASSERT_TRUE(DdsTranslator::decode_inbound(wire, TranslationMode::OPAQUE, round));
    EXPECT_EQ(round.get_service_id(), msg.get_service_id());
    EXPECT_EQ(round.get_method_id(), msg.get_method_id());
}

TEST(DdsGatewayLifecycleTest, StartStop) {
    DdsConfig cfg;
    DdsGateway gw(cfg);
    const auto r = gw.start();
    if (r != someip::Result::SUCCESS) {
        GTEST_SKIP() << "Cyclone DDS participant creation failed (environment or install).";
    }
    EXPECT_TRUE(gw.is_running());
    EXPECT_EQ(gw.stop(), someip::Result::SUCCESS);
    EXPECT_FALSE(gw.is_running());
}

TEST(DdsGatewayForwardingTest, SomeipToDdsStats) {
    DdsConfig cfg;
    DdsGateway gw(cfg);

    ServiceMapping m;
    m.someip_service_id = 0x6001;
    m.someip_instance_id = 0x0001;
    m.someip_method_ids = {0x8001};
    m.direction = GatewayDirection::SOMEIP_TO_EXTERNAL;
    gw.add_service_mapping(m);

    const auto sr = gw.start();
    if (sr != someip::Result::SUCCESS) {
        GTEST_SKIP() << "Cyclone DDS not available.";
    }

    someip::MessageId mid(0x6001, 0x8001);
    someip::RequestId rid(0x0001, 0x0001);
    someip::Message msg(mid, rid, someip::MessageType::NOTIFICATION);
    msg.set_payload({0x01});

    const auto pr = gw.on_someip_message(msg);
    EXPECT_EQ(pr, someip::Result::SUCCESS);

    const auto stats = gw.get_stats();
    EXPECT_GE(stats.messages_someip_to_external, 1u);
    gw.stop();
}

TEST(DdsGatewayInjectTest, InboundWithoutStart) {
    DdsGateway gw(DdsConfig{});
    std::vector<uint8_t> p = {0xAB};
    EXPECT_EQ(gw.inject_dds_sample("", p, 0x6001, 0x0001, 0x0001,
                                   static_cast<uint8_t>(someip::MessageType::RESPONSE)),
              someip::Result::SUCCESS);
    const auto st = gw.get_stats();
    EXPECT_GE(st.messages_external_to_someip, 1u);
}

}  // namespace opensomeip::gateway::dds
