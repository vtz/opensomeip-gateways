/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/ros2/ros2_translator.h"

#include <sstream>
#include <iomanip>

namespace opensomeip {
namespace gateway {
namespace ros2 {

namespace {

std::string hex4(uint16_t v) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
        << static_cast<int>(v);
    return oss.str();
}

}  // namespace

Ros2Translator::Ros2Translator(std::string topic_prefix, std::string ros_namespace)
    : topic_prefix_(std::move(topic_prefix)), ros_namespace_(normalize_namespace(std::move(ros_namespace))) {
}

std::string Ros2Translator::normalize_namespace(std::string ns) const {
    if (ns.empty()) {
        return {};
    }
    if (ns.front() != '/') {
        ns.insert(ns.begin(), '/');
    }
    while (ns.size() > 1 && ns.back() == '/') {
        ns.pop_back();
    }
    return ns;
}

std::string Ros2Translator::build_ros2_topic(uint16_t service_id, uint16_t instance_id,
                                               uint16_t event_id) const {
    std::ostringstream oss;
    if (!ros_namespace_.empty()) {
        oss << ros_namespace_ << '/';
    }
    oss << topic_prefix_ << '/' << hex4(service_id) << '/' << hex4(instance_id) << "/event/"
        << hex4(event_id);
    return oss.str();
}

Ros2QosProfile Ros2Translator::qos_for_someip_transport(SomeipTransportKind kind) const {
    Ros2QosProfile q;
    switch (kind) {
    case SomeipTransportKind::UDP_UNICAST:
    case SomeipTransportKind::UDP_MULTICAST:
        q.reliable = false;
        q.history_depth = 5;
        q.durability_transient_local = false;
        break;
    case SomeipTransportKind::TCP:
        q.reliable = true;
        q.history_depth = 10;
        q.durability_transient_local = false;
        break;
    }
    return q;
}

std::vector<uint8_t> Ros2Translator::convert_someip_to_ros2_bytes(const someip::Message& msg) const {
    return msg.get_payload();
}

bool Ros2Translator::convert_ros2_bytes_to_someip(const std::vector<uint8_t>& ros2_payload,
                                                    someip::Message& out) const {
    out.set_payload(ros2_payload);
    return true;
}

}  // namespace ros2
}  // namespace gateway
}  // namespace opensomeip
