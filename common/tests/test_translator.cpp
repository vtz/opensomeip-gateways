/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/translator.h"

#include <gtest/gtest.h>

namespace opensomeip::gateway {

TEST(TranslatorTest, SomeipToExternal) {
    MessageTranslator translator;

    someip::MessageId msg_id(0x1234, 0x0001);
    someip::RequestId req_id(0x00AB, 0x00CD);
    someip::Message msg(msg_id, req_id, someip::MessageType::REQUEST);
    msg.set_payload({0x01, 0x02, 0x03, 0x04});

    auto ext = translator.someip_to_external(msg, "radar/objects");

    EXPECT_EQ(ext.topic_or_key, "radar/objects");
    EXPECT_EQ(ext.payload.size(), 4u);
    EXPECT_EQ(ext.payload[0], 0x01);
    EXPECT_EQ(ext.source_service_id, 0x1234);
    EXPECT_EQ(ext.source_method_id, 0x0001);
    EXPECT_TRUE(ext.is_request);
    EXPECT_FALSE(ext.is_notification);
    EXPECT_EQ(ext.correlation_id, "00ab-00cd");
}

TEST(TranslatorTest, SomeipNotificationToExternal) {
    MessageTranslator translator;

    someip::MessageId msg_id(0x1234, 0x8001);
    someip::RequestId req_id(0x0000, 0x0000);
    someip::Message msg(msg_id, req_id, someip::MessageType::NOTIFICATION);
    msg.set_payload({0xDE, 0xAD});

    auto ext = translator.someip_to_external(msg, "vehicle/speed");

    EXPECT_TRUE(ext.is_notification);
    EXPECT_FALSE(ext.is_request);
    EXPECT_EQ(ext.payload.size(), 2u);
}

TEST(TranslatorTest, ExternalToSomeip) {
    MessageTranslator translator;

    ExternalMessage ext;
    ext.topic_or_key = "radar/objects";
    ext.payload = {0x05, 0x06, 0x07};

    auto msg = translator.external_to_someip(ext, 0x1234, 0x0001,
                                              someip::MessageType::REQUEST);

    EXPECT_EQ(msg.get_service_id(), 0x1234);
    EXPECT_EQ(msg.get_method_id(), 0x0001);
    EXPECT_EQ(msg.get_message_type(), someip::MessageType::REQUEST);
    EXPECT_EQ(msg.get_payload().size(), 3u);
    EXPECT_EQ(msg.get_payload()[0], 0x05);
}

TEST(TranslatorTest, BuildTopic) {
    auto topic = MessageTranslator::build_topic("vehicle", 0x1234, 0x0001, 0x8001);
    EXPECT_EQ(topic, "vehicle/0x1234/0x0001/0x8001");
}

TEST(TranslatorTest, FormatServiceId) {
    EXPECT_EQ(MessageTranslator::format_service_id(0x1234), "0x1234");
    EXPECT_EQ(MessageTranslator::format_service_id(0x0001), "0x0001");
    EXPECT_EQ(MessageTranslator::format_service_id(0xFFFF), "0xffff");
}

TEST(TranslatorTest, PayloadToJson) {
    someip::MessageId msg_id(0x1234, 0x0001);
    someip::RequestId req_id(0x0001, 0x0002);
    someip::Message msg(msg_id, req_id, someip::MessageType::REQUEST);
    msg.set_payload({0x01, 0x02});

    auto json = MessageTranslator::payload_to_json(msg);
    std::string json_str(json.begin(), json.end());

    EXPECT_NE(json_str.find("\"service_id\":4660"), std::string::npos);
    EXPECT_NE(json_str.find("\"method_id\":1"), std::string::npos);
    EXPECT_NE(json_str.find("\"payload\":[1,2]"), std::string::npos);
}

TEST(TranslatorTest, RoundTripPreservesPayload) {
    MessageTranslator translator;

    std::vector<uint8_t> original_payload = {0x10, 0x20, 0x30, 0x40, 0x50};

    someip::MessageId msg_id(0xABCD, 0x0042);
    someip::RequestId req_id(0x0001, 0x0001);
    someip::Message original(msg_id, req_id, someip::MessageType::REQUEST);
    original.set_payload(original_payload);

    auto ext = translator.someip_to_external(original, "test/roundtrip");
    auto reconstructed = translator.external_to_someip(
        ext, 0xABCD, 0x0042, someip::MessageType::REQUEST);

    EXPECT_EQ(reconstructed.get_payload(), original_payload);
}

}  // namespace opensomeip::gateway
