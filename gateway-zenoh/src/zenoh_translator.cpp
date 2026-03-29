/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/zenoh/zenoh_translator.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace opensomeip {
namespace gateway {
namespace zenoh {
namespace {

bool parse_hex_u16(std::string_view token, uint16_t& out) {
    if (token.empty()) {
        return false;
    }
    if (token.size() >= 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
        token.remove_prefix(2);
    }
    if (token.empty()) {
        return false;
    }
    uint32_t v = 0;
    for (char c : token) {
        int d = -1;
        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            d = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            d = c - 'A' + 10;
        } else {
            return false;
        }
        v = (v << 4) | static_cast<uint32_t>(d);
        if (v > 0xFFFFU) {
            return false;
        }
    }
    out = static_cast<uint16_t>(v);
    return true;
}

std::vector<std::string> split_path(std::string_view path) {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') {
            ++i;
        }
        size_t j = i;
        while (j < path.size() && path[j] != '/') {
            ++j;
        }
        if (j > i) {
            parts.emplace_back(path.substr(i, j - i));
        }
        i = j;
    }
    return parts;
}

std::string_view strip_prefix_key(std::string_view key, std::string_view prefix) {
    if (prefix.empty()) {
        return key;
    }
    if (key.size() < prefix.size()) {
        return {};
    }
    if (key.compare(0, prefix.size(), prefix) != 0) {
        return {};
    }
    if (key.size() == prefix.size()) {
        return {};
    }
    if (key[prefix.size()] != '/') {
        return {};
    }
    return key.substr(prefix.size() + 1);
}

}  // namespace

ZenohTranslator::ZenohTranslator(ZenohPayloadEncoding encoding) : encoding_(encoding) {}

ZenohTranslator::ParsedZenohSomeipKey ZenohTranslator::parse_someip_key(const std::string& key,
                                                                          const std::string& prefix) {
    ParsedZenohSomeipKey r{};
    const std::string_view rest = strip_prefix_key(key, prefix);
    if (rest.empty() && !prefix.empty()) {
        return r;
    }
    const std::vector<std::string> parts = split_path(std::string(rest));
    if (parts.size() >= 3 && parts[0] == "liveliness") {
        if (parse_hex_u16(parts[1], r.service_id) && parse_hex_u16(parts[2], r.instance_id)) {
            r.is_liveliness = true;
            r.valid = true;
        }
        return r;
    }
    // .../0xsrv/0xinst/rpc/0xmethod
    if (parts.size() >= 4 && parts[2] == "rpc") {
        if (parse_hex_u16(parts[0], r.service_id) && parse_hex_u16(parts[1], r.instance_id) &&
            parse_hex_u16(parts[3], r.method_or_event_id)) {
            r.is_rpc_path = true;
            r.valid = true;
        }
        return r;
    }
    // .../0xsrv/0xinst/0xmethod/event
    if (parts.size() >= 4 && parts[3] == "event") {
        if (parse_hex_u16(parts[0], r.service_id) && parse_hex_u16(parts[1], r.instance_id) &&
            parse_hex_u16(parts[2], r.method_or_event_id)) {
            r.is_rpc_path = false;
            r.valid = true;
        }
        return r;
    }
    return r;
}

std::string ZenohTranslator::build_key_expr(const std::string& prefix, uint16_t service_id,
                                            uint16_t instance_id, uint16_t method_or_event_id,
                                            bool /*is_request_response*/) {
    std::ostringstream oss;
    oss << prefix << '/' << format_service_id(service_id) << '/' << format_service_id(instance_id) << '/'
        << format_service_id(method_or_event_id) << "/event";
    return oss.str();
}

std::string ZenohTranslator::build_instance_pattern(const std::string& prefix, uint16_t service_id,
                                                    uint16_t instance_id) {
    std::ostringstream oss;
    oss << prefix << '/' << format_service_id(service_id) << '/' << format_service_id(instance_id) << "/**";
    return oss.str();
}

std::string ZenohTranslator::build_rpc_key(const std::string& prefix, uint16_t service_id,
                                             uint16_t instance_id, uint16_t method_id) {
    std::ostringstream oss;
    oss << prefix << '/' << format_service_id(service_id) << '/' << format_service_id(instance_id) << "/rpc/"
        << format_service_id(method_id);
    return oss.str();
}

std::string ZenohTranslator::build_liveliness_key(const std::string& prefix, uint16_t service_id,
                                                  uint16_t instance_id) {
    std::ostringstream oss;
    oss << prefix << "/liveliness/" << format_service_id(service_id) << '/' << format_service_id(instance_id);
    return oss.str();
}

ExternalMessage ZenohTranslator::someip_to_zenoh(const someip::Message& msg, const std::string& external_prefix,
                                               const ServiceMapping& mapping) const {
    ExternalMessage ext;
    const bool rpcish =
        msg.is_request() || msg.is_response() ||
        msg.get_message_type() == someip::MessageType::REQUEST_NO_RETURN ||
        msg.get_message_type() == someip::MessageType::ERROR ||
        msg.get_message_type() == someip::MessageType::TP_REQUEST ||
        msg.get_message_type() == someip::MessageType::TP_REQUEST_NO_RETURN;

    ext.topic_or_key =
        rpcish ? build_rpc_key(external_prefix, mapping.someip_service_id, mapping.someip_instance_id,
                               msg.get_method_id())
               : build_key_expr(external_prefix, mapping.someip_service_id, mapping.someip_instance_id,
                                msg.get_method_id(), false);

    ext.payload = encode_payload(msg);
    ext.source_service_id = msg.get_service_id();
    ext.source_method_id = msg.get_method_id();
    ext.source_instance_id = mapping.someip_instance_id;
    ext.is_request = msg.is_request();
    ext.is_notification = (msg.get_message_type() == someip::MessageType::NOTIFICATION) ||
                          (msg.get_message_type() == someip::MessageType::TP_NOTIFICATION);

    std::ostringstream corr;
    corr << std::hex << std::setfill('0') << std::setw(4) << msg.get_client_id() << '-' << std::setw(4)
         << msg.get_session_id();
    ext.correlation_id = corr.str();
    return ext;
}

someip::Message ZenohTranslator::zenoh_to_someip(const ExternalMessage& ext, uint16_t service_id,
                                               uint16_t /*instance_id*/, someip::MessageType type) const {
    const uint16_t method = ext.source_method_id;
    someip::MessageId mid(service_id, method);
    someip::RequestId rid(0x0000, 0x0001);
    someip::Message msg(mid, rid, type, someip::ReturnCode::E_OK);

    if (encoding_ == ZenohPayloadEncoding::RAW) {
        msg.set_payload(ext.payload);
        return msg;
    }

    someip::Message envelope;
    if (decode_payload(ext.payload, envelope)) {
        msg.set_payload(envelope.get_payload());
        if (encoding_ == ZenohPayloadEncoding::CBOR) {
            msg.set_message_type(envelope.get_message_type());
        }
    } else {
        msg.set_payload(ext.payload);
    }
    return msg;
}

void ZenohTranslator::append_cbor_uint(std::vector<uint8_t>& out, uint64_t v) {
    if (v < 24U) {
        out.push_back(static_cast<uint8_t>(v));
    } else if (v < 256U) {
        out.push_back(0x18);
        out.push_back(static_cast<uint8_t>(v));
    } else if (v < 65536U) {
        out.push_back(0x19);
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(v & 0xFF));
    } else {
        out.push_back(0x1A);
        out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(v & 0xFF));
    }
}

void ZenohTranslator::append_cbor_text_string(std::vector<uint8_t>& out, const std::string& s) {
    const size_t n = s.size();
    if (n < 24U) {
        out.push_back(static_cast<uint8_t>(0x60U + n));
    } else if (n < 256U) {
        out.push_back(0x78);
        out.push_back(static_cast<uint8_t>(n));
    } else {
        out.push_back(0x79);
        out.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(n & 0xFF));
    }
    out.insert(out.end(), s.begin(), s.end());
}

void ZenohTranslator::append_cbor_byte_string(std::vector<uint8_t>& out, const std::vector<uint8_t>& data) {
    const size_t n = data.size();
    if (n < 24U) {
        out.push_back(static_cast<uint8_t>(0x40U + n));
    } else if (n < 256U) {
        out.push_back(0x58);
        out.push_back(static_cast<uint8_t>(n));
    } else {
        out.push_back(0x59);
        out.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(n & 0xFF));
    }
    out.insert(out.end(), data.begin(), data.end());
}

std::vector<uint8_t> ZenohTranslator::build_cbor_envelope(const someip::Message& msg) {
    std::vector<uint8_t> out;
    out.push_back(0xA4);  // map of 4 pairs

    append_cbor_text_string(out, "svc");
    append_cbor_uint(out, msg.get_service_id());

    append_cbor_text_string(out, "method");
    append_cbor_uint(out, msg.get_method_id());

    append_cbor_text_string(out, "type");
    append_cbor_uint(out, static_cast<uint8_t>(msg.get_message_type()));

    append_cbor_text_string(out, "payload");
    append_cbor_byte_string(out, msg.get_payload());
    return out;
}

std::vector<uint8_t> ZenohTranslator::encode_payload(const someip::Message& msg) const {
    switch (encoding_) {
        case ZenohPayloadEncoding::RAW:
            return msg.get_payload();
        case ZenohPayloadEncoding::JSON:
            return MessageTranslator::payload_to_json(msg);
        case ZenohPayloadEncoding::CBOR:
            return build_cbor_envelope(msg);
    }
    return msg.get_payload();
}

namespace {

bool decode_cbor_envelope(const std::vector<uint8_t>& wire, someip::Message& out_msg) {
    if (wire.empty() || wire[0] != 0xA4) {
        return false;
    }
    size_t i = 1;
    uint16_t svc = 0;
    uint16_t method = 0;
    uint8_t mtype = 0;
    std::vector<uint8_t> payload;

    auto read_text_key = [&](std::string& key) -> bool {
        if (i >= wire.size()) {
            return false;
        }
        const uint8_t ib = wire[i++];
        if ((ib & 0xE0U) != 0x60U && ib != 0x78 && ib != 0x79) {
            return false;
        }
        size_t len = 0;
        if ((ib & 0xE0U) == 0x60U) {
            len = ib & 0x1FU;
        } else if (ib == 0x78 && i < wire.size()) {
            len = wire[i++];
        } else if (ib == 0x79 && i + 2 <= wire.size()) {
            len = (static_cast<size_t>(wire[i]) << 8) | wire[i + 1];
            i += 2;
        } else {
            return false;
        }
        if (i + len > wire.size()) {
            return false;
        }
        key.assign(reinterpret_cast<const char*>(&wire[i]), len);
        i += len;
        return true;
    };

    auto read_uint = [&](uint64_t& v) -> bool {
        if (i >= wire.size()) {
            return false;
        }
        const uint8_t ib = wire[i++];
        if (ib < 24U) {
            v = ib;
            return true;
        }
        if (ib == 0x18 && i < wire.size()) {
            v = wire[i++];
            return true;
        }
        if (ib == 0x19 && i + 2 <= wire.size()) {
            v = (static_cast<uint64_t>(wire[i]) << 8) | wire[i + 1];
            i += 2;
            return true;
        }
        if (ib == 0x1A && i + 4 <= wire.size()) {
            v = (static_cast<uint64_t>(wire[i]) << 24) | (static_cast<uint64_t>(wire[i + 1]) << 16) |
                (static_cast<uint64_t>(wire[i + 2]) << 8) | wire[i + 3];
            i += 4;
            return true;
        }
        return false;
    };

    auto read_byte_string = [&](std::vector<uint8_t>& bytes) -> bool {
        if (i >= wire.size()) {
            return false;
        }
        const uint8_t ib = wire[i++];
        size_t len = 0;
        if ((ib & 0xE0U) == 0x40U) {
            len = ib & 0x1FU;
        } else if (ib == 0x58 && i < wire.size()) {
            len = wire[i++];
        } else if (ib == 0x59 && i + 2 <= wire.size()) {
            len = (static_cast<size_t>(wire[i]) << 8) | wire[i + 1];
            i += 2;
        } else {
            return false;
        }
        if (i + len > wire.size()) {
            return false;
        }
        bytes.assign(wire.begin() + static_cast<std::ptrdiff_t>(i),
                     wire.begin() + static_cast<std::ptrdiff_t>(i + len));
        i += len;
        return true;
    };

    for (int pair = 0; pair < 4; ++pair) {
        std::string k;
        if (!read_text_key(k)) {
            return false;
        }
        if (k == "svc") {
            uint64_t v = 0;
            if (!read_uint(v) || v > 0xFFFFU) {
                return false;
            }
            svc = static_cast<uint16_t>(v);
        } else if (k == "method") {
            uint64_t v = 0;
            if (!read_uint(v) || v > 0xFFFFU) {
                return false;
            }
            method = static_cast<uint16_t>(v);
        } else if (k == "type") {
            uint64_t v = 0;
            if (!read_uint(v) || v > 0xFFU) {
                return false;
            }
            mtype = static_cast<uint8_t>(v);
        } else if (k == "payload") {
            if (!read_byte_string(payload)) {
                return false;
            }
        } else {
            return false;
        }
    }
    out_msg.set_service_id(svc);
    out_msg.set_method_id(method);
    out_msg.set_message_type(static_cast<someip::MessageType>(mtype));
    out_msg.set_payload(std::move(payload));
    return true;
}

bool decode_json_envelope(const std::vector<uint8_t>& wire, someip::Message& out_msg) {
    const std::string s(wire.begin(), wire.end());
    auto pos = s.find("\"payload\":[");
    if (pos == std::string::npos) {
        return false;
    }
    pos += 11;
    std::vector<uint8_t> bytes;
    while (pos < s.size()) {
        char c = s[pos];
        if (c == ']') {
            break;
        }
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            ++pos;
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
        size_t j = pos;
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) {
            ++j;
        }
        int v = std::stoi(s.substr(pos, j - pos));
        if (v < 0 || v > 255) {
            return false;
        }
        bytes.push_back(static_cast<uint8_t>(v));
        pos = j;
    }
    out_msg.set_payload(std::move(bytes));
    return true;
}

}  // namespace

bool ZenohTranslator::decode_payload(const std::vector<uint8_t>& wire, someip::Message& out_msg) const {
    switch (encoding_) {
        case ZenohPayloadEncoding::RAW:
            out_msg.set_payload(wire);
            return true;
        case ZenohPayloadEncoding::JSON:
            return decode_json_envelope(wire, out_msg);
        case ZenohPayloadEncoding::CBOR:
            return decode_cbor_envelope(wire, out_msg);
    }
    out_msg.set_payload(wire);
    return true;
}

}  // namespace zenoh
}  // namespace gateway
}  // namespace opensomeip
