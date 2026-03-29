/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include <iostream>
#include <vector>

#include "opensomeip/gateway/mqtt/mqtt_gateway.h"
#include "serialization/serializer.h"
#include "someip/message.h"

int main() {
    using namespace opensomeip::gateway;

    // --- Configure MQTT gateway ---
    MqttConfig config;
    config.broker_uri = "tcp://localhost:1883";
    config.client_id = "vehicle-telemetry-gw";
    config.topic_prefix = "vehicle";
    config.vin = "WBA12345678901234";
    config.default_publish_qos = 1;
    config.outbound_encoding = MqttPayloadEncoding::JSON;
    config.offline_buffer_capacity = 512;
    config.use_mqtt_v5_request_response = true;

    MqttGateway gateway(config);

    // --- Map: speed sensor (SOME/IP event → MQTT topic) ---
    ServiceMapping speed;
    speed.someip_service_id = 0x1000;
    speed.someip_instance_id = 0x0001;
    speed.someip_event_group_ids = {0x0001};
    speed.external_identifier = "telemetry/speed";
    speed.direction = GatewayDirection::SOMEIP_TO_EXTERNAL;
    speed.mode = TranslationMode::OPAQUE;
    gateway.add_service_mapping(speed);

    // --- Map: door lock (MQTT command → SOME/IP RPC) ---
    ServiceMapping door_lock;
    door_lock.someip_service_id = 0x2000;
    door_lock.someip_instance_id = 0x0001;
    door_lock.someip_method_ids = {0x0001};
    door_lock.external_identifier = "command/door_lock";
    door_lock.direction = GatewayDirection::BIDIRECTIONAL;
    gateway.add_service_mapping(door_lock);

    // --- SOME/IP outbound sink (messages arriving from cloud) ---
    gateway.set_someip_outbound_sink([](const someip::Message& msg) {
        std::cout << "[Cloud→Vehicle] SOME/IP message: service=0x"
                  << std::hex << msg.get_service_id()
                  << " method=0x" << msg.get_method_id()
                  << " payload=" << std::dec << msg.get_payload().size() << " bytes\n";
    });

    gateway.start();
    gateway.test_set_mqtt_connected(true);

    std::cout << "MQTT Gateway '" << gateway.get_name() << "' started\n";
    std::cout << "  Broker: " << config.broker_uri << "\n";
    std::cout << "  VIN: " << config.vin << "\n\n";

    // ---- Simulate vehicle speed event → MQTT ----
    {
        std::cout << "=== Vehicle Speed Event → MQTT ===\n";
        someip::MessageId msg_id(0x1000, 0x8001);
        someip::RequestId req_id(0x0000, 0x0001);
        someip::Message notification(msg_id, req_id, someip::MessageType::NOTIFICATION);

        someip::serialization::Serializer ser;
        ser.serialize_float(120.5f);
        ser.serialize_float(48.1234f);
        ser.serialize_float(11.5678f);
        notification.set_payload(ser.get_buffer());

        std::string topic = gateway.route_someip_to_mqtt_topic(
            notification, gateway.get_service_mappings()[0]);

        std::cout << "  MQTT topic: " << topic << "\n";
        std::cout << "  QoS: " << gateway.qos_for_outbound_event(0x8001) << "\n";
        std::cout << "  Encoding: JSON\n";

        auto result = gateway.on_someip_message(notification);
        std::cout << "  Result: " << (result == someip::Result::SUCCESS ? "published" : "buffered")
                  << "\n";
    }

    // ---- Simulate RPC request → MQTT v5 request-response ----
    {
        std::cout << "\n=== Door Lock RPC → MQTT v5 Request-Response ===\n";
        someip::MessageId msg_id(0x2000, 0x0001);
        someip::RequestId req_id(0x0042, 0x0001);
        someip::Message request(msg_id, req_id, someip::MessageType::REQUEST);

        someip::serialization::Serializer ser;
        ser.serialize_uint8(1);
        ser.serialize_string("lock_all");
        request.set_payload(ser.get_buffer());

        auto result = gateway.on_someip_message(request);
        std::cout << "  Result: " << (result == someip::Result::SUCCESS ? "published" : "buffered")
                  << "\n";
    }

    // ---- Simulate offline buffering ----
    {
        std::cout << "\n=== Offline Buffering (connection lost) ===\n";
        gateway.test_set_mqtt_connected(false);

        someip::MessageId msg_id(0x1000, 0x8001);
        someip::RequestId req_id(0x0000, 0x0002);
        someip::Message msg(msg_id, req_id, someip::MessageType::NOTIFICATION);
        msg.set_payload({0x01, 0x02, 0x03});

        gateway.on_someip_message(msg);
        gateway.on_someip_message(msg);

        std::cout << "  Buffered messages: " << gateway.offline_buffer_occupancy() << "\n";

        gateway.test_set_mqtt_connected(true);
        gateway.flush_offline_buffer();
        std::cout << "  After flush: " << gateway.offline_buffer_occupancy() << "\n";
    }

    // ---- Print statistics ----
    auto stats = gateway.get_stats();
    std::cout << "\n=== Gateway Statistics ===\n";
    std::cout << "  SOME/IP → MQTT: " << stats.messages_someip_to_external << " messages\n";
    std::cout << "  MQTT → SOME/IP: " << stats.messages_external_to_someip << " messages\n";
    std::cout << "  Translation errors: " << stats.translation_errors << "\n";
    std::cout << "  Uptime: " << stats.uptime().count() << " ms\n";

    gateway.stop();
    std::cout << "\nGateway stopped.\n";
    return 0;
}
