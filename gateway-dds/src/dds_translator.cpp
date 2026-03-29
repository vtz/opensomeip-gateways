/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/dds/dds_translator.h"

#include <iomanip>
#include <sstream>

#include "someip/types.h"

namespace opensomeip {
namespace gateway {
namespace dds {

namespace {

std::string hex4(uint16_t v) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>(v);
    return ss.str();
}

}  // namespace

std::string DdsTranslator::build_dds_topic(uint16_t service_id, uint16_t instance_id,
                                           uint16_t method_id) {
    std::ostringstream t;
    t << "opensomeip/svc/" << hex4(service_id) << "/inst/" << hex4(instance_id) << "/m/"
      << hex4(method_id);
    return t.str();
}

std::string DdsTranslator::build_dds_topic(uint16_t service_id, uint16_t instance_id,
                                           uint16_t method_id, const someip::Message& msg) {
    const char* kind = "method";
    if (msg.get_message_type() == someip::MessageType::NOTIFICATION) {
        kind = "event";
    }
    std::ostringstream t;
    t << "opensomeip/svc/" << hex4(service_id) << "/inst/" << hex4(instance_id) << "/" << kind << "/"
      << hex4(method_id);
    return t.str();
}

DdsQosProfile DdsTranslator::qos_for_someip_message(const someip::Message& msg, bool transport_is_tcp) {
    DdsQosProfile q;
    q.history_kind = DdsHistoryKind::KeepLast;
    q.history_depth = 8;

    switch (msg.get_message_type()) {
        case someip::MessageType::NOTIFICATION:
            q.reliability = transport_is_tcp ? DdsReliabilityKind::Reliable : DdsReliabilityKind::BestEffort;
            q.durability = DdsDurabilityKind::Volatile;
            q.history_depth = 1;
            break;
        case someip::MessageType::REQUEST:
        case someip::MessageType::RESPONSE:
        case someip::MessageType::ERROR:
            q.reliability = DdsReliabilityKind::Reliable;
            q.durability = DdsDurabilityKind::Volatile;
            q.history_depth = 16;
            break;
        default:
            q.reliability = DdsReliabilityKind::BestEffort;
            q.durability = DdsDurabilityKind::Volatile;
            break;
    }

    return q;
}

DdsQosProfile DdsTranslator::qos_for_service_mapping(const ServiceMapping& mapping, bool writer_side) {
    DdsQosProfile q;
    q.history_kind = DdsHistoryKind::KeepLast;
    q.history_depth = writer_side ? 8 : 16;

    const bool to_dds =
        mapping.direction == GatewayDirection::SOMEIP_TO_EXTERNAL ||
        mapping.direction == GatewayDirection::BIDIRECTIONAL;
    const bool from_dds =
        mapping.direction == GatewayDirection::EXTERNAL_TO_SOMEIP ||
        mapping.direction == GatewayDirection::BIDIRECTIONAL;

    if (writer_side && to_dds) {
        if (!mapping.someip_event_group_ids.empty() && mapping.someip_method_ids.empty()) {
            q.reliability = DdsReliabilityKind::BestEffort;
            q.durability = DdsDurabilityKind::Volatile;
            q.history_depth = 1;
        } else {
            q.reliability = DdsReliabilityKind::Reliable;
            q.durability = DdsDurabilityKind::TransientLocal;
            q.history_depth = 8;
        }
    } else if (!writer_side && from_dds) {
        q.reliability = DdsReliabilityKind::Reliable;
        q.durability = DdsDurabilityKind::Volatile;
        q.history_depth = 16;
    } else {
        q.reliability = DdsReliabilityKind::Reliable;
        q.durability = DdsDurabilityKind::Volatile;
    }

    return q;
}

std::vector<uint8_t> DdsTranslator::encode_outbound(const someip::Message& msg, TranslationMode mode) {
    if (mode == TranslationMode::TYPED) {
        return MessageTranslator::payload_to_json(msg);
    }
    return msg.get_payload();
}

bool DdsTranslator::decode_inbound(const std::vector<uint8_t>& bytes, TranslationMode mode,
                                    someip::Message& out_msg) {
    if (mode == TranslationMode::TYPED) {
        out_msg.set_payload(MessageTranslator::json_to_payload(bytes));
        return true;
    }
    return out_msg.deserialize(bytes, false);
}

bool DdsTranslator::bridge_sample_to_someip(uint16_t service_id, uint16_t instance_id,
                                              uint16_t method_or_event_id, uint8_t message_type_byte,
                                              const std::vector<uint8_t>& payload, someip::Message& out) {
    someip::MessageId mid(service_id, method_or_event_id);
    someip::RequestId rid(0x0000, 0x0001);
    auto mt = static_cast<someip::MessageType>(message_type_byte);
    out = someip::Message(mid, rid, mt, someip::ReturnCode::E_OK);
    (void)instance_id;
    out.set_payload(payload);
    return true;
}

std::vector<uint8_t> DdsTranslator::bridge_someip_to_payload(const someip::Message& msg,
                                                             uint16_t instance_id) {
    (void)instance_id;
    return encode_outbound(msg, TranslationMode::OPAQUE);
}

}  // namespace dds
}  // namespace gateway
}  // namespace opensomeip
