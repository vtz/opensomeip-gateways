/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include <iostream>
#include <vector>

#include "opensomeip/gateway/dbus/dbus_gateway.h"
#include "opensomeip/gateway/dbus/dbus_translator.h"
#include "serialization/serializer.h"
#include "someip/message.h"

int main() {
    using namespace opensomeip::gateway;
    using namespace opensomeip::gateway::dbus;

    // --- Configure D-Bus gateway ---
    DbusConfig config;
    config.bus_type = DbusBusType::SESSION;
    config.bus_name_prefix = "com.opensomeip.gateway";
    config.object_path_prefix = "/com/opensomeip/gateway";
    config.enable_introspection = true;

    DbusGateway gateway(config);

    // --- Map: vehicle network status (SOME/IP events → D-Bus signals) ---
    ServiceMapping network_status;
    network_status.someip_service_id = 0x4500;
    network_status.someip_instance_id = 0x0001;
    network_status.someip_event_group_ids = {0x0001};
    network_status.external_identifier = "NetworkStatus";
    network_status.direction = GatewayDirection::SOMEIP_TO_EXTERNAL;
    gateway.add_service_mapping(network_status);

    // --- Map: BlueZ-style BT control (D-Bus method calls → SOME/IP RPC) ---
    ServiceMapping bluetooth;
    bluetooth.someip_service_id = 0x4600;
    bluetooth.someip_instance_id = 0x0001;
    bluetooth.someip_method_ids = {0x0001, 0x0002};
    bluetooth.external_identifier = "BluetoothControl";
    bluetooth.direction = GatewayDirection::BIDIRECTIONAL;
    gateway.add_service_mapping(bluetooth);

    gateway.start();
    std::cout << "D-Bus Gateway started\n";
    std::cout << "  Bus: " << (config.bus_type == DbusBusType::SESSION ? "session" : "system")
              << "\n\n";

    // --- Show D-Bus naming ---
    DbusTranslator translator(config.bus_name_prefix, config.object_path_prefix);
    std::cout << "=== D-Bus Interface Layout ===\n";
    std::cout << "  Bus name:    " << translator.build_bus_name(0x4500, 0x0001) << "\n";
    std::cout << "  Object path: " << translator.build_object_path(0x4500, 0x0001) << "\n";
    std::cout << "  Interface:   " << translator.build_interface_name(0x4500) << "\n";

    auto xml = translator.generate_introspection_xml(0x4500, 0x0001, {0x0001}, {0x8001});
    std::cout << "\n  Introspection XML:\n" << xml << "\n";

    // --- Simulate SOME/IP notification → D-Bus signal ---
    {
        std::cout << "\n=== SOME/IP Event → D-Bus Signal ===\n";
        someip::MessageId msg_id(0x4500, 0x8001);
        someip::RequestId req_id(0x0000, 0x0001);
        someip::Message notification(msg_id, req_id, someip::MessageType::NOTIFICATION);

        someip::serialization::Serializer ser;
        ser.serialize_string("wlan0");
        ser.serialize_uint8(1);
        ser.serialize_string("192.168.1.42");
        notification.set_payload(ser.get_buffer());

        auto result = gateway.on_someip_message(notification);
        std::cout << "  Signal emitted: "
                  << (result == someip::Result::SUCCESS ? "OK" : "N/A (no D-Bus link)") << "\n";
    }

    // --- Simulate SOME/IP RPC → D-Bus method ---
    {
        std::cout << "\n=== SOME/IP RPC → D-Bus Method ===\n";
        someip::MessageId msg_id(0x4600, 0x0001);
        someip::RequestId req_id(0x0042, 0x0001);
        someip::Message request(msg_id, req_id, someip::MessageType::REQUEST);

        someip::serialization::Serializer ser;
        ser.serialize_string("scan");
        ser.serialize_uint16(30);
        request.set_payload(ser.get_buffer());

        auto result = gateway.on_someip_message(request);
        std::cout << "  Method call forwarded: "
                  << (result == someip::Result::SUCCESS ? "OK" : "N/A (no D-Bus link)") << "\n";
    }

    // --- Print statistics ---
    auto stats = gateway.get_stats();
    std::cout << "\n=== Gateway Statistics ===\n";
    std::cout << "  SOME/IP → D-Bus: " << stats.messages_someip_to_external << " messages\n";
    std::cout << "  D-Bus → SOME/IP: " << stats.messages_external_to_someip << " messages\n";
    std::cout << "  Translation errors: " << stats.translation_errors << "\n";

    gateway.stop();
    std::cout << "\nGateway stopped.\n";
    return 0;
}
