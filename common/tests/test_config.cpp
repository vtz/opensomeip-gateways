/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/config.h"

#include <gtest/gtest.h>

namespace opensomeip::gateway {

TEST(ConfigTest, ParseLogLevel) {
    EXPECT_EQ(parse_log_level("debug"), LogLevel::DEBUG);
    EXPECT_EQ(parse_log_level("DEBUG"), LogLevel::DEBUG);
    EXPECT_EQ(parse_log_level("info"), LogLevel::INFO);
    EXPECT_EQ(parse_log_level("Info"), LogLevel::INFO);
    EXPECT_EQ(parse_log_level("warn"), LogLevel::WARN);
    EXPECT_EQ(parse_log_level("warning"), LogLevel::WARN);
    EXPECT_EQ(parse_log_level("error"), LogLevel::ERROR);
    EXPECT_EQ(parse_log_level("unknown"), LogLevel::INFO);
}

TEST(ConfigTest, LogLevelToString) {
    EXPECT_EQ(to_string(LogLevel::DEBUG), "debug");
    EXPECT_EQ(to_string(LogLevel::INFO), "info");
    EXPECT_EQ(to_string(LogLevel::WARN), "warn");
    EXPECT_EQ(to_string(LogLevel::ERROR), "error");
}

TEST(ConfigTest, ParseDirection) {
    EXPECT_EQ(parse_direction("someip_to_external"),
              GatewayDirection::SOMEIP_TO_EXTERNAL);
    EXPECT_EQ(parse_direction("someip_to_mqtt"),
              GatewayDirection::SOMEIP_TO_EXTERNAL);
    EXPECT_EQ(parse_direction("someip_to_grpc"),
              GatewayDirection::SOMEIP_TO_EXTERNAL);
    EXPECT_EQ(parse_direction("someip_to_iceoryx2"),
              GatewayDirection::SOMEIP_TO_EXTERNAL);

    EXPECT_EQ(parse_direction("external_to_someip"),
              GatewayDirection::EXTERNAL_TO_SOMEIP);
    EXPECT_EQ(parse_direction("mqtt_to_someip"),
              GatewayDirection::EXTERNAL_TO_SOMEIP);

    EXPECT_EQ(parse_direction("bidirectional"),
              GatewayDirection::BIDIRECTIONAL);
    EXPECT_EQ(parse_direction("unknown"),
              GatewayDirection::BIDIRECTIONAL);
}

TEST(ConfigTest, DirectionToString) {
    EXPECT_EQ(to_string(GatewayDirection::SOMEIP_TO_EXTERNAL), "someip_to_external");
    EXPECT_EQ(to_string(GatewayDirection::EXTERNAL_TO_SOMEIP), "external_to_someip");
    EXPECT_EQ(to_string(GatewayDirection::BIDIRECTIONAL), "bidirectional");
}

TEST(ConfigTest, ParseTranslationMode) {
    EXPECT_EQ(parse_translation_mode("opaque"), TranslationMode::OPAQUE);
    EXPECT_EQ(parse_translation_mode("typed"), TranslationMode::TYPED);
    EXPECT_EQ(parse_translation_mode("schema"), TranslationMode::TYPED);
    EXPECT_EQ(parse_translation_mode("unknown"), TranslationMode::OPAQUE);
}

TEST(ConfigTest, ParseHexOrDecimal) {
    EXPECT_EQ(parse_hex_or_decimal("0x1234"), 0x1234);
    EXPECT_EQ(parse_hex_or_decimal("0X00FF"), 0x00FF);
    EXPECT_EQ(parse_hex_or_decimal("1234"), 1234);
    EXPECT_EQ(parse_hex_or_decimal("0"), 0);
}

TEST(ConfigTest, SomeipEndpointConfigDefaults) {
    SomeipEndpointConfig config;
    EXPECT_EQ(config.local_address, "0.0.0.0");
    EXPECT_EQ(config.local_port, 30500);
    EXPECT_EQ(config.sd_multicast, "239.255.255.250");
    EXPECT_EQ(config.sd_port, 30490);
    EXPECT_FALSE(config.use_tcp);
}

}  // namespace opensomeip::gateway
