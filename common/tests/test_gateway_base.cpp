/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/gateway_base.h"

#include <gtest/gtest.h>

namespace opensomeip::gateway {

class TestGateway : public GatewayBase {
public:
    TestGateway() : GatewayBase("test-gateway", "test-protocol") {
    }

    someip::Result start() override {
        set_running(true);
        return someip::Result::SUCCESS;
    }

    someip::Result stop() override {
        set_running(false);
        return someip::Result::SUCCESS;
    }

    someip::Result on_someip_message(const someip::Message& msg) override {
        record_someip_to_external(msg.get_payload().size());
        return someip::Result::SUCCESS;
    }
};

TEST(GatewayBaseTest, InitialState) {
    TestGateway gw;
    EXPECT_FALSE(gw.is_running());
    EXPECT_EQ(gw.get_name(), "test-gateway");
    EXPECT_EQ(gw.get_protocol(), "test-protocol");

    auto stats = gw.get_stats();
    EXPECT_EQ(stats.messages_someip_to_external, 0u);
    EXPECT_EQ(stats.messages_external_to_someip, 0u);
    EXPECT_EQ(stats.translation_errors, 0u);
}

TEST(GatewayBaseTest, StartStop) {
    TestGateway gw;
    EXPECT_EQ(gw.start(), someip::Result::SUCCESS);
    EXPECT_TRUE(gw.is_running());

    EXPECT_EQ(gw.stop(), someip::Result::SUCCESS);
    EXPECT_FALSE(gw.is_running());
}

TEST(GatewayBaseTest, StatsTracking) {
    TestGateway gw;
    gw.start();

    someip::MessageId msg_id(0x1234, 0x0001);
    someip::RequestId req_id(0x0001, 0x0001);
    someip::Message msg(msg_id, req_id);
    msg.set_payload({0x01, 0x02, 0x03});

    gw.on_someip_message(msg);
    gw.on_someip_message(msg);

    auto stats = gw.get_stats();
    EXPECT_EQ(stats.messages_someip_to_external, 2u);
    EXPECT_EQ(stats.bytes_someip_to_external, 6u);
    EXPECT_GT(stats.uptime().count(), 0);
}

TEST(GatewayBaseTest, ServiceMappings) {
    TestGateway gw;

    ServiceMapping mapping;
    mapping.someip_service_id = 0x1234;
    mapping.someip_instance_id = 0x0001;
    mapping.external_identifier = "test/service";
    mapping.direction = GatewayDirection::BIDIRECTIONAL;

    gw.add_service_mapping(mapping);
    EXPECT_EQ(gw.get_service_mappings().size(), 1u);
    EXPECT_EQ(gw.get_service_mappings()[0].external_identifier, "test/service");
}

TEST(GatewayBaseTest, FindMapping) {
    TestGateway gw;

    ServiceMapping m1;
    m1.someip_service_id = 0x1234;
    m1.someip_instance_id = 0x0001;
    m1.external_identifier = "radar";

    ServiceMapping m2;
    m2.someip_service_id = 0x5678;
    m2.someip_instance_id = 0x0002;
    m2.external_identifier = "camera";

    gw.add_service_mapping(m1);
    gw.add_service_mapping(m2);

    auto* found = gw.find_mapping_for_service(0x1234, 0x0001);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->external_identifier, "radar");

    auto* not_found = gw.find_mapping_for_service(0x9999, 0x0001);
    EXPECT_EQ(not_found, nullptr);
}

TEST(GatewayBaseTest, DirectionFiltering) {
    TestGateway gw;

    ServiceMapping forward_only;
    forward_only.direction = GatewayDirection::SOMEIP_TO_EXTERNAL;

    ServiceMapping reverse_only;
    reverse_only.direction = GatewayDirection::EXTERNAL_TO_SOMEIP;

    ServiceMapping bidir;
    bidir.direction = GatewayDirection::BIDIRECTIONAL;

    EXPECT_TRUE(gw.should_forward_to_external(forward_only));
    EXPECT_FALSE(gw.should_forward_to_someip(forward_only));

    EXPECT_FALSE(gw.should_forward_to_external(reverse_only));
    EXPECT_TRUE(gw.should_forward_to_someip(reverse_only));

    EXPECT_TRUE(gw.should_forward_to_external(bidir));
    EXPECT_TRUE(gw.should_forward_to_someip(bidir));
}

TEST(GatewayBaseTest, ExternalMessageCallback) {
    TestGateway gw;

    bool callback_invoked = false;
    uint16_t received_service_id = 0;

    gw.set_external_message_callback(
        [&](uint16_t service_id, uint16_t, const std::vector<uint8_t>&) {
            callback_invoked = true;
            received_service_id = service_id;
        });

    gw.external_message_callback_(0x1234, 0x0001, {0x01});
    EXPECT_TRUE(callback_invoked);
    EXPECT_EQ(received_service_id, 0x1234);
}

}  // namespace opensomeip::gateway
