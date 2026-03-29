/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include <iostream>

#include "opensomeip/gateway/grpc/grpc_gateway.h"
#include "serialization/serializer.h"
#include "someip/message.h"

int main() {
    using namespace opensomeip::gateway;
    using namespace opensomeip::gateway::grpc;

    GrpcConfig config;
    config.server_listen_address = "0.0.0.0:50051";
    config.someip_bridge_client_id = 0x7001;

    GrpcGateway gateway(config);

    // Map radar service
    ServiceMapping radar;
    radar.someip_service_id = 0x1234;
    radar.someip_instance_id = 0x0001;
    radar.someip_method_ids = {0x0001, 0x0002};
    radar.someip_event_group_ids = {0x0001};
    radar.external_identifier = "vehicle.gateway.RadarService";
    radar.direction = GatewayDirection::BIDIRECTIONAL;
    gateway.add_service_mapping(radar);

    gateway.start();
    std::cout << "gRPC Gateway started on " << config.server_listen_address << "\n\n";

    // Simulate SOME/IP radar request → gRPC
    {
        std::cout << "=== SOME/IP RPC → gRPC Unary ===\n";
        someip::MessageId msg_id(0x1234, 0x0001);
        someip::RequestId req_id(0x0042, 0x0001);
        someip::Message request(msg_id, req_id, someip::MessageType::REQUEST);

        someip::serialization::Serializer ser;
        ser.serialize_uint32(200);
        ser.serialize_float(45.0f);
        request.set_payload(ser.get_buffer());

        auto result = gateway.on_someip_message(request);
        std::cout << "  Forwarded radar request: "
                  << (result == someip::Result::SUCCESS ? "OK" : "error") << "\n";

        // Show error code mapping
        GrpcTranslator& t = gateway.translator();
        std::cout << "  E_OK → gRPC " << t.someip_to_grpc_status(someip::ReturnCode::E_OK) << "\n";
        std::cout << "  E_NOT_READY → gRPC "
                  << t.someip_to_grpc_status(someip::ReturnCode::E_NOT_READY) << "\n";
    }

    // Simulate SOME/IP event → gRPC server-streaming
    {
        std::cout << "\n=== SOME/IP Event → gRPC Stream ===\n";
        someip::MessageId msg_id(0x1234, 0x8001);
        someip::RequestId req_id(0x0000, 0x0001);
        someip::Message notification(msg_id, req_id, someip::MessageType::NOTIFICATION);

        someip::serialization::Serializer ser;
        ser.serialize_double(42.5);
        ser.serialize_double(10.2);
        ser.serialize_double(-3.7);
        notification.set_payload(ser.get_buffer());

        gateway.on_someip_message(notification);
        std::cout << "  Streamed radar object update\n";
    }

    auto stats = gateway.get_stats();
    std::cout << "\n=== Statistics ===\n";
    std::cout << "  SOME/IP → gRPC: " << stats.messages_someip_to_external << "\n";
    std::cout << "  gRPC → SOME/IP: " << stats.messages_external_to_someip << "\n";

    gateway.stop();
    std::cout << "\nGateway stopped.\n";
    return 0;
}
