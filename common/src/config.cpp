/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/config.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace opensomeip {
namespace gateway {

LogLevel parse_log_level(const std::string& level) {
    std::string lower = level;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "debug") {
        return LogLevel::DEBUG;
    }
    if (lower == "info") {
        return LogLevel::INFO;
    }
    if (lower == "warn" || lower == "warning") {
        return LogLevel::WARN;
    }
    if (lower == "error") {
        return LogLevel::ERROR;
    }
    return LogLevel::INFO;
}

std::string to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:
            return "debug";
        case LogLevel::INFO:
            return "info";
        case LogLevel::WARN:
            return "warn";
        case LogLevel::ERROR:
            return "error";
    }
    return "info";
}

GatewayDirection parse_direction(const std::string& direction) {
    std::string lower = direction;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "someip_to_external" || lower == "someip_to_mqtt" ||
        lower == "someip_to_grpc" || lower == "someip_to_iceoryx2" ||
        lower == "someip_to_zenoh" || lower == "someip_to_dbus" ||
        lower == "someip_to_dds" || lower == "someip_to_ros2") {
        return GatewayDirection::SOMEIP_TO_EXTERNAL;
    }
    if (lower == "external_to_someip" || lower == "mqtt_to_someip" ||
        lower == "grpc_to_someip" || lower == "iceoryx2_to_someip" ||
        lower == "zenoh_to_someip" || lower == "dbus_to_someip" ||
        lower == "dds_to_someip" || lower == "ros2_to_someip") {
        return GatewayDirection::EXTERNAL_TO_SOMEIP;
    }
    return GatewayDirection::BIDIRECTIONAL;
}

std::string to_string(GatewayDirection direction) {
    switch (direction) {
        case GatewayDirection::SOMEIP_TO_EXTERNAL:
            return "someip_to_external";
        case GatewayDirection::EXTERNAL_TO_SOMEIP:
            return "external_to_someip";
        case GatewayDirection::BIDIRECTIONAL:
            return "bidirectional";
    }
    return "bidirectional";
}

TranslationMode parse_translation_mode(const std::string& mode) {
    std::string lower = mode;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "typed" || lower == "schema") {
        return TranslationMode::TYPED;
    }
    return TranslationMode::OPAQUE;
}

std::string to_string(TranslationMode mode) {
    switch (mode) {
        case TranslationMode::OPAQUE:
            return "opaque";
        case TranslationMode::TYPED:
            return "typed";
    }
    return "opaque";
}

uint16_t parse_hex_or_decimal(const std::string& value) {
    if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
        return static_cast<uint16_t>(std::stoul(value, nullptr, 16));
    }
    return static_cast<uint16_t>(std::stoul(value));
}

}  // namespace gateway
}  // namespace opensomeip
