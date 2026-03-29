/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_ROS2_ROS2_TRANSLATOR_H
#define OPENSOMEIP_GATEWAY_ROS2_ROS2_TRANSLATOR_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "someip/message.h"

namespace opensomeip {
namespace gateway {
namespace ros2 {

/**
 * @brief Hint for SOME/IP transport used on the wire (drives ROS 2 QoS selection).
 */
enum class SomeipTransportKind : uint8_t {
    UDP_UNICAST = 0,
    UDP_MULTICAST = 1,
    TCP = 2,
};

/**
 * @brief Portable QoS description (no rclcpp types in the public header).
 */
struct Ros2QosProfile {
    bool reliable{true};
    std::size_t history_depth{10};
    bool durability_transient_local{false};
};

/**
 * @brief Builds ROS 2 topic names and maps SOME/IP payloads to/from opaque ROS byte payloads.
 */
class Ros2Translator {
public:
    Ros2Translator(std::string topic_prefix, std::string ros_namespace);

    const std::string& topic_prefix() const { return topic_prefix_; }
    const std::string& ros_namespace() const { return ros_namespace_; }

    /**
     * @brief Canonical topic: ``{ns}/{prefix}/{sid}/{iid}/event/{eid}`` (16-bit IDs as 0xABCD hex).
     */
    std::string build_ros2_topic(uint16_t service_id, uint16_t instance_id,
                                   uint16_t event_id) const;

    Ros2QosProfile qos_for_someip_transport(SomeipTransportKind kind) const;

    /** ROS payload: raw SOME/IP payload bytes (opaque bridge). */
    std::vector<uint8_t> convert_someip_to_ros2_bytes(const someip::Message& msg) const;

    /**
     * @brief Fill a SOME/IP message payload from ROS byte payload.
     * @return false if the message cannot be constructed.
     */
    bool convert_ros2_bytes_to_someip(const std::vector<uint8_t>& ros2_payload,
                                      someip::Message& out) const;

private:
    std::string normalize_namespace(std::string ns) const;

    std::string topic_prefix_;
    std::string ros_namespace_;
};

}  // namespace ros2
}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_ROS2_ROS2_TRANSLATOR_H
