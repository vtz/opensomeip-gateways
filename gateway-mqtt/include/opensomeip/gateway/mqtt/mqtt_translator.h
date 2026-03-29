/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_MQTT_TRANSLATOR_H
#define OPENSOMEIP_GATEWAY_MQTT_TRANSLATOR_H

#include <cstdint>
#include <string>
#include <vector>

#include "opensomeip/gateway/translator.h"
#include "someip/message.h"

namespace opensomeip {
namespace gateway {

enum class MqttPayloadEncoding : uint8_t {
    RAW,
    JSON,
    SOMEIP_FRAMED
};

class MqttTranslator : public MessageTranslator {
public:
    MqttTranslator() = default;
    explicit MqttTranslator(std::string topic_prefix, std::string vin);
    ~MqttTranslator() override = default;

    MqttTranslator(const MqttTranslator&) = delete;
    MqttTranslator& operator=(const MqttTranslator&) = delete;

    std::string build_mqtt_topic(uint16_t service_id, uint16_t instance_id, uint16_t method_id,
                                 bool is_request) const;

    std::vector<uint8_t> encode_outbound(const someip::Message& msg,
                                         MqttPayloadEncoding encoding) const;

    someip::Message decode_inbound(const std::vector<uint8_t>& payload,
                                   MqttPayloadEncoding encoding) const;

    std::vector<uint8_t> build_correlation_data(uint16_t client_id, uint16_t session_id) const;

    static std::string build_topic_with_vin(const std::string& topic_prefix,
                                            const std::string& vin,
                                            uint16_t service_id,
                                            uint16_t instance_id,
                                            uint16_t method_or_event_id);

    static std::string build_rpc_request_topic(const std::string& topic_prefix,
                                               const std::string& vin,
                                               uint16_t service_id,
                                               uint16_t instance_id,
                                               uint16_t method_id);

    static std::string build_rpc_response_topic(const std::string& topic_prefix,
                                                const std::string& vin,
                                                uint16_t service_id,
                                                uint16_t instance_id,
                                                uint16_t method_id);

    static std::vector<uint8_t> encode_payload(const someip::Message& msg,
                                               MqttPayloadEncoding encoding);

    static std::vector<uint8_t> decode_payload(const std::vector<uint8_t>& mqtt_payload,
                                               MqttPayloadEncoding encoding);

    ExternalMessage someip_to_mqtt(const someip::Message& msg,
                                   const std::string& topic_prefix,
                                   const std::string& vin,
                                   uint16_t instance_id,
                                   MqttPayloadEncoding encoding) const;

    static int select_qos(bool is_notification,
                          bool is_request,
                          int default_event_qos,
                          int default_rpc_qos);

    static std::vector<uint8_t> payload_to_json_envelope(const someip::Message& msg);

private:
    std::string topic_prefix_;
    std::string vin_;
};

}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_MQTT_TRANSLATOR_H
