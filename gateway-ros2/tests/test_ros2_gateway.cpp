/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/ros2/ros2_gateway.h"
#include "opensomeip/gateway/ros2/ros2_translator.h"

#include <gtest/gtest.h>

namespace opensomeip::gateway::ros2 {

TEST(Ros2ConfigTest, Defaults) {
    Ros2Config cfg;
    EXPECT_EQ(cfg.node_name, "opensomeip_ros2_gateway");
    EXPECT_EQ(cfg.ros_namespace, "/vehicle");
    EXPECT_EQ(cfg.someip_bind_address, "0.0.0.0");
    EXPECT_EQ(cfg.someip_bind_port, 30500u);
    EXPECT_EQ(cfg.rpc_client_id, 0x5100u);
    EXPECT_EQ(cfg.default_someip_instance_id, 0x0001u);
    EXPECT_EQ(cfg.topic_prefix, "someip");
    EXPECT_FALSE(cfg.enable_udp_transport);
    EXPECT_TRUE(cfg.enable_rpc_client);
    EXPECT_FALSE(cfg.enable_rpc_server);
    EXPECT_FALSE(cfg.use_e2e);
}

TEST(Ros2TranslatorTest, BuildRos2Topic) {
    Ros2Translator tr("someip", "/adas");
    const auto topic = tr.build_ros2_topic(0x6001, 0x0001, 0x8001);
    EXPECT_EQ(topic, "/adas/someip/0x6001/0x0001/event/0x8001");
}

TEST(Ros2TranslatorTest, BuildRos2TopicNoNamespace) {
    Ros2Translator tr("gw", "");
    const auto topic = tr.build_ros2_topic(0x1000, 0x0002, 0x8003);
    EXPECT_EQ(topic, "gw/0x1000/0x0002/event/0x8003");
}

TEST(Ros2TranslatorTest, QosMapping) {
    Ros2Translator tr("p", "/n");
    const auto udp = tr.qos_for_someip_transport(SomeipTransportKind::UDP_UNICAST);
    EXPECT_FALSE(udp.reliable);
    EXPECT_EQ(udp.history_depth, 5u);

    const auto tcp = tr.qos_for_someip_transport(SomeipTransportKind::TCP);
    EXPECT_TRUE(tcp.reliable);
    EXPECT_GE(tcp.history_depth, 10u);
}

TEST(Ros2TranslatorTest, PayloadRoundTrip) {
    Ros2Translator tr("p", "/n");
    someip::MessageId mid(0x1111, 0x2222);
    someip::RequestId rid(1, 1);
    someip::Message msg(mid, rid, someip::MessageType::NOTIFICATION, someip::ReturnCode::E_OK);
    msg.set_payload({0x01, 0x02, 0x03});

    const auto bytes = tr.convert_someip_to_ros2_bytes(msg);
    someip::Message out(mid, rid, someip::MessageType::REQUEST, someip::ReturnCode::E_OK);
    ASSERT_TRUE(tr.convert_ros2_bytes_to_someip(bytes, out));
    EXPECT_EQ(out.get_payload(), bytes);
}

TEST(Ros2GatewayTest, Lifecycle) {
    Ros2Config cfg;
    cfg.enable_rpc_client = false;
    cfg.enable_event_subscriber = false;
    Ros2Gateway gw(cfg);

    EXPECT_FALSE(gw.is_running());
    EXPECT_EQ(gw.get_protocol(), "ros2");

    EXPECT_EQ(gw.start(), someip::Result::SUCCESS);
    EXPECT_TRUE(gw.is_running());

    EXPECT_EQ(gw.stop(), someip::Result::SUCCESS);
    EXPECT_FALSE(gw.is_running());
}

TEST(Ros2GatewayTest, ForwardsSomeipToRos2WithMapping) {
    Ros2Config cfg;
    cfg.enable_rpc_client = false;
    cfg.enable_event_subscriber = false;
    Ros2Gateway gw(cfg);

    ServiceMapping mapping;
    mapping.someip_service_id = 0x6001;
    mapping.someip_instance_id = 0x0001;
    mapping.external_identifier = "/test/speed";
    mapping.direction = GatewayDirection::SOMEIP_TO_EXTERNAL;
    gw.add_service_mapping(mapping);

    std::string seen_topic;
    std::vector<uint8_t> seen_payload;
    gw.set_ros2_publish_callback([&](const std::string& t, const std::vector<uint8_t>& p) {
        seen_topic = t;
        seen_payload = p;
    });

    ASSERT_EQ(gw.start(), someip::Result::SUCCESS);

    someip::MessageId mid(0x6001, 0x8001);
    someip::RequestId rid(0x0100, 0x0200);
    someip::Message msg(mid, rid, someip::MessageType::NOTIFICATION, someip::ReturnCode::E_OK);
    msg.set_payload({0xDE, 0xAD});

    EXPECT_EQ(gw.on_someip_message(msg), someip::Result::SUCCESS);
    EXPECT_EQ(seen_topic, "/test/speed");
    EXPECT_EQ(seen_payload, (std::vector<uint8_t>{0xDE, 0xAD}));

    const auto stats = gw.get_stats();
    EXPECT_GE(stats.messages_someip_to_external, 1u);

    gw.stop();
}

TEST(Ros2GatewayTest, UnmappedServiceReturnsNotFound) {
    Ros2Config cfg;
    cfg.enable_rpc_client = false;
    cfg.enable_event_subscriber = false;
    Ros2Gateway gw(cfg);
    ASSERT_EQ(gw.start(), someip::Result::SUCCESS);

    someip::MessageId mid(0xFFFF, 0x0001);
    someip::RequestId rid(1, 1);
    someip::Message msg(mid, rid, someip::MessageType::REQUEST, someip::ReturnCode::E_OK);

    EXPECT_EQ(gw.on_someip_message(msg), someip::Result::SERVICE_NOT_FOUND);
    gw.stop();
}

TEST(Ros2GatewayTest, InjectRos2UsesRpcOrSink) {
    Ros2Config cfg;
    cfg.enable_rpc_client = true;
    cfg.enable_event_subscriber = false;
    Ros2Gateway gw(cfg);

    ServiceMapping mapping;
    mapping.someip_service_id = 0x7001;
    mapping.someip_instance_id = 0x0001;
    mapping.external_identifier = "/cmd/steer";
    mapping.someip_method_ids = {0x0005};
    mapping.direction = GatewayDirection::EXTERNAL_TO_SOMEIP;
    gw.add_service_mapping(mapping);

    ASSERT_EQ(gw.start(), someip::Result::SUCCESS);

    EXPECT_EQ(gw.inject_ros2_message("/cmd/steer", {0x01}), someip::Result::SUCCESS);

    gw.stop();
}

}  // namespace opensomeip::gateway::ros2
