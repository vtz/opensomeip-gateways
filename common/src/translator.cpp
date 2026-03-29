/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/translator.h"

#include <iomanip>
#include <sstream>

namespace opensomeip {
namespace gateway {

ExternalMessage MessageTranslator::someip_to_external(
    const someip::Message& msg, const std::string& external_id) const {

    ExternalMessage ext;
    ext.topic_or_key = external_id;
    ext.payload = msg.get_payload();
    ext.source_service_id = msg.get_service_id();
    ext.source_method_id = msg.get_method_id();
    ext.is_request = msg.is_request();
    ext.is_notification =
        (msg.get_message_type() == someip::MessageType::NOTIFICATION);

    std::ostringstream corr;
    corr << std::hex << std::setfill('0') << std::setw(4) << msg.get_client_id()
         << "-" << std::setw(4) << msg.get_session_id();
    ext.correlation_id = corr.str();

    return ext;
}

someip::Message MessageTranslator::external_to_someip(
    const ExternalMessage& ext_msg, uint16_t service_id, uint16_t method_id,
    someip::MessageType type) const {

    someip::MessageId msg_id(service_id, method_id);
    someip::RequestId req_id(0x0000, 0x0001);
    someip::Message msg(msg_id, req_id, type);
    msg.set_payload(ext_msg.payload);

    return msg;
}

std::string MessageTranslator::build_topic(const std::string& prefix,
                                           uint16_t service_id,
                                           uint16_t instance_id,
                                           uint16_t method_or_event_id) {
    std::ostringstream topic;
    topic << prefix << "/" << format_service_id(service_id) << "/"
          << format_service_id(instance_id) << "/"
          << format_service_id(method_or_event_id);
    return topic.str();
}

std::string MessageTranslator::format_service_id(uint16_t id) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::setfill('0') << std::setw(4) << id;
    return ss.str();
}

std::vector<uint8_t> MessageTranslator::payload_to_json(
    const someip::Message& msg) {

    std::ostringstream json;
    json << "{";
    json << "\"service_id\":" << msg.get_service_id() << ",";
    json << "\"method_id\":" << msg.get_method_id() << ",";
    json << "\"client_id\":" << msg.get_client_id() << ",";
    json << "\"session_id\":" << msg.get_session_id() << ",";
    json << "\"message_type\":" << static_cast<int>(msg.get_message_type()) << ",";
    json << "\"return_code\":" << static_cast<int>(msg.get_return_code()) << ",";
    json << "\"payload\":[";

    const auto& payload = msg.get_payload();
    for (size_t i = 0; i < payload.size(); ++i) {
        if (i > 0) {
            json << ",";
        }
        json << static_cast<int>(payload[i]);
    }
    json << "]}";

    std::string s = json.str();
    return std::vector<uint8_t>(s.begin(), s.end());
}

std::vector<uint8_t> MessageTranslator::json_to_payload(
    const std::vector<uint8_t>& json_data) {
    // For opaque mode, return the raw bytes as-is.
    // A full JSON parser would be used in typed mode with a real JSON library.
    return json_data;
}

}  // namespace gateway
}  // namespace opensomeip
