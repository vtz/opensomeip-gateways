/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/mqtt/mqtt_gateway.h"
#include "opensomeip/gateway/mqtt/mqtt_translator.h"

#include <gtest/gtest.h>

namespace opensomeip::gateway {

TEST(MqttTranslatorTest, BuildTopicEvent) {
    MqttTranslator translator("vehicle", "WBA123");
    auto topic = translator.build_mqtt_topic(0x1234, 0x0001, 0x8001, false);
    EXPECT_EQ(topic, "vehicle/WBA123/someip/0x1234/0x0001/event/0x8001");
}

TEST(MqttTranslatorTest, BuildTopicRequest) {
    MqttTranslator translator("vehicle", "WBA123");
    auto topic = translator.build_mqtt_topic(0x2000, 0x0001, 0x0001, true);
    EXPECT_EQ(topic, "vehicle/WBA123/someip/0x2000/0x0001/method/0x0001/request");
}

TEST(MqttTranslatorTest, QoSMapping) {
    EXPECT_EQ(MqttTranslator::default_qos_for_message_type(someip::MessageType::NOTIFICATION), 0);
    EXPECT_EQ(MqttTranslator::default_qos_for_message_type(someip::MessageType::REQUEST), 1);
    EXPECT_EQ(MqttTranslator::default_qos_for_message_type(someip::MessageType::RESPONSE), 1);
}

TEST(MqttTranslatorTest, EncodeOutboundRaw) {
    MqttTranslator translator("v", "X");
    someip::MessageId msg_id(0x1234, 0x0001);
    someip::RequestId req_id(0x0001, 0x0001);
    someip::Message msg(msg_id, req_id);
    msg.set_payload({0xAA, 0xBB});

    auto encoded = translator.encode_outbound(msg, MqttPayloadEncoding::RAW);
    EXPECT_EQ(encoded, msg.get_payload());
}

TEST(MqttTranslatorTest, EncodeOutboundJson) {
    MqttTranslator translator("v", "X");
    someip::MessageId msg_id(0x1234, 0x0001);
    someip::RequestId req_id(0x0001, 0x0001);
    someip::Message msg(msg_id, req_id);
    msg.set_payload({0x01});

    auto encoded = translator.encode_outbound(msg, MqttPayloadEncoding::JSON);
    std::string json_str(encoded.begin(), encoded.end());
    EXPECT_NE(json_str.find("service_id"), std::string::npos);
    EXPECT_NE(json_str.find("payload"), std::string::npos);
}

TEST(MqttTranslatorTest, EncodeOutboundSomeipFramed) {
    MqttTranslator translator("v", "X");
    someip::MessageId msg_id(0x1234, 0x0001);
    someip::RequestId req_id(0x0001, 0x0001);
    someip::Message msg(msg_id, req_id);
    msg.set_payload({0x01, 0x02});

    auto encoded = translator.encode_outbound(msg, MqttPayloadEncoding::SOMEIP_FRAMED);
    EXPECT_GE(encoded.size(), someip::Message::get_header_size() + 2);
}

TEST(MqttTranslatorTest, CorrelationData) {
    MqttTranslator translator("v", "X");
    auto corr = translator.build_correlation_data(0x00AB, 0x00CD);
    EXPECT_EQ(corr.size(), 4u);
    EXPECT_EQ(corr[0], 0x00);
    EXPECT_EQ(corr[1], 0xAB);
    EXPECT_EQ(corr[2], 0x00);
    EXPECT_EQ(corr[3], 0xCD);
}

TEST(MqttConfigTest, Defaults) {
    MqttConfig cfg;
    EXPECT_EQ(cfg.broker_uri, "tcp://localhost:1883");
    EXPECT_EQ(cfg.keep_alive_seconds, 60);
    EXPECT_TRUE(cfg.clean_session);
    EXPECT_TRUE(cfg.auto_reconnect);
    EXPECT_EQ(cfg.default_publish_qos, 1);
    EXPECT_FALSE(cfg.tls.enable);
    EXPECT_FALSE(cfg.last_will.enable);
    EXPECT_EQ(cfg.offline_buffer_capacity, 256u);
}

TEST(OfflineBufferTest, PushPop) {
    OfflineMqttRingBuffer buffer(3);

    BufferedMqttPublish a{"topicA", {0x01}, 0, false};
    BufferedMqttPublish b{"topicB", {0x02}, 1, false};
    BufferedMqttPublish c{"topicC", {0x03}, 2, true};

    buffer.push(a);
    buffer.push(b);
    buffer.push(c);
    EXPECT_EQ(buffer.size(), 3u);

    BufferedMqttPublish out;
    EXPECT_TRUE(buffer.pop(out));
    EXPECT_EQ(out.topic, "topicA");

    EXPECT_TRUE(buffer.pop(out));
    EXPECT_EQ(out.topic, "topicB");
}

TEST(OfflineBufferTest, OverflowDropsOldest) {
    OfflineMqttRingBuffer buffer(2);

    buffer.push({"t1", {}, 0, false});
    buffer.push({"t2", {}, 0, false});
    buffer.push({"t3", {}, 0, false});

    EXPECT_EQ(buffer.size(), 2u);

    BufferedMqttPublish out;
    buffer.pop(out);
    EXPECT_EQ(out.topic, "t2");
}

TEST(MqttGatewayTest, Lifecycle) {
    MqttConfig cfg;
    MqttGateway gw(cfg);

    EXPECT_FALSE(gw.is_running());
    EXPECT_EQ(gw.get_protocol(), "mqtt");

    EXPECT_EQ(gw.start(), someip::Result::SUCCESS);
    EXPECT_TRUE(gw.is_running());

    EXPECT_EQ(gw.stop(), someip::Result::SUCCESS);
    EXPECT_FALSE(gw.is_running());
}

TEST(MqttGatewayTest, OnSomeipMessageBuffersWhenDisconnected) {
    MqttConfig cfg;
    MqttGateway gw(cfg);

    ServiceMapping mapping;
    mapping.someip_service_id = 0x1234;
    mapping.someip_instance_id = 0x0001;
    mapping.external_identifier = "test";
    mapping.direction = GatewayDirection::SOMEIP_TO_EXTERNAL;
    gw.add_service_mapping(mapping);

    gw.start();
    gw.test_set_mqtt_connected(false);

    someip::MessageId msg_id(0x1234, 0x8001);
    someip::RequestId req_id(0x0000, 0x0001);
    someip::Message msg(msg_id, req_id, someip::MessageType::NOTIFICATION);
    msg.set_payload({0x01, 0x02});

    auto result = gw.on_someip_message(msg);
    EXPECT_EQ(result, someip::Result::NOT_CONNECTED);
    EXPECT_EQ(gw.offline_buffer_occupancy(), 1u);

    gw.stop();
}

TEST(MqttGatewayTest, OnSomeipMessagePublishesWhenConnected) {
    MqttConfig cfg;
    MqttGateway gw(cfg);

    ServiceMapping mapping;
    mapping.someip_service_id = 0x5678;
    mapping.someip_instance_id = 0x0001;
    mapping.direction = GatewayDirection::BIDIRECTIONAL;
    gw.add_service_mapping(mapping);

    gw.start();
    gw.test_set_mqtt_connected(true);

    someip::MessageId msg_id(0x5678, 0x0001);
    someip::RequestId req_id(0x0001, 0x0001);
    someip::Message msg(msg_id, req_id, someip::MessageType::NOTIFICATION);
    msg.set_payload({0xCA, 0xFE});

    auto result = gw.on_someip_message(msg);
    EXPECT_EQ(result, someip::Result::SUCCESS);

    auto stats = gw.get_stats();
    EXPECT_EQ(stats.messages_someip_to_external, 1u);

    gw.stop();
}

TEST(MqttGatewayTest, FlushOfflineBuffer) {
    MqttConfig cfg;
    MqttGateway gw(cfg);
    gw.start();
    gw.test_set_mqtt_connected(true);

    EXPECT_EQ(gw.flush_offline_buffer(), someip::Result::SUCCESS);
    EXPECT_EQ(gw.offline_buffer_occupancy(), 0u);
    gw.stop();
}

}  // namespace opensomeip::gateway
