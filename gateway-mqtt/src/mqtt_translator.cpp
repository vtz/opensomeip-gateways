/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/mqtt/mqtt_translator.h"

#include <sstream>
#include <utility>

namespace opensomeip {
namespace gateway {

MqttTranslator::MqttTranslator(std::string topic_prefix, std::string vin)
    : topic_prefix_(std::move(topic_prefix)), vin_(std::move(vin)) {
}

std::string MqttTranslator::build_mqtt_topic(uint16_t service_id, uint16_t instance_id,
                                             uint16_t method_id, bool is_request) const {
    std::ostringstream topic;
    topic << topic_prefix_ << "/" << vin_ << "/someip/" << format_service_id(service_id) << "/"
          << format_service_id(instance_id) << "/";
    if (is_request) {
        topic << "method/" << format_service_id(method_id) << "/request";
    } else {
        topic << "event/" << format_service_id(method_id);
    }
    return topic.str();
}

std::vector<uint8_t> MqttTranslator::encode_outbound(const someip::Message& msg,
                                                     MqttPayloadEncoding encoding) const {
    return encode_payload(msg, encoding);
}

someip::Message MqttTranslator::decode_inbound(const std::vector<uint8_t>& payload,
                                               MqttPayloadEncoding encoding) const {
    if (encoding == MqttPayloadEncoding::SOMEIP_FRAMED) {
        someip::Message m;
        if (m.deserialize(payload, false)) {
            return m;
        }
        return someip::Message();
    }
    std::vector<uint8_t> inner = decode_payload(payload, encoding);
    someip::MessageId mid(0x0000, 0x0000);
    someip::RequestId rid(0x0000, 0x0001);
    someip::Message msg(mid, rid, someip::MessageType::REQUEST);
    msg.set_payload(std::move(inner));
    return msg;
}

std::vector<uint8_t> MqttTranslator::build_correlation_data(uint16_t client_id,
                                                             uint16_t session_id) const {
    return {
        static_cast<uint8_t>((client_id >> 8) & 0xFF),
        static_cast<uint8_t>(client_id & 0xFF),
        static_cast<uint8_t>((session_id >> 8) & 0xFF),
        static_cast<uint8_t>(session_id & 0xFF),
    };
}

std::string MqttTranslator::build_topic_with_vin(const std::string& topic_prefix,
                                                 const std::string& vin,
                                                 uint16_t service_id,
                                                 uint16_t instance_id,
                                                 uint16_t method_or_event_id) {
    std::ostringstream topic;
    topic << topic_prefix << "/" << vin << "/someip/" << format_service_id(service_id) << "/"
          << format_service_id(instance_id) << "/" << format_service_id(method_or_event_id);
    return topic.str();
}

std::string MqttTranslator::build_rpc_request_topic(const std::string& topic_prefix,
                                                    const std::string& vin,
                                                    uint16_t service_id,
                                                    uint16_t instance_id,
                                                    uint16_t method_id) {
    return build_topic_with_vin(topic_prefix, vin, service_id, instance_id, method_id) + "/rpc/req";
}

std::string MqttTranslator::build_rpc_response_topic(const std::string& topic_prefix,
                                                     const std::string& vin,
                                                     uint16_t service_id,
                                                     uint16_t instance_id,
                                                     uint16_t method_id) {
    return build_topic_with_vin(topic_prefix, vin, service_id, instance_id, method_id) + "/rpc/rsp";
}

std::vector<uint8_t> MqttTranslator::encode_payload(const someip::Message& msg,
                                                     MqttPayloadEncoding encoding) {
    switch (encoding) {
        case MqttPayloadEncoding::RAW:
            return msg.get_payload();
        case MqttPayloadEncoding::JSON:
            return payload_to_json_envelope(msg);
        case MqttPayloadEncoding::SOMEIP_FRAMED: {
            return msg.serialize();
        }
        default:
            return msg.get_payload();
    }
}

std::vector<uint8_t> MqttTranslator::decode_payload(const std::vector<uint8_t>& mqtt_payload,
                                                    MqttPayloadEncoding encoding) {
    switch (encoding) {
        case MqttPayloadEncoding::RAW:
            return mqtt_payload;
        case MqttPayloadEncoding::JSON:
            return MessageTranslator::json_to_payload(mqtt_payload);
        case MqttPayloadEncoding::SOMEIP_FRAMED: {
            someip::Message m;
            if (m.deserialize(mqtt_payload, false)) {
                return m.get_payload();
            }
            return {};
        }
        default:
            return mqtt_payload;
    }
}

ExternalMessage MqttTranslator::someip_to_mqtt(const someip::Message& msg,
                                               const std::string& topic_prefix,
                                               const std::string& vin,
                                               uint16_t instance_id,
                                               MqttPayloadEncoding encoding) const {
    ExternalMessage ext =
        someip_to_external(msg, build_topic_with_vin(topic_prefix, vin, msg.get_service_id(),
                                                     instance_id, msg.get_method_id()));
    ext.topic_or_key =
        build_topic_with_vin(topic_prefix, vin, msg.get_service_id(), instance_id,
                             msg.get_method_id());
    ext.payload = encode_payload(msg, encoding);
    return ext;
}

int MqttTranslator::select_qos(bool is_notification, bool is_request, int default_event_qos,
                               int default_rpc_qos) {
    int q = is_request || !is_notification ? default_rpc_qos : default_event_qos;
    if (q < 0) {
        q = 0;
    }
    if (q > 2) {
        q = 2;
    }
    return q;
}

std::vector<uint8_t> MqttTranslator::payload_to_json_envelope(const someip::Message& msg) {
    return MessageTranslator::payload_to_json(msg);
}

}  // namespace gateway
}  // namespace opensomeip
