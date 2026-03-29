/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_TRANSLATOR_H
#define OPENSOMEIP_GATEWAY_TRANSLATOR_H

#include <cstdint>
#include <string>
#include <vector>

#include "someip/message.h"
#include "someip/types.h"
#include "serialization/serializer.h"

namespace opensomeip {
namespace gateway {

struct ExternalMessage {
    std::string topic_or_key;
    std::vector<uint8_t> payload;

    uint16_t source_service_id{0};
    uint16_t source_method_id{0};
    uint16_t source_instance_id{0};

    bool is_request{false};
    bool is_notification{false};

    std::string correlation_id;
};

class MessageTranslator {
public:
    MessageTranslator() = default;
    virtual ~MessageTranslator() = default;

    MessageTranslator(const MessageTranslator&) = delete;
    MessageTranslator& operator=(const MessageTranslator&) = delete;

    ExternalMessage someip_to_external(const someip::Message& msg,
                                       const std::string& external_id) const;

    someip::Message external_to_someip(const ExternalMessage& ext_msg,
                                       uint16_t service_id,
                                       uint16_t method_id,
                                       someip::MessageType type) const;

    static std::string build_topic(const std::string& prefix,
                                   uint16_t service_id,
                                   uint16_t instance_id,
                                   uint16_t method_or_event_id);

    static std::string format_service_id(uint16_t id);

    static std::vector<uint8_t> payload_to_json(
        const someip::Message& msg);

    static std::vector<uint8_t> json_to_payload(
        const std::vector<uint8_t>& json_data);
};

}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_TRANSLATOR_H
