/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/dbus/dbus_gateway.h"
#include "opensomeip/gateway/dbus/dbus_translator.h"

#include <gtest/gtest.h>

#include "someip/message.h"
#include "someip/types.h"

namespace opensomeip::gateway::dbus {

TEST(DbusTranslatorTest, BuildBusName) {
    DbusTranslator tr("com.example", "/com/example");
    EXPECT_EQ(tr.build_bus_name(0x1234, 0x0001), "com.example.svc.1234.inst.0001");
}

TEST(DbusTranslatorTest, BuildObjectPath) {
    DbusTranslator tr("com.example", "/com/example");
    EXPECT_EQ(tr.build_object_path(0x1234, 0x0001), "/com/example/svc_1234/inst_0001");
}

TEST(DbusTranslatorTest, BuildInterfaceName) {
    EXPECT_EQ(DbusTranslator::build_interface_name(0xABCD), "com.opensomeip.Service.abcd");
}

TEST(DbusTranslatorTest, TypeMapping) {
    EXPECT_EQ(DbusTranslator::someip_type_to_dbus_signature("uint16"), "q");
    EXPECT_EQ(DbusTranslator::someip_type_to_dbus_signature("Float"), "d");
    EXPECT_EQ(DbusTranslator::someip_type_to_dbus_signature("string"), "s");
    EXPECT_EQ(DbusTranslator::someip_type_to_dbus_signature("unknown_blob"), "ay");
}

TEST(DbusTranslatorTest, SignatureFromTypes) {
    auto sig = DbusTranslator::build_signature_from_someip_types({"uint32", "double"});
    EXPECT_EQ(sig, "ud");
}

TEST(DbusTranslatorTest, IntrospectionXml) {
    DbusTranslator tr("com.opensomeip", "/com/opensomeip");
    auto xml = tr.generate_introspection_xml(0x2000, {"uint16", "uint16"}, true);
    EXPECT_NE(xml.find("com.opensomeip.Service.2000"), std::string::npos);
    EXPECT_NE(xml.find("InvokeRpc"), std::string::npos);
    EXPECT_NE(xml.find("SomeipNotification"), std::string::npos);
    EXPECT_NE(xml.find("typed SOME/IP layout"), std::string::npos);
}

TEST(DbusConfigTest, Defaults) {
    DbusConfig cfg;
    EXPECT_EQ(cfg.rpc_client_id, 0x4200u);
    EXPECT_EQ(cfg.default_someip_instance_id, 0x0001u);
    EXPECT_TRUE(cfg.enable_introspection);
    EXPECT_EQ(cfg.someip.local_port, 30500u);
}

TEST(DbusGatewayTest, Lifecycle) {
    DbusConfig cfg;
    DbusGateway gw(cfg);
    gw.test_set_dbus_enabled(false);

    EXPECT_FALSE(gw.is_running());
    EXPECT_EQ(gw.get_protocol(), "dbus");

    EXPECT_EQ(gw.start(), someip::Result::SUCCESS);
    EXPECT_TRUE(gw.is_running());

    EXPECT_EQ(gw.stop(), someip::Result::SUCCESS);
    EXPECT_FALSE(gw.is_running());
}

TEST(DbusGatewayTest, OnSomeipNotificationRecordsStats) {
    DbusConfig cfg;
    DbusGateway gw(cfg);
    gw.test_set_dbus_enabled(false);

    ServiceMapping mapping;
    mapping.someip_service_id = 0x1234;
    mapping.someip_instance_id = 0x0001;
    mapping.external_identifier = "vehicle";
    mapping.direction = GatewayDirection::SOMEIP_TO_EXTERNAL;
    gw.add_service_mapping(mapping);

    gw.start();

    someip::MessageId msg_id(0x1234, 0x8001);
    someip::RequestId req_id(0x0000, 0x0001);
    someip::Message msg(msg_id, req_id, someip::MessageType::NOTIFICATION);
    msg.set_payload({0x01, 0x02, 0x03});

    EXPECT_EQ(gw.on_someip_message(msg), someip::Result::SUCCESS);
    auto stats = gw.get_stats();
    EXPECT_EQ(stats.messages_someip_to_external, 1u);
    EXPECT_EQ(stats.bytes_someip_to_external, 3u);

    gw.stop();
}

TEST(DbusGatewayTest, EmitExternalRpcCallback) {
    DbusConfig cfg;
    DbusGateway gw(cfg);
    gw.test_set_dbus_enabled(false);

    ServiceMapping mapping;
    mapping.someip_service_id = 0x1234;
    mapping.someip_instance_id = 0x0001;
    mapping.direction = GatewayDirection::EXTERNAL_TO_SOMEIP;
    gw.add_service_mapping(mapping);

    uint16_t seen_method = 0;
    std::vector<uint8_t> seen_payload;
    gw.set_external_message_callback(
        [&](uint16_t sid, uint16_t mid, const std::vector<uint8_t>& pl) {
            (void)sid;
            seen_method = mid;
            seen_payload = pl;
        });

    gw.start();
    EXPECT_EQ(gw.emit_external_rpc(0x1234, 0x0001, 0x0005, {0xAA, 0xBB}), someip::Result::SUCCESS);
    EXPECT_EQ(seen_method, 0x0005);
    ASSERT_EQ(seen_payload.size(), 2u);
    EXPECT_EQ(seen_payload[0], 0xAA);
    EXPECT_EQ(seen_payload[1], 0xBB);

    auto stats = gw.get_stats();
    EXPECT_EQ(stats.messages_external_to_someip, 1u);

    gw.stop();
}

}  // namespace opensomeip::gateway::dbus
