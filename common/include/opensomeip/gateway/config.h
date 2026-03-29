/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_CONFIG_H
#define OPENSOMEIP_GATEWAY_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

#include "gateway_base.h"

namespace opensomeip {
namespace gateway {

struct SomeipEndpointConfig {
    std::string local_address{"0.0.0.0"};
    uint16_t local_port{30500};
    std::string sd_multicast{"239.255.255.250"};
    uint16_t sd_port{30490};
    bool use_tcp{false};
};

enum class LogLevel : uint8_t {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

struct GatewayBaseConfig {
    std::string name;
    LogLevel log_level{LogLevel::INFO};
    SomeipEndpointConfig someip;
    std::vector<ServiceMapping> service_mappings;
};

LogLevel parse_log_level(const std::string& level);
std::string to_string(LogLevel level);

GatewayDirection parse_direction(const std::string& direction);
std::string to_string(GatewayDirection direction);

TranslationMode parse_translation_mode(const std::string& mode);
std::string to_string(TranslationMode mode);

uint16_t parse_hex_or_decimal(const std::string& value);

}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_CONFIG_H
