/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/zenoh/zenoh_gateway.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include "e2e/e2e_protection.h"
#include "events/event_publisher.h"
#include "events/event_subscriber.h"
#include "events/event_types.h"
#include "rpc/rpc_client.h"
#include "rpc/rpc_server.h"
#include "rpc/rpc_types.h"
#include "sd/sd_client.h"
#include "sd/sd_server.h"
#include "sd/sd_types.h"
#include "serialization/serializer.h"
#include "someip/message.h"
#include "transport/endpoint.h"
#include "transport/udp_transport.h"

#include "zenoh.hxx"

namespace opensomeip {
namespace gateway {
namespace zenoh {

namespace {

someip::sd::SdConfig make_sd_config(const ZenohConfig& zc) {
    someip::sd::SdConfig c;
    c.multicast_address = "239.255.255.250";
    c.multicast_port = 30490;
    c.unicast_address = zc.someip_bind_address;
    c.unicast_port = zc.someip_bind_port;
    return c;
}

std::string mode_str(ZenohSessionMode m) {
    switch (m) {
        case ZenohSessionMode::CLIENT:
            return "client";
        case ZenohSessionMode::ROUTER:
            return "router";
        case ZenohSessionMode::PEER:
        default:
            return "peer";
    }
}


bool liveliness_sample_is_put_or_delete(const ::zenoh::Sample& sample) {
#if defined(Z_SAMPLE_KIND_PUT) && defined(Z_SAMPLE_KIND_DELETE)
    const auto k = sample.get_kind();
    return k == Z_SAMPLE_KIND_PUT || k == Z_SAMPLE_KIND_DELETE;
#else
    (void)sample;
    return true;
#endif
}

}  // namespace

struct ZenohGateway::Impl : someip::transport::ITransportListener {
    explicit Impl(ZenohGateway& owner) : gw_(owner) {}

    ZenohGateway& gw_;
    std::mutex mutex_;

    someip::transport::Endpoint last_sender_;
    std::unique_ptr<someip::transport::UdpTransport> udp_;

    std::unique_ptr<someip::sd::SdClient> sd_client_;
    std::unique_ptr<someip::sd::SdServer> sd_server_;

    std::map<std::pair<uint16_t, uint16_t>, std::unique_ptr<someip::events::EventPublisher>> event_publishers_;
    std::unique_ptr<someip::events::EventSubscriber> event_subscriber_;

    std::unique_ptr<someip::rpc::RpcServer> rpc_server_;
    uint16_t rpc_server_service_id_{0};

    std::unique_ptr<someip::rpc::RpcClient> rpc_client_;

    someip::e2e::E2EProtection e2e_;

    std::unique_ptr<::zenoh::Session> session_;
    std::vector<::zenoh::Publisher> zenoh_publishers_;
    std::vector<::zenoh::Subscriber> zenoh_subscribers_;
    std::vector<::zenoh::Queryable<void>> zenoh_queryables_;
    std::vector<::zenoh::LivelinessToken> liveliness_tokens_;
    std::vector<::zenoh::Subscriber> liveliness_subscribers_;

    void on_message_received(someip::MessagePtr message,
                             const someip::transport::Endpoint& sender) override {
        if (!message) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        last_sender_ = sender;
        const ZenohConfig& zc = gw_.zenoh_config();
        if (zc.enable_e2e_validation) {
            if (e2e_.validate(*message, zc.e2e_config) != someip::Result::SUCCESS) {
                return;
            }
        }
        gw_.on_someip_message(*message);
    }

    void on_connection_lost(const someip::transport::Endpoint& /*endpoint*/) override {}

    void on_connection_established(const someip::transport::Endpoint& /*endpoint*/) override {}

    void on_error(someip::Result /*error*/) override {}

    const ServiceMapping* find_mapping(uint16_t service_id) const {
        for (const auto& m : gw_.get_service_mappings()) {
            if (m.someip_service_id == service_id) {
                return &m;
            }
        }
        return nullptr;
    }

    const ServiceMapping* find_mapping(uint16_t service_id, uint16_t instance_id) const {
        const ServiceMapping* p = gw_.find_mapping_for_service(service_id, instance_id);
        if (p) {
            return p;
        }
        return find_mapping(service_id);
    }

    void publish_to_zenoh(const someip::Message& msg, const ServiceMapping& mapping) {
        if (!session_) {
            return;
        }
        ExternalMessage ext = gw_.translator().someip_to_zenoh(msg, gw_.zenoh_config().key_prefix, mapping);
        ::zenoh::Publisher::PutOptions opts;
#if defined(ZENOHCXX_ZENOHC) || (defined(Z_FEATURE_ENCODING_VALUES) && Z_FEATURE_ENCODING_VALUES == 1)
        opts.encoding = ::zenoh::Encoding::Predefined::zenoh_bytes();
#endif
        for (auto& pub : zenoh_publishers_) {
            if (pub.get_keyexpr().as_string_view() == ext.topic_or_key) {
                pub.put(::zenoh::Bytes(ext.payload), std::move(opts));
                return;
            }
        }
        // Fallback: declare an ad-hoc publisher on the exact key for this hop.
        auto p = session_->declare_publisher(::zenoh::KeyExpr(ext.topic_or_key));
        p.put(::zenoh::Bytes(ext.payload), std::move(opts));
    }

    void send_someip_udp(const someip::Message& msg) {
        if (!udp_) {
            return;
        }
        someip::Message out = msg;
        const ZenohConfig& zc = gw_.zenoh_config();
        if (zc.enable_e2e_validation) {
            e2e_.protect(out, zc.e2e_config);
        }
        someip::transport::Endpoint dest(zc.someip_remote_address, zc.someip_remote_port,
                                         someip::transport::TransportProtocol::UDP);
        if (last_sender_.get_port() != 0) {
            dest = last_sender_;
        }
        udp_->send_message(out, dest);
    }

    void on_zenoh_sample(const ::zenoh::Sample& sample) {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string_view key = sample.get_keyexpr().as_string_view();
        const std::string& prefix = gw_.zenoh_config().key_prefix;
        if (key.size() <= prefix.size() || key.compare(0, prefix.size(), prefix) != 0) {
            return;
        }

        ZenohTranslator::ParsedZenohSomeipKey parsed =
            ZenohTranslator::parse_someip_key(std::string(key), prefix);
        if (!parsed.valid || parsed.is_liveliness || parsed.is_rpc_path) {
            return;
        }

        ExternalMessage ext;
        ext.topic_or_key = std::string(key);
        ext.payload.assign(sample.get_payload().begin(), sample.get_payload().end());
        ext.source_service_id = parsed.service_id;
        ext.source_method_id = parsed.method_or_event_id;
        ext.source_instance_id = parsed.instance_id;
        ext.is_notification = true;

        const ServiceMapping* mapping = find_mapping(parsed.service_id, parsed.instance_id);
        if (!mapping || !gw_.should_forward_to_someip(*mapping)) {
            return;
        }

        someip::Message msg = gw_.translator().zenoh_to_someip(ext, parsed.service_id, parsed.instance_id,
                                                               someip::MessageType::NOTIFICATION);
        gw_.record_external_to_someip(ext.payload.size());
        send_someip_udp(msg);
    }

    void on_liveliness_sample(const ::zenoh::Sample& sample) {
        const ZenohConfig& zc = gw_.zenoh_config();
        if (!zc.enable_liveliness_sd_bridge || !sd_client_) {
            return;
        }
        if (!liveliness_sample_is_put_or_delete(sample)) {
            return;
        }
        const std::string key(sample.get_keyexpr().as_string_view());
        ZenohTranslator::ParsedZenohSomeipKey parsed =
            ZenohTranslator::parse_someip_key(key, zc.key_prefix);
        const uint16_t sid = parsed.valid && parsed.is_liveliness ? parsed.service_id : 0;

        sd_client_->find_service(
            sid,
            [](const std::vector<someip::sd::ServiceInstance>& /*found*/) {
            },
            std::chrono::milliseconds{500});
    }

    void handle_rpc_query(const ::zenoh::Query& query, uint16_t service_id, uint16_t method_id) {
        std::vector<uint8_t> wire;
        auto pl = query.get_payload();
        if (pl.has_value()) {
            wire.assign(pl->get().begin(), pl->get().end());
        }

        someip::Message zenoh_envelope;
        std::vector<uint8_t> rpc_params = wire;
        if (gw_.translator().decode_payload(wire, zenoh_envelope)) {
            rpc_params = zenoh_envelope.get_payload();
        }

        ::zenoh::Query::ReplyOptions ro;
#if defined(ZENOHCXX_ZENOHC) || (defined(Z_FEATURE_ENCODING_VALUES) && Z_FEATURE_ENCODING_VALUES == 1)
        ro.encoding = ::zenoh::Encoding::Predefined::zenoh_bytes();
#endif

        if (!rpc_client_) {
            query.reply(query.get_keyexpr(), ::zenoh::Bytes(std::vector<uint8_t>{}), std::move(ro));
            return;
        }

        const auto sync = rpc_client_->call_method_sync(service_id, method_id, rpc_params, {});

        someip::Message resp(someip::MessageId(service_id, method_id),
                             someip::RequestId(gw_.zenoh_config().rpc_client_id, 0x0001),
                             someip::MessageType::RESPONSE, someip::ReturnCode::E_OK);
        if (sync.result == someip::rpc::RpcResult::SUCCESS) {
            resp.set_payload(sync.return_values);
        }
        std::vector<uint8_t> reply_wire = gw_.translator().encode_payload(resp);
        query.reply(query.get_keyexpr(), ::zenoh::Bytes(std::move(reply_wire)), std::move(ro));
    }

    someip::Result start() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (gw_.is_running()) {
            return someip::Result::SUCCESS;
        }

        gw_.translator().set_encoding(gw_.zenoh_config().payload_encoding);
        const ZenohConfig& zc = gw_.zenoh_config();

        if (zc.enable_service_discovery) {
            sd_client_ = std::make_unique<someip::sd::SdClient>(make_sd_config(zc));
            sd_server_ = std::make_unique<someip::sd::SdServer>(make_sd_config(zc));
            sd_client_->initialize();
            sd_server_->initialize();
        }

        if (zc.enable_udp_transport) {
            someip::transport::UdpTransportConfig tcfg;
            tcfg.reuse_address = true;
            udp_ = std::make_unique<someip::transport::UdpTransport>(
                someip::transport::Endpoint(zc.someip_bind_address, zc.someip_bind_port,
                                            someip::transport::TransportProtocol::UDP),
                tcfg);
            udp_->set_listener(this);
            if (udp_->start() != someip::Result::SUCCESS) {
                return someip::Result::NETWORK_ERROR;
            }
        }

        rpc_client_ = std::make_unique<someip::rpc::RpcClient>(zc.rpc_client_id);
        rpc_client_->initialize();

        event_subscriber_ = std::make_unique<someip::events::EventSubscriber>(zc.rpc_client_id);
        event_subscriber_->initialize();

        for (const auto& map : gw_.get_service_mappings()) {
            const auto key = std::make_pair(map.someip_service_id, map.someip_instance_id);
            if (!event_publishers_.count(key)) {
                auto pub = std::make_unique<someip::events::EventPublisher>(map.someip_service_id,
                                                                            map.someip_instance_id);
                pub->initialize();
                for (uint16_t eg : map.someip_event_group_ids) {
                    someip::events::EventConfig ec;
                    ec.eventgroup_id = eg;
                    ec.event_id = static_cast<uint16_t>(0x8000U | (eg & 0x0FFFU));
                    pub->register_event(ec);
                }
                event_publishers_.emplace(key, std::move(pub));
            }

            if (!rpc_server_ && !map.someip_method_ids.empty()) {
                rpc_server_ = std::make_unique<someip::rpc::RpcServer>(map.someip_service_id);
                rpc_server_service_id_ = map.someip_service_id;
                rpc_server_->initialize();
            }
        }

        if (rpc_server_) {
            for (const auto& map : gw_.get_service_mappings()) {
                if (map.someip_service_id != rpc_server_service_id_) {
                    continue;
                }
                for (uint16_t mid : map.someip_method_ids) {
                    rpc_server_->register_method(
                        mid,
                        [this, mid](uint16_t client_id, uint16_t session_id,
                                    const std::vector<uint8_t>& in,
                                    std::vector<uint8_t>& out) -> someip::rpc::RpcResult {
                            someip::Message req(someip::MessageId(rpc_server_service_id_, mid),
                                                someip::RequestId(client_id, session_id),
                                                someip::MessageType::REQUEST);
                            req.set_payload(in);
                            const ServiceMapping* m = find_mapping(rpc_server_service_id_);
                            if (m && gw_.should_forward_to_external(*m)) {
                                publish_to_zenoh(req, *m);
                            }
                            out = in;
                            return someip::rpc::RpcResult::SUCCESS;
                        });
                }
            }
        }

        for (const auto& map : gw_.get_service_mappings()) {
            if (!gw_.should_forward_to_someip(map)) {
                continue;
            }
            for (uint16_t eg : map.someip_event_group_ids) {
                event_subscriber_->subscribe_eventgroup(
                    map.someip_service_id, map.someip_instance_id, eg,
                    [this](const someip::events::EventNotification& n) {
                        someip::Message msg(someip::MessageId(n.service_id, n.event_id),
                                            someip::RequestId(gw_.zenoh_config().rpc_client_id, 0x0001),
                                            someip::MessageType::NOTIFICATION);
                        msg.set_payload(n.event_data);
                        const ServiceMapping* m = find_mapping(n.service_id, n.instance_id);
                        if (m) {
                            publish_to_zenoh(msg, *m);
                        }
                    });
            }
        }

#ifdef ZENOHCXX_ZENOHC
        ::zenoh::init_log_from_env_or("error");
#endif

        ::zenoh::Config zcfg = [&]() {
            if (!zc.zenoh_config_file.empty()) {
#ifdef ZENOHCXX_ZENOHC
                ::zenoh::Config c = ::zenoh::Config::from_file(zc.zenoh_config_file);
                c.insert_json5("mode", "\"" + mode_str(zc.mode) + "\"");
                return c;
#else
                (void)zc;
                return ::zenoh::Config::create_default();
#endif
            }
#ifdef ZENOHCXX_ZENOHC
            return ::zenoh::Config::from_str(zc.to_json5());
#else
            return ::zenoh::Config::create_default();
#endif
        }();

        try {
            session_ = std::make_unique<::zenoh::Session>(::zenoh::Session::open(std::move(zcfg)));
        } catch (const ::zenoh::ZException& e) {
            (void)e;
            return someip::Result::NOT_INITIALIZED;
        }

        for (const auto& map : gw_.get_service_mappings()) {
            if (gw_.should_forward_to_external(map)) {
                for (uint16_t mid : map.someip_method_ids) {
                    std::string k = ZenohTranslator::build_rpc_key(zc.key_prefix, map.someip_service_id,
                                                                   map.someip_instance_id, mid);
                    zenoh_publishers_.push_back(session_->declare_publisher(::zenoh::KeyExpr(k)));
                    zenoh_queryables_.push_back(session_->declare_queryable(
                        k,
                        [this, svc = map.someip_service_id, mid](const ::zenoh::Query& q) {
                            handle_rpc_query(q, svc, mid);
                        },
                        []() {},
                        ::zenoh::Session::QueryableOptions{}));
                }
            }
            if (gw_.should_forward_to_someip(map)) {
                std::string pat = ZenohTranslator::build_instance_pattern(zc.key_prefix, map.someip_service_id,
                                                                          map.someip_instance_id);
                zenoh_subscribers_.push_back(session_->declare_subscriber(
                    ::zenoh::KeyExpr(pat),
                    [this](const ::zenoh::Sample& s) {
                        on_zenoh_sample(s);
                    },
                    ::zenoh::closures::none));
            }

            std::string liv = ZenohTranslator::build_liveliness_key(zc.key_prefix, map.someip_service_id,
                                                                    map.someip_instance_id);
            liveliness_tokens_.push_back(session_->liveliness_declare_token(::zenoh::KeyExpr(liv)));

            if (sd_server_) {
                someip::sd::ServiceInstance inst(map.someip_service_id, map.someip_instance_id);
                sd_server_->offer_service(inst, zc.someip_bind_address + ":" + std::to_string(zc.someip_bind_port),
                                          "");
            }
        }

        if (zc.enable_liveliness_sd_bridge) {
            std::string lk = zc.liveliness_subscription_key.empty()
                                 ? (zc.key_prefix + "/liveliness/**")
                                 : zc.liveliness_subscription_key;
            ::zenoh::Session::LivelinessSubscriberOptions lopts;
            liveliness_subscribers_.push_back(session_->liveliness_declare_subscriber(
                ::zenoh::KeyExpr(lk),
                [this](const ::zenoh::Sample& s) {
                    on_liveliness_sample(s);
                },
                ::zenoh::closures::none, std::move(lopts)));
        }

        gw_.set_running(true);
        return someip::Result::SUCCESS;
    }

    someip::Result stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!gw_.is_running()) {
            return someip::Result::SUCCESS;
        }

        liveliness_subscribers_.clear();
        liveliness_tokens_.clear();
        zenoh_queryables_.clear();
        zenoh_subscribers_.clear();
        zenoh_publishers_.clear();
        session_.reset();

        if (event_subscriber_) {
            event_subscriber_->shutdown();
            event_subscriber_.reset();
        }
        if (rpc_client_) {
            rpc_client_->shutdown();
            rpc_client_.reset();
        }
        if (rpc_server_) {
            rpc_server_->shutdown();
            rpc_server_.reset();
        }
        for (auto& p : event_publishers_) {
            if (p.second) {
                p.second->shutdown();
            }
        }
        event_publishers_.clear();

        if (udp_) {
            udp_->stop();
            udp_.reset();
        }
        if (sd_client_) {
            sd_client_->shutdown();
            sd_client_.reset();
        }
        if (sd_server_) {
            sd_server_->shutdown();
            sd_server_.reset();
        }

        gw_.set_running(false);
        return someip::Result::SUCCESS;
    }
};

ZenohGateway::ZenohGateway(std::string name, ZenohConfig config)
    : GatewayBase(std::move(name), "zenoh"),
      impl_(std::make_unique<Impl>(*this)),
      config_(std::move(config)) {
    translator_.set_encoding(config_.payload_encoding);
}

ZenohGateway::~ZenohGateway() = default;

someip::Result ZenohGateway::start() {
    return impl_->start();
}

someip::Result ZenohGateway::stop() {
    return impl_->stop();
}

someip::Result ZenohGateway::on_someip_message(const someip::Message& msg) {
    const ServiceMapping* mapping = impl_->find_mapping(msg.get_service_id());
    if (!mapping) {
        record_translation_error();
        return someip::Result::INVALID_SERVICE_ID;
    }
    if (!should_forward_to_external(*mapping)) {
        return someip::Result::SUCCESS;
    }

    someip::Message to_publish = msg;
    if (mapping->mode == TranslationMode::TYPED) {
        someip::serialization::Serializer s;
        s.serialize_uint16(msg.get_service_id());
        s.serialize_uint16(msg.get_method_id());
        s.serialize_uint8(static_cast<uint8_t>(msg.get_message_type()));
        s.serialize_uint8(static_cast<uint8_t>(msg.get_return_code()));
        s.serialize_array(msg.get_payload());
        to_publish.set_payload(s.move_buffer());
    }

    impl_->publish_to_zenoh(to_publish, *mapping);
    record_someip_to_external(to_publish.get_payload().size());
    return someip::Result::SUCCESS;
}

}  // namespace zenoh
}  // namespace gateway
}  // namespace opensomeip
