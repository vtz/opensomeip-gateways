/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include <iostream>

#include "opensomeip/gateway/dds/dds_gateway.h"
#include "serialization/serializer.h"
#include "someip/message.h"

int main() {
    using namespace opensomeip::gateway;
    using namespace opensomeip::gateway::dds;

    DdsConfig config;
    config.domain_id = 0;
    config.participant_name = "dds-adaptive-bridge";
    config.rpc_client_id = 0x7200;
    config.someip.default_someip_instance_id = 0x0001;

    DdsGateway gateway(config);

    // Classic ECU service exposed to Adaptive / cloud over DDS
    ServiceMapping vehicle_svc;
    vehicle_svc.someip_service_id = 0x6001;
    vehicle_svc.someip_instance_id = 0x0001;
    vehicle_svc.someip_method_ids = {0x0001, 0x0002};
    vehicle_svc.someip_event_group_ids = {0x0001};
    vehicle_svc.external_identifier = "";
    vehicle_svc.direction = GatewayDirection::BIDIRECTIONAL;
    vehicle_svc.mode = TranslationMode::OPAQUE;
    gateway.add_service_mapping(vehicle_svc);

    gateway.set_someip_outbound_sink([](const someip::Message& m) {
        std::cout << "  DDS → SOME/IP inbound: service=0x" << std::hex << m.get_service_id()
                  << " method=0x" << m.get_method_id() << std::dec
                  << " payload_bytes=" << m.get_payload().size() << "\n";
    });

    if (gateway.start() != someip::Result::SUCCESS) {
        std::cerr << "Failed to start DDS gateway (is Cyclone DDS installed and configured?)\n";
        return 1;
    }

    std::cout << "SOME/IP ↔ DDS bridge running (domain " << config.domain_id << ")\n\n";

    // Event path: NOTIFICATION → DDS topic .../event/...
    {
        std::cout << "=== SOME/IP event (Classic) → DDS ===\n";
        someip::MessageId nid(vehicle_svc.someip_service_id, 0x8001);
        someip::RequestId nrid(0x0000, 0x0001);
        someip::Message notification(nid, nrid, someip::MessageType::NOTIFICATION,
                                     someip::ReturnCode::E_OK);
        someip::serialization::Serializer ser;
        ser.serialize_float(88.5f);
        notification.set_payload(ser.get_buffer());

        const auto r = gateway.on_someip_message(notification);
        std::cout << "  Publish result: " << (r == someip::Result::SUCCESS ? "OK" : "error") << "\n";
    }

    // Method path: REQUEST → DDS topic .../method/...
    {
        std::cout << "\n=== SOME/IP RPC request → DDS ===\n";
        someip::MessageId mid(vehicle_svc.someip_service_id, 0x0001);
        someip::RequestId rid(0x0042, 0x0003);
        someip::Message request(mid, rid, someip::MessageType::REQUEST, someip::ReturnCode::E_OK);
        someip::serialization::Serializer ser2;
        ser2.serialize_uint16(3);
        request.set_payload(ser2.get_buffer());

        const auto r = gateway.on_someip_message(request);
        std::cout << "  Forward result: " << (r == someip::Result::SUCCESS ? "OK" : "error") << "\n";
    }

    // Simulated Adaptive consumer writing the same DDS topic shape back
    {
        std::cout << "\n=== DDS sample → SOME/IP sink (inject) ===\n";
        std::vector<uint8_t> body = {0x01, 0x02, 0x03};
        gateway.inject_dds_sample("", body, vehicle_svc.someip_service_id, vehicle_svc.someip_instance_id,
                                  0x0002,
                                  static_cast<uint8_t>(someip::MessageType::RESPONSE));
    }

    const auto stats = gateway.get_stats();
    std::cout << "\n=== Statistics ===\n";
    std::cout << "  SOME/IP → DDS messages: " << stats.messages_someip_to_external << "\n";
    std::cout << "  DDS → SOME/IP messages: " << stats.messages_external_to_someip << "\n";

    gateway.stop();
    return 0;
}
