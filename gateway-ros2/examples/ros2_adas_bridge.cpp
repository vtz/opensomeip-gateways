/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

/**
 * @file ros2_adas_bridge.cpp
 * @brief Example: bridge ADAS-style SOME/IP speed/steering services to ROS 2 topics and commands.
 *
 * Build with BUILD_GATEWAY_ROS2=ON and a ROS 2 environment sourced so rclcpp and std_msgs are found.
 */

#include "opensomeip/gateway/ros2/ros2_gateway.h"

#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/u_int8_multi_array.hpp>

#include "serialization/serializer.h"
#include "someip/types.h"

namespace {

std::atomic<bool> g_run{true};

void on_sigint(int) {
    g_run.store(false);
}

std::vector<uint8_t> serialize_float(float v) {
    someip::serialization::Serializer ser;
    ser.serialize_float(v);
    return ser.move_buffer();
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, on_sigint);
    std::signal(SIGTERM, on_sigint);

    rclcpp::init(argc, argv);

    opensomeip::gateway::ros2::Ros2Config cfg;
    cfg.node_name = "ros2_adas_bridge";
    cfg.ros_namespace = "/vehicle";
    cfg.topic_prefix = "someip";
    cfg.rpc_client_id = 0x7101;
    cfg.enable_event_subscriber = true;
    cfg.default_someip_transport = opensomeip::gateway::ros2::SomeipTransportKind::UDP_UNICAST;

    opensomeip::gateway::ros2::Ros2Gateway gateway(cfg);

    constexpr uint16_t kSpeedService = 0x6001;
    constexpr uint16_t kSteerService = 0x6002;
    constexpr uint16_t kSpeedEvent = 0x8001;
    constexpr uint16_t kSteerEvent = 0x8002;
    constexpr uint16_t kSteerMethod = 0x0001;

    opensomeip::gateway::ServiceMapping speed_out;
    speed_out.someip_service_id = kSpeedService;
    speed_out.someip_instance_id = 0x0001;
    speed_out.external_identifier = "/vehicle/adas/speed_ms";
    speed_out.direction = opensomeip::gateway::GatewayDirection::SOMEIP_TO_EXTERNAL;
    gateway.add_service_mapping(speed_out);

    opensomeip::gateway::ServiceMapping steer_out;
    steer_out.someip_service_id = kSteerService;
    steer_out.someip_instance_id = 0x0001;
    steer_out.external_identifier = "/vehicle/adas/steering_deg";
    steer_out.direction = opensomeip::gateway::GatewayDirection::SOMEIP_TO_EXTERNAL;
    gateway.add_service_mapping(steer_out);

    opensomeip::gateway::ServiceMapping steer_cmd;
    steer_cmd.someip_service_id = kSteerService;
    steer_cmd.someip_instance_id = 0x0001;
    steer_cmd.external_identifier = "/vehicle/adas/steering_command";
    steer_cmd.someip_method_ids = {kSteerMethod};
    steer_cmd.direction = opensomeip::gateway::GatewayDirection::EXTERNAL_TO_SOMEIP;
    gateway.add_service_mapping(steer_cmd);

    if (gateway.start() != someip::Result::SUCCESS) {
        std::cerr << "gateway.start() failed\n";
        return 1;
    }

    auto node = std::make_shared<rclcpp::Node>("adas_demo_publisher");

    rclcpp::QoS qos(5);
    qos.best_effort();

    auto speed_pub = node->create_publisher<std_msgs::msg::UInt8MultiArray>("/vehicle/adas/speed_ms", qos);
    auto steer_pub = node->create_publisher<std_msgs::msg::UInt8MultiArray>("/vehicle/adas/steering_deg", qos);

    float speed = 0.F;
    float steer = 0.F;

    auto timer = node->create_wall_timer(std::chrono::milliseconds(100), [&] {
        speed += 0.25F;
        if (speed > 40.F) {
            speed = 0.F;
        }
        steer = 5.F * std::sin(speed * 0.05F);

        someip::Message speed_msg(
            someip::MessageId{kSpeedService, kSpeedEvent},
            someip::RequestId{0x0001, 0x0001}, someip::MessageType::NOTIFICATION, someip::ReturnCode::E_OK);
        speed_msg.set_payload(serialize_float(speed));

        someip::Message steer_msg(
            someip::MessageId{kSteerService, kSteerEvent},
            someip::RequestId{0x0001, 0x0002}, someip::MessageType::NOTIFICATION, someip::ReturnCode::E_OK);
        steer_msg.set_payload(serialize_float(steer));

        gateway.on_someip_message(speed_msg);
        gateway.on_someip_message(steer_msg);

        std_msgs::msg::UInt8MultiArray ros_speed;
        ros_speed.data = serialize_float(speed);
        std_msgs::msg::UInt8MultiArray ros_steer;
        ros_steer.data = serialize_float(steer);
        speed_pub->publish(ros_speed);
        steer_pub->publish(ros_steer);
    });

    std::cout << "ros2_adas_bridge running; Ctrl+C to exit.\n";
    std::cout << "SOME/IP → ROS: inject via gateway; demo also publishes parallel float topics.\n";

    while (g_run.load() && rclcpp::ok()) {
        rclcpp::spin_some(node);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    gateway.stop();
    rclcpp::shutdown();
    return 0;
}
