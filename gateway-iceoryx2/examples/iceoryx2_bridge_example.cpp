/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include <iostream>
#include <thread>

#include "opensomeip/gateway/iceoryx2/iceoryx2_gateway.h"
#include "serialization/serializer.h"
#include "someip/message.h"

int main() {
    using namespace opensomeip::gateway;
    using namespace opensomeip::gateway::iceoryx2;

    // --- Configure the gateway ---
    Iceoryx2Config config;
    config.gateway_name = "radar-iceoryx2-bridge";
    config.service_name_prefix = "vehicle/ecu1/someip";
    config.use_inprocess_shm_simulation = true;
    config.shm.max_sample_bytes = 4096;

    Iceoryx2Gateway gateway(config);

    // --- Service mapping: SOME/IP radar service ↔ iceoryx2 "radar/front" ---
    ServiceMapping radar_events;
    radar_events.someip_service_id = 0x1234;
    radar_events.someip_instance_id = 0x0001;
    radar_events.someip_event_group_ids = {0x0001};
    radar_events.external_identifier = "radar/front";
    radar_events.direction = GatewayDirection::BIDIRECTIONAL;
    radar_events.mode = TranslationMode::OPAQUE;
    gateway.add_service_mapping(radar_events);

    // --- Service mapping: vehicle control RPC ---
    ServiceMapping control_rpc;
    control_rpc.someip_service_id = 0x2000;
    control_rpc.someip_instance_id = 0x0001;
    control_rpc.someip_method_ids = {0x0001, 0x0002};
    control_rpc.external_identifier = "vehicle/control";
    control_rpc.direction = GatewayDirection::BIDIRECTIONAL;
    control_rpc.mode = TranslationMode::OPAQUE;
    gateway.add_service_mapping(control_rpc);

    // --- Observe iceoryx2-bound samples ---
    gateway.set_iceoryx2_outbound_hook(
        [](const std::string& service_name, const std::vector<uint8_t>& sample) {
            std::cout << "[iceoryx2 OUT] service=" << service_name
                      << " sample_size=" << sample.size() << " bytes\n";
        });

    // --- Start the gateway ---
    auto result = gateway.start();
    if (result != someip::Result::SUCCESS) {
        std::cerr << "Failed to start gateway\n";
        return 1;
    }
    std::cout << "Gateway '" << gateway.get_name() << "' started (protocol: "
              << gateway.get_protocol() << ")\n\n";

    // ---- Simulate SOME/IP → iceoryx2 (event notification) ----
    {
        std::cout << "=== SOME/IP Event → iceoryx2 pub/sub ===\n";
        someip::MessageId msg_id(0x1234, 0x8001);
        someip::RequestId req_id(0x0000, 0x0001);
        someip::Message notification(msg_id, req_id, someip::MessageType::NOTIFICATION);

        someip::serialization::Serializer ser;
        ser.serialize_float(42.5f);
        ser.serialize_float(10.2f);
        ser.serialize_float(-3.7f);
        notification.set_payload(ser.get_buffer());

        std::cout << "  Sending SOME/IP notification: service=0x1234, payload="
                  << notification.get_payload().size() << " bytes\n";
        gateway.on_someip_message(notification);
    }

    // ---- Simulate SOME/IP → iceoryx2 (RPC request) ----
    {
        std::cout << "\n=== SOME/IP RPC → iceoryx2 request-response ===\n";
        someip::MessageId msg_id(0x2000, 0x0001);
        someip::RequestId req_id(0x0042, 0x0001);
        someip::Message request(msg_id, req_id, someip::MessageType::REQUEST);

        someip::serialization::Serializer ser;
        ser.serialize_uint16(100);
        ser.serialize_string("steer_left");
        request.set_payload(ser.get_buffer());

        std::cout << "  Sending SOME/IP RPC request: service=0x2000, method=0x0001\n";
        gateway.on_someip_message(request);
    }

    // ---- Simulate iceoryx2 → SOME/IP (incoming sample) ----
    {
        std::cout << "\n=== iceoryx2 sample → SOME/IP event injection ===\n";
        Iceoryx2MessageTranslator translator;
        Iceoryx2Envelope env;
        env.service_id = 0x1234;
        env.method_id = 0x8001;
        env.is_notification = true;

        someip::serialization::Serializer ser;
        ser.serialize_double(55.123);
        ser.serialize_double(8.456);
        env.payload = ser.get_buffer();

        auto bytes = translator.serialize_envelope(env);
        std::cout << "  Injecting iceoryx2 sample: "
                  << bytes.size() << " bytes\n";
        gateway.inject_iceoryx2_sample("vehicle/ecu1/someip/0x1234/0x0001", bytes);
    }

    // ---- Print statistics ----
    auto stats = gateway.get_stats();
    std::cout << "\n=== Gateway Statistics ===\n";
    std::cout << "  SOME/IP → iceoryx2: " << stats.messages_someip_to_external << " messages, "
              << stats.bytes_someip_to_external << " bytes\n";
    std::cout << "  iceoryx2 → SOME/IP: " << stats.messages_external_to_someip << " messages, "
              << stats.bytes_external_to_someip << " bytes\n";
    std::cout << "  Translation errors: " << stats.translation_errors << "\n";
    std::cout << "  SD offers sent:     " << gateway.get_sd_offer_count() << "\n";
    std::cout << "  Uptime:             " << stats.uptime().count() << " ms\n";

    gateway.stop();
    std::cout << "\nGateway stopped.\n";
    return 0;
}
