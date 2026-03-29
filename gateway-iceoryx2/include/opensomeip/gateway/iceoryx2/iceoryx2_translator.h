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

struct Iceoryx2Envelope {
    static constexpr uint32_t kMagic = 0x53495031;

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

class Iceoryx2Translator : public MessageTranslator {
public:
    Iceoryx2Translator() = default;
    ~Iceoryx2Translator() override = default;

    Iceoryx2Translator(const Iceoryx2Translator&) = delete;
    Iceoryx2Translator& operator=(const Iceoryx2Translator&) = delete;

    static std::string build_iceoryx2_service_name(const std::string& prefix,
                                                   uint16_t service_id,
                                                   uint16_t instance_id,
                                                   uint16_t method_or_event_id,
                                                   char kind_tag);

    std::vector<uint8_t> someip_to_sample(const someip::Message& msg,
                                          uint16_t instance_id,
                                          TranslationMode mode) const;

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
