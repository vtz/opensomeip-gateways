/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_DDS_DDS_TRANSLATOR_H
#define OPENSOMEIP_GATEWAY_DDS_DDS_DDS_TRANSLATOR_H

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "opensomeip/gateway/gateway_base.h"
#include "opensomeip/gateway/translator.h"
#include "someip/message.h"

namespace opensomeip {
namespace gateway {
namespace dds {

/**
 * @brief DDS reliability policy hint (maps to ReliabilityQosPolicy in DDS implementations).
 */
enum class DdsReliabilityKind : uint8_t {
    BestEffort,
    Reliable
};

/**
 * @brief DDS durability policy hint (maps to DurabilityQosPolicy).
 */
enum class DdsDurabilityKind : uint8_t {
    Volatile,
    TransientLocal,
    Persistent
};

/**
 * @brief History kind hint (maps to HistoryQosPolicy).
 */
enum class DdsHistoryKind : uint8_t {
    KeepLast,
    KeepAll
};

/**
 * @brief Portable QoS snapshot derived from SOME/IP message semantics and transport assumptions.
 */
struct DdsQosProfile {
    DdsReliabilityKind reliability{DdsReliabilityKind::BestEffort};
    DdsDurabilityKind durability{DdsDurabilityKind::Volatile};
    DdsHistoryKind history_kind{DdsHistoryKind::KeepLast};
    int32_t history_depth{1};
    std::chrono::milliseconds deadline{};

    static constexpr bool deadline_is_infinite(const std::chrono::milliseconds& d) {
        return d.count() <= 0;
    }
};

/**
 * @brief Translates SOME/IP routing and payloads to DDS topic names and QoS hints.
 */
class DdsTranslator : public MessageTranslator {
public:
    DdsTranslator() = default;
    ~DdsTranslator() override = default;

    DdsTranslator(const DdsTranslator&) = delete;
    DdsTranslator& operator=(const DdsTranslator&) = delete;

    /** @brief Default DDS topic naming: hierarchical, hex SOME/IP identifiers. */
    static std::string build_dds_topic(uint16_t service_id, uint16_t instance_id, uint16_t method_id);

    /**
     * @brief Topic name with message-class suffix (event vs method/RPC).
     */
    static std::string build_dds_topic(uint16_t service_id, uint16_t instance_id, uint16_t method_id,
                                       const someip::Message& msg);

    /**
     * @brief Map SOME/IP semantics to DDS QoS hints.
     * @param transport_is_tcp When true, SOME/IP carried over TCP is mapped to reliable DDS.
     */
    static DdsQosProfile qos_for_someip_message(const someip::Message& msg, bool transport_is_tcp);

    /** @brief Map gateway direction to default reader/writer reliability for a mapping. */
    static DdsQosProfile qos_for_service_mapping(const ServiceMapping& mapping, bool writer_side);

    /** @brief Opaque or typed outbound encoding (typed path wraps JSON envelope). */
    static std::vector<uint8_t> encode_outbound(const someip::Message& msg, TranslationMode mode);

    /** @brief Decode DDS payload bytes back into a SOME/IP message shell (header + payload). */
    static bool decode_inbound(const std::vector<uint8_t>& bytes, TranslationMode mode,
                               someip::Message& out_msg);

    /** @brief Bridge sample fields (from generated IDL) into a SOME/IP message. */
    static bool bridge_sample_to_someip(uint16_t service_id, uint16_t instance_id,
                                        uint16_t method_or_event_id, uint8_t message_type_byte,
                                        const std::vector<uint8_t>& payload, someip::Message& out);

    static std::vector<uint8_t> bridge_someip_to_payload(const someip::Message& msg,
                                                         uint16_t instance_id);
};

}  // namespace dds
}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_DDS_DDS_TRANSLATOR_H
