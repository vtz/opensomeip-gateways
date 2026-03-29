/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "opensomeip/gateway/zenoh/zenoh_gateway.h"
#include "zenoh.hxx"

namespace {

using opensomeip::gateway::GatewayDirection;
using opensomeip::gateway::ServiceMapping;
using opensomeip::gateway::zenoh::ZenohConfig;
using opensomeip::gateway::zenoh::ZenohGateway;
using opensomeip::gateway::zenoh::ZenohPayloadEncoding;
using opensomeip::gateway::zenoh::ZenohSessionMode;
using opensomeip::gateway::zenoh::ZenohTranslator;

void demo_peer_session(const std::string& listen_tcp) {
    ::zenoh::Config cfg = ::zenoh::Config::create_default();
#ifdef ZENOHCXX_ZENOHC
    cfg.insert_json5("mode", ""peer"");
    cfg.insert_json5("listen/endpoints", "["" + listen_tcp + ""]");
#endif
    try {
        auto session = ::zenoh::Session::open(std::move(cfg));
        const std::string liv_key = "vehicle/demo/liveliness/0x6001/0x0001";
        auto token = session.liveliness_declare_token(::zenoh::KeyExpr(liv_key));
        ::zenoh::Session::LivelinessSubscriberOptions lopts;
        (void)session.liveliness_declare_subscriber(
            ::zenoh::KeyExpr("vehicle/demo/liveliness/**"),
            [](const ::zenoh::Sample& s) {
                std::cout << "[liveliness] " << s.get_keyexpr().as_string_view() << std::endl;
            },
            ::zenoh::closures::none, std::move(lopts));

        const std::string q_key = "vehicle/demo/0x6001/0x0001/rpc/0x0001";
        auto qab = session.declare_queryable(
            q_key,
            [](const ::zenoh::Query& q) {
                ::zenoh::Query::ReplyOptions ro;
                q.reply(q.get_keyexpr(), ::zenoh::Bytes(std::vector<uint8_t>{0x7F}), std::move(ro));
            },
            []() {},
            ::zenoh::Session::QueryableOptions{});

        std::cout << "Peer session: liveliness + queryable on " << q_key << "
";
        std::this_thread::sleep_for(std::chrono::seconds(1));
        (void)token;
        (void)qab;
    } catch (const ::zenoh::ZException& e) {
        std::cerr << "Zenoh peer demo skipped: " << e.what() << "
";
    }
}

ZenohConfig make_client_bridge_config() {
    ZenohConfig z;
    z.mode = ZenohSessionMode::CLIENT;
    z.key_prefix = "vehicle/ecu1/someip";
    z.connect_endpoints.push_back("tcp/127.0.0.1:7447");
    z.payload_encoding = ZenohPayloadEncoding::CBOR;
    z.enable_liveliness_sd_bridge = true;
    z.enable_service_discovery = true;
    z.enable_udp_transport = false;
    return z;
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    std::cout << "OpenSOME/IP Zenoh edge bridge example
";

    ServiceMapping speed;
    speed.someip_service_id = 0x6001;
    speed.someip_instance_id = 0x0001;
    speed.someip_method_ids.push_back(0x0001);
    speed.someip_event_group_ids.push_back(0x0001);
    speed.direction = GatewayDirection::BIDIRECTIONAL;

    ZenohGateway gw("edge-bridge", make_client_bridge_config());
    gw.add_service_mapping(speed);

    std::cout << "Gateway protocol: " << gw.get_protocol() << "
";
    std::cout << "Zenoh key prefix: " << gw.zenoh_config().key_prefix << "
";
    std::cout << "RPC key sample: "
              << ZenohTranslator::build_rpc_key(gw.zenoh_config().key_prefix, speed.someip_service_id,
                                                 speed.someip_instance_id, 0x0001)
              << "
";

    if (const char* p = std::getenv("ZENOH_EDGE_PEER_DEMO")) {
        if (std::string(p) == "1") {
            demo_peer_session("tcp/0.0.0.0:17447");
        }
    }

    return 0;
}
