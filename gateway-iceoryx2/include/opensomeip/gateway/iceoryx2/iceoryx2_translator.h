/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_ICEORYX2_TRANSLATOR_H
#define OPENSOMEIP_GATEWAY_ICEORYX2_TRANSLATOR_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "opensomeip/gateway/gateway_base.h"
#include "opensomeip/gateway/translator.h"
#include "someip/message.h"
#include "serialization/serializer.h"

namespace opensomeip {
namespace gateway {
namespace iceoryx2 {

/**
 * Binary envelope exchanged with iceoryx2 samples (opaque on the wire).
 * Layout is big-endian and uses someip::serialization::{Serializer,Deserializer}.
 */
struct Iceoryx2Envelope {
    static constexpr uint32_t kMagic = 0x53495031;  // 'SIP1'

    uint8_t version{1};
    TranslationMode mode{TranslationMode::OPAQUE};

    uint16_t service_id{0};
    uint16_t instance_id{0};
    uint16_t method_or_event_id{0};

    someip::MessageType message_type{someip::MessageType::REQUEST};

    uint16_t client_id{0};
    uint16_t session_id{0};

    std::string correlation_id;
    std::vector<uint8_t> payload;
};

/**
 * Translates SOME/IP messages to iceoryx2-oriented byte samples and back.
 * Extends MessageTranslator for topic naming and opaque/typed payload policies.
 */
class Iceoryx2MessageTranslator : public MessageTranslator {
public:
    Iceoryx2MessageTranslator() = default;
    ~Iceoryx2MessageTranslator() override = default;

    Iceoryx2MessageTranslator(const Iceoryx2MessageTranslator&) = delete;
    Iceoryx2MessageTranslator& operator=(const Iceoryx2MessageTranslator&) = delete;

    /** Build iceoryx2 service/topic string from gateway prefix and SOME/IP IDs. */
    static std::string build_iceoryx2_service_name(const std::string& prefix,
                                                   uint16_t service_id,
                                                   uint16_t instance_id,
                                                   uint16_t method_or_event_id,
                                                   char kind_tag);

    /** Serialize SOME/IP message + instance context into a single iceoryx2 sample. */
    std::vector<uint8_t> someip_to_sample(const someip::Message& msg,
                                          uint16_t instance_id,
                                          TranslationMode mode) const;

    /** Parse iceoryx2 sample bytes into envelope + SOME/IP message skeleton. */
    std::optional<Iceoryx2Envelope> parse_sample(const std::vector<uint8_t>& data) const;

    someip::Message envelope_to_someip(const Iceoryx2Envelope& env) const;

    ExternalMessage envelope_to_external(const Iceoryx2Envelope& env,
                                         const std::string& topic) const;

private:
    static void serialize_envelope(someip::serialization::Serializer& ser,
                                   const Iceoryx2Envelope& env);
    static std::optional<Iceoryx2Envelope> deserialize_envelope(
        someip::serialization::Deserializer& deser);
};

}  // namespace iceoryx2
}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_ICEORYX2_TRANSLATOR_H
