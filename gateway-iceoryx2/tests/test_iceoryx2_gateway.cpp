/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/iceoryx2/iceoryx2_gateway.h"
#include "opensomeip/gateway/iceoryx2/iceoryx2_translator.h"

#include <gtest/gtest.h>

namespace opensomeip::gateway::iceoryx2 {

TEST(Iceoryx2ConfigTest, Defaults) {
    Iceoryx2Config cfg;
    EXPECT_EQ(cfg.gateway_name, "iceoryx2-gateway");
    EXPECT_EQ(cfg.service_name_prefix, "someip");
    EXPECT_EQ(cfg.shm.max_sample_bytes, 65536u);
    EXPECT_EQ(cfg.someip_client_id, 0x4200);
    EXPECT_TRUE(cfg.enable_sd_proxy);
    EXPECT_FALSE(cfg.enable_someip_udp_listener);
}

TEST(Iceoryx2TranslatorTest, BuildServiceName) {
    Iceoryx2MessageTranslator translator;
    auto name = translator.build_iceoryx2_service_name("someip", 0x1234, 0x0001);
    EXPECT_EQ(name, "someip/0x1234/0x0001");
}

TEST(Iceoryx2TranslatorTest, EnvelopeRoundTrip) {
    Iceoryx2MessageTranslator translator;

    someip::MessageId msg_id(0x1234, 0x0001);
    someip::RequestId req_id(0x00AB, 0x00CD);
    someip::Message msg(msg_id, req_id, someip::MessageType::REQUEST);
    msg.set_payload({0x10, 0x20, 0x30});

    Iceoryx2Envelope env = translator.someip_to_envelope(msg);
    EXPECT_EQ(env.service_id, 0x1234);
    EXPECT_EQ(env.method_id, 0x0001);
    EXPECT_EQ(env.client_id, 0x00AB);
    EXPECT_EQ(env.session_id, 0x00CD);
    EXPECT_EQ(env.payload.size(), 3u);
    EXPECT_EQ(env.payload[0], 0x10);

    auto reconstructed = translator.envelope_to_someip(env);
    EXPECT_EQ(reconstructed.get_service_id(), 0x1234);
    EXPECT_EQ(reconstructed.get_method_id(), 0x0001);
    EXPECT_EQ(reconstructed.get_client_id(), 0x00AB);
    EXPECT_EQ(reconstructed.get_session_id(), 0x00CD);
    EXPECT_EQ(reconstructed.get_payload(), msg.get_payload());
}

TEST(Iceoryx2TranslatorTest, NotificationEnvelope) {
    Iceoryx2MessageTranslator translator;

    someip::MessageId msg_id(0x5678, 0x8001);
    someip::RequestId req_id(0x0000, 0x0000);
    someip::Message msg(msg_id, req_id, someip::MessageType::NOTIFICATION);
    msg.set_payload({0xDE, 0xAD, 0xBE, 0xEF});

    Iceoryx2Envelope env = translator.someip_to_envelope(msg);
    EXPECT_TRUE(env.is_notification);
    EXPECT_EQ(env.payload.size(), 4u);
}

TEST(Iceoryx2TranslatorTest, SerializeDeserializeEnvelope) {
    Iceoryx2MessageTranslator translator;

    Iceoryx2Envelope env;
    env.service_id = 0xABCD;
    env.method_id = 0x0042;
    env.client_id = 0x0001;
    env.session_id = 0x0002;
    env.is_request = true;
    env.payload = {0x01, 0x02, 0x03, 0x04, 0x05};

    auto bytes = translator.serialize_envelope(env);
    EXPECT_GT(bytes.size(), 5u);

    auto restored = translator.deserialize_envelope(bytes);
    EXPECT_EQ(restored.service_id, env.service_id);
    EXPECT_EQ(restored.method_id, env.method_id);
    EXPECT_EQ(restored.client_id, env.client_id);
    EXPECT_EQ(restored.session_id, env.session_id);
    EXPECT_EQ(restored.is_request, env.is_request);
    EXPECT_EQ(restored.payload, env.payload);
}

TEST(Iceoryx2GatewayTest, Lifecycle) {
    Iceoryx2Config cfg;
    cfg.use_inprocess_shm_simulation = true;
    cfg.enable_someip_udp_listener = false;

    Iceoryx2Gateway gw(cfg);
    EXPECT_FALSE(gw.is_running());
    EXPECT_EQ(gw.get_name(), cfg.gateway_name);
    EXPECT_EQ(gw.get_protocol(), "iceoryx2");

    EXPECT_EQ(gw.start(), someip::Result::SUCCESS);
    EXPECT_TRUE(gw.is_running());

    EXPECT_EQ(gw.stop(), someip::Result::SUCCESS);
    EXPECT_FALSE(gw.is_running());
}

TEST(Iceoryx2GatewayTest, OnSomeipMessageWithMapping) {
    Iceoryx2Config cfg;
    cfg.use_inprocess_shm_simulation = true;

    Iceoryx2Gateway gw(cfg);

    ServiceMapping mapping;
    mapping.someip_service_id = 0x1234;
    mapping.someip_instance_id = 0x0001;
    mapping.external_identifier = "radar/front";
    mapping.direction = GatewayDirection::BIDIRECTIONAL;
    gw.add_service_mapping(mapping);

    std::string captured_name;
    std::vector<uint8_t> captured_sample;
    gw.set_iceoryx2_outbound_hook(
        [&](const std::string& name, const std::vector<uint8_t>& sample) {
            captured_name = name;
            captured_sample = sample;
        });

    gw.start();

    someip::MessageId msg_id(0x1234, 0x0001);
    someip::RequestId req_id(0x0001, 0x0001);
    someip::Message msg(msg_id, req_id, someip::MessageType::NOTIFICATION);
    msg.set_payload({0xCA, 0xFE});

    auto result = gw.on_someip_message(msg);
    EXPECT_EQ(result, someip::Result::SUCCESS);

    EXPECT_FALSE(captured_name.empty());
    EXPECT_FALSE(captured_sample.empty());

    auto stats = gw.get_stats();
    EXPECT_EQ(stats.messages_someip_to_external, 1u);

    gw.stop();
}

TEST(Iceoryx2GatewayTest, InjectIceoryx2Sample) {
    Iceoryx2Config cfg;
    cfg.use_inprocess_shm_simulation = true;

    Iceoryx2Gateway gw(cfg);

    ServiceMapping mapping;
    mapping.someip_service_id = 0x1234;
    mapping.someip_instance_id = 0x0001;
    mapping.external_identifier = "radar/front";
    mapping.direction = GatewayDirection::BIDIRECTIONAL;
    gw.add_service_mapping(mapping);

    gw.start();

    Iceoryx2MessageTranslator translator;
    Iceoryx2Envelope env;
    env.service_id = 0x1234;
    env.method_id = 0x0001;
    env.is_notification = true;
    env.payload = {0xBE, 0xEF};

    auto bytes = translator.serialize_envelope(env);
    gw.inject_iceoryx2_sample("someip/0x1234/0x0001", bytes);

    auto stats = gw.get_stats();
    EXPECT_EQ(stats.messages_external_to_someip, 1u);

    gw.stop();
}

TEST(Iceoryx2GatewayTest, UnmappedServiceIgnored) {
    Iceoryx2Config cfg;
    cfg.use_inprocess_shm_simulation = true;

    Iceoryx2Gateway gw(cfg);
    gw.start();

    someip::MessageId msg_id(0x9999, 0x0001);
    someip::RequestId req_id(0x0001, 0x0001);
    someip::Message msg(msg_id, req_id);

    auto result = gw.on_someip_message(msg);
    EXPECT_EQ(result, someip::Result::SERVICE_NOT_FOUND);

    auto stats = gw.get_stats();
    EXPECT_EQ(stats.messages_someip_to_external, 0u);

    gw.stop();
}

}  // namespace opensomeip::gateway::iceoryx2
