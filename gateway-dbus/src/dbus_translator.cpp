/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/dbus/dbus_translator.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace opensomeip {
namespace gateway {
namespace dbus {

namespace {

std::string to_hex4(uint16_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04x", static_cast<unsigned>(v));
    return std::string(buf);
}

}  // namespace

std::string DbusTranslator::normalize_path_prefix(std::string p) {
    while (!p.empty() && p.back() == '/') {
        p.pop_back();
    }
    if (p.empty()) {
        return "/com/opensomeip";
    }
    if (p.front() != '/') {
        p.insert(p.begin(), '/');
    }
    return p;
}

std::string DbusTranslator::normalize_bus_prefix(std::string p) {
    while (!p.empty() && p.back() == '.') {
        p.pop_back();
    }
    if (p.empty()) {
        return "com.opensomeip";
    }
    return p;
}

DbusTranslator::DbusTranslator(std::string bus_name_prefix, std::string object_path_prefix)
    : bus_name_prefix_(normalize_bus_prefix(std::move(bus_name_prefix))),
      object_path_prefix_(normalize_path_prefix(std::move(object_path_prefix))) {
}

std::string DbusTranslator::build_bus_name(uint16_t service_id, uint16_t instance_id) const {
    std::ostringstream oss;
    oss << bus_name_prefix_ << ".svc." << to_hex4(service_id) << ".inst." << to_hex4(instance_id);
    return oss.str();
}

std::string DbusTranslator::build_object_path(uint16_t service_id, uint16_t instance_id) const {
    std::ostringstream oss;
    oss << object_path_prefix_ << "/svc_" << to_hex4(service_id) << "/inst_" << to_hex4(instance_id);
    return oss.str();
}

std::string DbusTranslator::build_interface_name(uint16_t service_id) {
    return std::string("com.opensomeip.Service.") + to_hex4(service_id);
}

std::string DbusTranslator::someip_type_to_dbus_signature(const std::string& someip_type) {
    std::string t = someip_type;
    std::transform(t.begin(), t.end(), t.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (t == "bool" || t == "boolean") {
        return "b";
    }
    if (t == "uint8" || t == "u8") {
        return "y";
    }
    if (t == "uint16" || t == "u16") {
        return "q";
    }
    if (t == "uint32" || t == "u32") {
        return "u";
    }
    if (t == "uint64" || t == "u64") {
        return "t";
    }
    if (t == "int16" || t == "i16") {
        return "n";
    }
    if (t == "int32" || t == "i32") {
        return "i";
    }
    if (t == "int64" || t == "i64") {
        return "x";
    }
    if (t == "float" || t == "float32") {
        return "d";
    }
    if (t == "double" || t == "float64") {
        return "d";
    }
    if (t == "string" || t == "utf8") {
        return "s";
    }
    return "ay";
}

std::string DbusTranslator::build_signature_from_someip_types(
    const std::vector<std::string>& someip_types) {
    std::string out;
    out.reserve(someip_types.size() * 2);
    for (const auto& ty : someip_types) {
        out += someip_type_to_dbus_signature(ty);
    }
    return out;
}

std::string DbusTranslator::generate_introspection_xml(
    uint16_t service_id,
    const std::vector<std::string>& method_arg_types,
    bool include_signal) const {

    const std::string iface = build_interface_name(service_id);
    const std::string arg_sig = build_signature_from_someip_types(method_arg_types);
    std::ostringstream xml;
    xml << R"(<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-Bus Object Introspection 1.0//EN")"
           R"( "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">)"
        << "<node>\n"
        << "  <interface name=\"" << iface << "\">\n";

    if (!arg_sig.empty()) {
        xml << "    <!-- opensomeip typed SOME/IP layout (D-Bus wire uses opaque ay): " << arg_sig
            << " -->\n";
    }
    xml << "    <method name=\"InvokeRpc\">\n"
        << "      <arg type=\"q\" name=\"method_id\" direction=\"in\"/>\n"
        << "      <arg type=\"ay\" name=\"payload\" direction=\"in\"/>\n"
        << "      <arg type=\"ay\" name=\"return_payload\" direction=\"out\"/>\n"
        << "    </method>\n";

    if (include_signal) {
        xml << "    <signal name=\"SomeipNotification\">\n"
            << "      <arg type=\"q\" name=\"method_or_event_id\"/>\n"
            << "      <arg type=\"ay\" name=\"payload\"/>\n"
            << "    </signal>\n"
            << "    <signal name=\"SomeipRequest\">\n"
            << "      <arg type=\"q\" name=\"method_id\"/>\n"
            << "      <arg type=\"ay\" name=\"payload\"/>\n"
            << "    </signal>\n";
    }

    xml << "  </interface>\n</node>\n";
    return xml.str();
}

}  // namespace dbus
}  // namespace gateway
}  // namespace opensomeip
