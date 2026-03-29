/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/iceoryx2/iceoryx2_translator.h"

#include <iomanip>
#include <sstream>

namespace opensomeip {
namespace gateway {
namespace iceoryx2 {

std::string Iceoryx2Translator::build_iceoryx2_service_name(
    const std::string& prefix, uint16_t service_id, uint16_t instance_id,
    uint16_t method_or_event_id, char kind_tag) {

    std::ostringstream o;
    o << prefix << '/' << MessageTranslator::format_service_id(service_id) << '/'
      << MessageTranslator::format_service_id(instance_id) << '/'
      << MessageTranslator::format_service_id(method_or_event_id) << '/' << kind_tag;
    return o.str();
}

void Iceoryx2Translator::serialize_envelope(someip::serialization::Serializer& ser,
                                                   const Iceoryx2Envelope& env) {
    ser.serialize_uint32(Iceoryx2Envelope::kMagic);
    ser.serialize_uint8(env.version);
    ser.serialize_uint8(static_cast<uint8_t>(env.mode));
    ser.serialize_uint16(env.service_id);
    ser.serialize_uint16(env.instance_id);
    ser.serialize_uint16(env.method_or_event_id);
    ser.serialize_uint8(static_cast<uint8_t>(env.message_type));
    ser.serialize_uint8(0);
    ser.serialize_uint16(env.client_id);
    ser.serialize_uint16(env.session_id);
    ser.serialize_string(env.correlation_id);

    ser.serialize_uint32(static_cast<uint32_t>(env.payload.size()));
    for (uint8_t b : env.payload) {
        ser.serialize_uint8(b);
    }
}

std::optional<Iceoryx2Envelope> Iceoryx2Translator::deserialize_envelope(
    someip::serialization::Deserializer& deser) {

    Iceoryx2Envelope env;
    auto magic = deser.deserialize_uint32();
    if (magic.is_error() || magic.get_value() != Iceoryx2Envelope::kMagic) {
        return std::nullopt;
    }
    auto ver = deser.deserialize_uint8();
    if (ver.is_error()) {
        return std::nullopt;
    }
    env.version = ver.get_value();

    auto mode = deser.deserialize_uint8();
    if (mode.is_error()) {
        return std::nullopt;
    }
    env.mode = static_cast<TranslationMode>(mode.get_value());

    auto sid = deser.deserialize_uint16();
    auto iid = deser.deserialize_uint16();
    auto mid = deser.deserialize_uint16();
    if (sid.is_error() || iid.is_error() || mid.is_error()) {
        return std::nullopt;
    }
    env.service_id = sid.get_value();
    env.instance_id = iid.get_value();
    env.method_or_event_id = mid.get_value();

    auto mtype = deser.deserialize_uint8();
    auto pad = deser.deserialize_uint8();
    if (mtype.is_error() || pad.is_error()) {
        return std::nullopt;
    }
    env.message_type = static_cast<someip::MessageType>(mtype.get_value());
    (void)pad.get_value();

    auto cid = deser.deserialize_uint16();
    auto sess = deser.deserialize_uint16();
    if (cid.is_error() || sess.is_error()) {
        return std::nullopt;
    }
    env.client_id = cid.get_value();
    env.session_id = sess.get_value();

    auto corr = deser.deserialize_string();
    if (corr.is_error()) {
        return std::nullopt;
    }
    env.correlation_id = corr.move_value();

    auto len = deser.deserialize_uint32();
    if (len.is_error()) {
        return std::nullopt;
    }
    const uint32_t payload_len = len.get_value();
    if (payload_len > 16U * 1024U * 1024U) {
        return std::nullopt;
    }
    if (deser.get_remaining() < payload_len) {
        return std::nullopt;
    }
    env.payload.reserve(payload_len);
    for (uint32_t i = 0; i < payload_len; ++i) {
        auto b = deser.deserialize_uint8();
        if (b.is_error()) {
            return std::nullopt;
        }
        env.payload.push_back(b.get_value());
    }
    return env;
}

std::vector<uint8_t> Iceoryx2Translator::someip_to_sample(
    const someip::Message& msg, uint16_t instance_id, TranslationMode mode) const {

    Iceoryx2Envelope env;
    env.version = 1;
    env.mode = mode;
    env.service_id = msg.get_service_id();
    env.instance_id = instance_id;
    env.method_or_event_id = msg.get_method_id();
    env.message_type = msg.get_message_type();
    env.client_id = msg.get_client_id();
    env.session_id = msg.get_session_id();

    std::ostringstream corr;
    corr << std::hex << std::setfill('0') << std::setw(4) << msg.get_client_id() << "-"
         << std::setw(4) << msg.get_session_id();
    env.correlation_id = corr.str();

    if (mode == TranslationMode::TYPED) {
        env.payload = MessageTranslator::payload_to_json(msg);
    } else {
        env.payload = msg.get_payload();
    }

    someip::serialization::Serializer ser;
    serialize_envelope(ser, env);
    return std::vector<uint8_t>(ser.get_buffer().begin(), ser.get_buffer().end());
}

std::optional<Iceoryx2Envelope> Iceoryx2Translator::parse_sample(
    const std::vector<uint8_t>& data) const {

    someip::serialization::Deserializer deser(data);
    return deserialize_envelope(deser);
}

someip::Message Iceoryx2Translator::envelope_to_someip(
    const Iceoryx2Envelope& env) const {

    someip::MessageId mid(env.service_id, env.method_or_event_id);
    someip::RequestId rid(env.client_id, env.session_id);
    someip::Message msg(mid, rid, env.message_type);

    if (env.mode == TranslationMode::TYPED) {
        msg.set_payload(MessageTranslator::json_to_payload(env.payload));
    } else {
        msg.set_payload(env.payload);
    }
    return msg;
}

ExternalMessage Iceoryx2Translator::envelope_to_external(
    const Iceoryx2Envelope& env, const std::string& topic) const {

    ExternalMessage ext;
    ext.topic_or_key = topic;
    ext.source_service_id = env.service_id;
    ext.source_method_id = env.method_or_event_id;
    ext.source_instance_id = env.instance_id;
    ext.is_request = (env.message_type == someip::MessageType::REQUEST) ||
                     (env.message_type == someip::MessageType::REQUEST_NO_RETURN);
    ext.is_notification = (env.message_type == someip::MessageType::NOTIFICATION);
    ext.correlation_id = env.correlation_id;
    ext.payload = env.payload;
    return ext;
}

}  // namespace iceoryx2
}  // namespace gateway
}  // namespace opensomeip
