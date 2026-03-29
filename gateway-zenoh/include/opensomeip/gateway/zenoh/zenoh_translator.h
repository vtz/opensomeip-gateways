/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_ZENOH_TRANSLATOR_H
#define OPENSOMEIP_GATEWAY_ZENOH_TRANSLATOR_H

#include <cstdint>
#include <string>
#include <vector>

#include "opensomeip/gateway/translator.h"
#include "someip/message.h"

namespace opensomeip {
namespace gateway {
namespace zenoh {

/**
 * @brief How SOME/IP payloads are represented on the Zenoh wire.
 *
 * Raw keeps bytes verbatim. JSON wraps metadata and payload bytes as a small JSON object.
 * CBOR encodes a fixed map {svc, method, type, payload} with the payload as a byte string.
 */
enum class ZenohPayloadEncoding : uint8_t {
    RAW = 0,
    JSON = 1,
    CBOR = 2
};

/**
 * @brief Maps SOME/IP messages to Zenoh key expressions and encoded payloads.
 *
 * Builds hierarchical keys: `<prefix>/<service>/<instance>/<method_or_event>[/rpc|/event]`
 * and reuses opensomeip::gateway::MessageTranslator for correlation metadata.
 */
class ZenohTranslator : public MessageTranslator {
public:
    ZenohTranslator() = default;
    explicit ZenohTranslator(ZenohPayloadEncoding encoding);

    ZenohPayloadEncoding encoding() const { return encoding_; }
    void set_encoding(ZenohPayloadEncoding enc) { encoding_ = enc; }

    /** @brief Parsed routing information from a concrete Zenoh key (no wildcards). */
    struct ParsedZenohSomeipKey {
        uint16_t service_id{0};
        uint16_t instance_id{0};
        uint16_t method_or_event_id{0};
        bool is_rpc_path{false};
        bool is_liveliness{false};
        bool valid{false};
    };

    /** @brief Split a key of the form `<prefix>/...` into SOME/IP routing fields. */
    static ParsedZenohSomeipKey parse_someip_key(const std::string& key, const std::string& prefix);

    /** @brief Zenoh key expression (no wildcards) for a concrete SOME/IP tuple. */
    static std::string build_key_expr(const std::string& prefix, uint16_t service_id,
                                      uint16_t instance_id, uint16_t method_or_event_id,
                                      bool is_request_response);

    /** @brief Subscriber-side pattern under the gateway prefix for a service instance. */
    static std::string build_instance_pattern(const std::string& prefix, uint16_t service_id,
                                              uint16_t instance_id);

    /** @brief Key used for RPC-style queryables. */
    static std::string build_rpc_key(const std::string& prefix, uint16_t service_id,
                                     uint16_t instance_id, uint16_t method_id);

    /** @brief Liveliness token key advertising SOME/IP service availability. */
    static std::string build_liveliness_key(const std::string& prefix, uint16_t service_id,
                                            uint16_t instance_id);

    ExternalMessage someip_to_zenoh(const someip::Message& msg, const std::string& external_prefix,
                                    const ServiceMapping& mapping) const;

    someip::Message zenoh_to_someip(const ExternalMessage& ext, uint16_t service_id,
                                    uint16_t instance_id, someip::MessageType type) const;

    std::vector<uint8_t> encode_payload(const someip::Message& msg) const;
    bool decode_payload(const std::vector<uint8_t>& wire, someip::Message& out_msg) const;

private:
    ZenohPayloadEncoding encoding_{ZenohPayloadEncoding::RAW};

    static void append_cbor_text_string(std::vector<uint8_t>& out, const std::string& s);
    static void append_cbor_byte_string(std::vector<uint8_t>& out, const std::vector<uint8_t>& data);
    static void append_cbor_uint(std::vector<uint8_t>& out, uint64_t v);
    static std::vector<uint8_t> build_cbor_envelope(const someip::Message& msg);
};

}  // namespace zenoh
}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_ZENOH_TRANSLATOR_H
