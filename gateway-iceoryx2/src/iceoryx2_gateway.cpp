/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/iceoryx2/iceoryx2_gateway.h"

#include "common/result.h"
#include "events/event_types.h"
#include "rpc/rpc_types.h"
#include "sd/sd_types.h"
#include "someip/types.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <sstream>

#ifdef OPENSOMEIP_GATEWAY_HAS_ICEORYX2
// Example: #include <iox2/service_builder.hpp>
// Use iox2::ServiceBuilder, publishers/subscribers, or client/server ports here.
#endif

namespace opensomeip {
namespace gateway {
namespace iceoryx2 {

namespace {

struct Iceoryx2PendingRpc {
    std::mutex mutex;
    std::condition_variable cv;
    bool done{false};
    std::vector<uint8_t> payload;
    someip::rpc::RpcResult result{someip::rpc::RpcResult::INTERNAL_ERROR};
};

class UdpBridgeListener : public someip::transport::ITransportListener {
public:
    explicit UdpBridgeListener(Iceoryx2Gateway* gw) : gw_(gw) {}

    void on_message_received(someip::MessagePtr message,
                             const someip::transport::Endpoint&) override {
        if (gw_ != nullptr && message) {
            (void)gw_->on_someip_message(*message);
        }
    }

    void on_connection_lost(const someip::transport::Endpoint&) override {}
    void on_connection_established(const someip::transport::Endpoint&) override {}
    void on_error(someip::Result) override {}

private:
    Iceoryx2Gateway* gw_;
};

}  // namespace

Iceoryx2Gateway::Iceoryx2Gateway(Iceoryx2Config config)
    : GatewayBase(config.gateway_name, "iceoryx2"), config_(std::move(config)) {}

Iceoryx2Gateway::~Iceoryx2Gateway() {
    (void)stop();
}

void Iceoryx2Gateway::set_someip_outbound_sink(SomeipOutboundSink sink) {
    std::lock_guard<std::mutex> lk(sink_mutex_);
    someip_outbound_sink_ = std::move(sink);
}

void Iceoryx2Gateway::set_iceoryx2_outbound_hook(Iceoryx2OutboundHook hook) {
    std::lock_guard<std::mutex> lk(hook_mutex_);
    outbound_hook_ = std::move(hook);
}

const ServiceMapping* Iceoryx2Gateway::resolve_mapping(const someip::Message& msg) const {
    const ServiceMapping* m =
        find_mapping_for_service(msg.get_service_id(), config_.default_someip_instance_id);
    if (m != nullptr) {
        return m;
    }
    for (const auto& sm : get_service_mappings()) {
        if (sm.someip_service_id == msg.get_service_id()) {
            return &sm;
        }
    }
    return nullptr;
}

size_t Iceoryx2Gateway::simulated_outbound_depth() const {
    std::lock_guard<std::mutex> lk(simulated_out_mutex_);
    return simulated_outbound_.size();
}

bool Iceoryx2Gateway::pop_simulated_outbound(std::string* iceoryx2_name,
                                             std::vector<uint8_t>* sample) {
    std::lock_guard<std::mutex> lk(simulated_out_mutex_);
    if (simulated_outbound_.empty()) {
        return false;
    }
    auto front = std::move(simulated_outbound_.front());
    simulated_outbound_.pop_front();
    if (iceoryx2_name != nullptr) {
        *iceoryx2_name = std::move(front.first);
    }
    if (sample != nullptr) {
        *sample = std::move(front.second);
    }
    return true;
}

someip::Result Iceoryx2Gateway::publish_toward_iceoryx2(const std::string& iceoryx2_name,
                                                       const std::vector<uint8_t>& sample,
                                                       const ServiceMapping* mapping) {
    if (mapping != nullptr && should_forward_to_external(*mapping)) {
        record_someip_to_external(sample.size());
    } else if (mapping == nullptr) {
        record_someip_to_external(sample.size());
    }

    {
        std::lock_guard<std::mutex> lk(hook_mutex_);
        if (outbound_hook_) {
            outbound_hook_(iceoryx2_name, sample);
        }
    }

    if (config_.use_inprocess_shm_simulation) {
        std::lock_guard<std::mutex> lk(simulated_out_mutex_);
        while (simulated_outbound_.size() >= config_.simulated_outbound_queue_cap) {
            simulated_outbound_.pop_front();
        }
        simulated_outbound_.push_back({iceoryx2_name, sample});
    }

    return someip::Result::SUCCESS;
}

void Iceoryx2Gateway::complete_pending_rpc(const std::string& correlation_id,
                                           const std::vector<uint8_t>& response_payload,
                                           someip::rpc::RpcResult rpc_result) {
    if (correlation_id.empty()) {
        return;
    }
    std::shared_ptr<Iceoryx2PendingRpc> slot;
    {
        std::lock_guard<std::mutex> g(pending_rpc_mutex_);
        auto it = pending_by_correlation_.find(correlation_id);
        if (it == pending_by_correlation_.end()) {
            return;
        }
        slot = it->second;
    }
    {
        std::lock_guard<std::mutex> lk(slot->mutex);
        slot->payload = response_payload;
        slot->result = rpc_result;
        slot->done = true;
    }
    slot->cv.notify_all();
}

void Iceoryx2Gateway::handle_incoming_envelope(const Iceoryx2Envelope& env,
                                               const std::string& iceoryx2_name) {
    const ServiceMapping* mapping = find_mapping_for_service(env.service_id, env.instance_id);
    if (mapping == nullptr) {
        for (const auto& sm : get_service_mappings()) {
            if (sm.someip_service_id == env.service_id) {
                mapping = &sm;
                break;
            }
        }
    }

    if (env.message_type == someip::MessageType::RESPONSE ||
        env.message_type == someip::MessageType::ERROR) {
        complete_pending_rpc(env.correlation_id, env.payload,
                             env.message_type == someip::MessageType::ERROR
                                 ? someip::rpc::RpcResult::INTERNAL_ERROR
                                 : someip::rpc::RpcResult::SUCCESS);
    }

    if (mapping != nullptr && should_forward_to_someip(*mapping)) {
        if (env.message_type == someip::MessageType::NOTIFICATION) {
            someip::Message sm = translator_.envelope_to_someip(env);
            ServiceKey key = make_key(env.service_id, env.instance_id);
            auto it = event_publishers_.find(key);
            if (it != event_publishers_.end() && it->second != nullptr) {
                (void)it->second->publish_event(env.method_or_event_id, sm.get_payload());
                record_external_to_someip(sm.get_payload().size());
            }
        } else if (env.message_type == someip::MessageType::REQUEST ||
                   env.message_type == someip::MessageType::REQUEST_NO_RETURN) {
            someip::rpc::RpcSyncResult r = rpc_client_->call_method_sync(
                env.service_id, env.method_or_event_id, env.payload, someip::rpc::RpcTimeout{});
            someip::MessageId mid(env.service_id, env.method_or_event_id);
            someip::RequestId rid(env.client_id, env.session_id);
            const auto mt = (r.result == someip::rpc::RpcResult::SUCCESS)
                                ? someip::MessageType::RESPONSE
                                : someip::MessageType::ERROR;
            someip::Message resp(mid, rid, mt);
            resp.set_payload(r.return_values);
            {
                std::lock_guard<std::mutex> lk(sink_mutex_);
                if (someip_outbound_sink_) {
                    someip_outbound_sink_(resp);
                }
            }
            auto out_sample =
                translator_.someip_to_sample(resp, env.instance_id, env.mode);
            std::string rname = Iceoryx2Translator::build_iceoryx2_service_name(
                config_.service_name_prefix, env.service_id, env.instance_id,
                env.method_or_event_id, 'R');
            (void)publish_toward_iceoryx2(rname, out_sample, mapping);
            record_external_to_someip(r.return_values.size());
        }
    }

    if (external_message_callback_) {
        ExternalMessage ext = translator_.envelope_to_external(env, iceoryx2_name);
        external_message_callback_(ext.source_service_id, ext.source_method_id, ext.payload);
    }
}

someip::Result Iceoryx2Gateway::bridge_pub_sub_someip_to_external(const someip::Message& msg,
                                                                 const ServiceMapping& mapping) {
    const char kind =
        (msg.get_message_type() == someip::MessageType::NOTIFICATION) ? 'N' : 'P';
    std::string name = Iceoryx2Translator::build_iceoryx2_service_name(
        config_.service_name_prefix, mapping.someip_service_id, mapping.someip_instance_id,
        msg.get_method_id(), kind);
    return publish_toward_iceoryx2(
        name, translator_.someip_to_sample(msg, mapping.someip_instance_id, mapping.mode),
        &mapping);
}

someip::Result Iceoryx2Gateway::bridge_rpc_someip_to_external(const someip::Message& msg,
                                                             const ServiceMapping& mapping) {
    std::string name = Iceoryx2Translator::build_iceoryx2_service_name(
        config_.service_name_prefix, mapping.someip_service_id, mapping.someip_instance_id,
        msg.get_method_id(), 'R');
    return publish_toward_iceoryx2(
        name, translator_.someip_to_sample(msg, mapping.someip_instance_id, mapping.mode),
        &mapping);
}

void Iceoryx2Gateway::forward_someip_event_notification(
    const someip::events::EventNotification& n, const ServiceMapping& mapping) {

    someip::MessageId mid(n.service_id, n.event_id);
    someip::RequestId rid(n.client_id, n.session_id);
    someip::Message msg(mid, rid, someip::MessageType::NOTIFICATION);
    msg.set_payload(n.event_data);
    (void)bridge_pub_sub_someip_to_external(msg, mapping);
}

someip::rpc::RpcResult Iceoryx2Gateway::rpc_handler_bridge_to_iceoryx2(
    const ServiceMapping& mapping, uint16_t method_id, uint16_t client_id, uint16_t session_id,
    const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {

    someip::MessageId msg_id(mapping.someip_service_id, method_id);
    someip::RequestId rid(client_id, session_id);
    someip::Message msg(msg_id, rid, someip::MessageType::REQUEST);
    msg.set_payload(in);

    std::ostringstream corr;
    corr << std::hex << std::setfill('0') << std::setw(4) << client_id << "-" << std::setw(4)
         << session_id;
    const std::string correlation_id = corr.str();

    auto slot = std::make_shared<Iceoryx2PendingRpc>();
    {
        std::lock_guard<std::mutex> g(pending_rpc_mutex_);
        pending_by_correlation_[correlation_id] = slot;
    }

    auto sample = translator_.someip_to_sample(msg, mapping.someip_instance_id, mapping.mode);
    std::string io_name = Iceoryx2Translator::build_iceoryx2_service_name(
        config_.service_name_prefix, mapping.someip_service_id, mapping.someip_instance_id,
        method_id, 'R');

    const someip::Result pub_res = publish_toward_iceoryx2(io_name, sample, &mapping);
    if (!someip::is_success(pub_res)) {
        std::lock_guard<std::mutex> g(pending_rpc_mutex_);
        pending_by_correlation_.erase(correlation_id);
        return someip::rpc::RpcResult::INTERNAL_ERROR;
    }

    std::unique_lock<std::mutex> lk(slot->mutex);
    const bool ok = slot->cv.wait_for(lk, config_.rpc_bridge_timeout, [&] { return slot->done; });
    std::vector<uint8_t> payload_copy;
    someip::rpc::RpcResult result_copy = someip::rpc::RpcResult::INTERNAL_ERROR;
    if (ok) {
        payload_copy = slot->payload;
        result_copy = slot->result;
    }
    lk.unlock();
    {
        std::lock_guard<std::mutex> g(pending_rpc_mutex_);
        pending_by_correlation_.erase(correlation_id);
    }

    if (!ok) {
        return someip::rpc::RpcResult::TIMEOUT;
    }
    out = std::move(payload_copy);
    return result_copy;
}

void Iceoryx2Gateway::setup_rpc_server_for_mapping(const ServiceMapping& mapping) {
    const uint16_t sid = mapping.someip_service_id;
    if (rpc_servers_.find(sid) == rpc_servers_.end()) {
        auto srv = std::make_unique<someip::rpc::RpcServer>(sid);
        if (!srv->initialize()) {
            return;
        }
        rpc_servers_[sid] = std::move(srv);
    }
    someip::rpc::RpcServer& srv = *rpc_servers_[sid];
    for (uint16_t mid : mapping.someip_method_ids) {
        if (rpc_registered_methods_[sid].count(mid) != 0U) {
            continue;
        }
        (void)srv.register_method(
            mid, [this, mapping, mid](uint16_t client_id, uint16_t session_id,
                                      const std::vector<uint8_t>& in, std::vector<uint8_t>& o) {
                return rpc_handler_bridge_to_iceoryx2(mapping, mid, client_id, session_id, in, o);
            });
        rpc_registered_methods_[sid].insert(mid);
    }
}

void Iceoryx2Gateway::setup_event_subscriptions_for_mapping(const ServiceMapping& mapping) {
    if (!should_forward_to_external(mapping) || event_subscriber_ == nullptr) {
        return;
    }
    for (uint16_t eg : mapping.someip_event_group_ids) {
        const auto tup =
            std::make_tuple(mapping.someip_service_id, mapping.someip_instance_id, eg);
        if (eventgroup_subscriptions_done_.count(tup) != 0U) {
            continue;
        }
        (void)event_subscriber_->subscribe_eventgroup(
            mapping.someip_service_id, mapping.someip_instance_id, eg,
            [this, mapping](const someip::events::EventNotification& n) {
                forward_someip_event_notification(n, mapping);
            });
        eventgroup_subscriptions_done_.insert(tup);
    }
}

someip::Result Iceoryx2Gateway::setup_sd_proxy_for_mapping(const ServiceMapping& mapping) {
    if (sd_client_ == nullptr || sd_server_ == nullptr) {
        return someip::Result::NOT_INITIALIZED;
    }
    const uint16_t sid = mapping.someip_service_id;
    (void)sd_client_->find_service(sid, [this](const std::vector<someip::sd::ServiceInstance>& found) {
        (void)found;
        sd_finds_handled_++;
    });

    someip::sd::ServiceInstance inst(mapping.someip_service_id, mapping.someip_instance_id, 1, 0);
    inst.ip_address = config_.someip_listen_endpoint.address;
    inst.port = config_.someip_listen_endpoint.port;
    const std::string ep =
        inst.ip_address + ":" + std::to_string(static_cast<int>(inst.port));
    if (sd_server_->offer_service(inst, ep, "")) {
        sd_offers_sent_++;
    }
    return someip::Result::SUCCESS;
}

someip::Result Iceoryx2Gateway::start() {
    if (is_running()) {
        return someip::Result::SUCCESS;
    }

    rpc_registered_methods_.clear();
    eventgroup_subscriptions_done_.clear();
    {
        std::lock_guard<std::mutex> lk(simulated_out_mutex_);
        simulated_outbound_.clear();
    }

    event_subscriber_ = std::make_unique<someip::events::EventSubscriber>(config_.someip_client_id);
    if (!event_subscriber_->initialize()) {
        event_subscriber_.reset();
        return someip::Result::NOT_INITIALIZED;
    }

    rpc_client_ = std::make_unique<someip::rpc::RpcClient>(config_.someip_client_id);
    if (!rpc_client_->initialize()) {
        (void)stop();
        return someip::Result::NOT_INITIALIZED;
    }

    const auto& mappings = get_service_mappings();
    for (const auto& m : mappings) {
        if (should_forward_to_someip(m)) {
            const ServiceKey key = make_key(m.someip_service_id, m.someip_instance_id);
            if (event_publishers_.find(key) == event_publishers_.end()) {
                auto pub = std::make_unique<someip::events::EventPublisher>(m.someip_service_id,
                                                                           m.someip_instance_id);
                if (!pub->initialize()) {
                    (void)stop();
                    return someip::Result::NOT_INITIALIZED;
                }
                event_publishers_[key] = std::move(pub);
            }
            someip::events::EventPublisher& ep = *event_publishers_[key];
            for (uint16_t eid : m.someip_method_ids) {
                someip::events::EventConfig ec;
                ec.event_id = eid;
                ec.eventgroup_id =
                    m.someip_event_group_ids.empty() ? static_cast<uint16_t>(0)
                                                       : m.someip_event_group_ids.front();
                (void)ep.register_event(ec);
            }
        }
    }

    for (const auto& m : mappings) {
        setup_rpc_server_for_mapping(m);
        setup_event_subscriptions_for_mapping(m);
    }

    if (config_.enable_sd_proxy) {
        sd_client_ = std::make_unique<someip::sd::SdClient>(config_.sd_config);
        sd_server_ = std::make_unique<someip::sd::SdServer>(config_.sd_config);
        if (!sd_client_->initialize() || !sd_server_->initialize()) {
            (void)stop();
            return someip::Result::NETWORK_ERROR;
        }
        for (const auto& m : mappings) {
            (void)setup_sd_proxy_for_mapping(m);
        }
    }

    if (config_.enable_someip_udp_listener) {
        udp_transport_ = std::make_unique<someip::transport::UdpTransport>(
            config_.someip_listen_endpoint, config_.udp_config);
        udp_listener_ = std::make_unique<UdpBridgeListener>(this);
        udp_transport_->set_listener(udp_listener_.get());
        if (someip::is_error(udp_transport_->start())) {
            (void)stop();
            return someip::Result::NETWORK_ERROR;
        }
    }

    if (config_.enable_someip_tcp_transport) {
        tcp_transport_ = std::make_unique<someip::transport::TcpTransport>(config_.tcp_config);
        if (someip::is_error(tcp_transport_->initialize(config_.someip_listen_endpoint)) ||
            someip::is_error(tcp_transport_->start())) {
            (void)stop();
            return someip::Result::NETWORK_ERROR;
        }
    }

    set_running(true);
    return someip::Result::SUCCESS;
}

someip::Result Iceoryx2Gateway::stop() {
    if (!is_running()) {
        set_running(false);
    } else {
        set_running(false);
    }

    if (udp_transport_ != nullptr) {
        (void)udp_transport_->stop();
        udp_transport_.reset();
    }
    udp_listener_.reset();

    if (tcp_transport_ != nullptr) {
        (void)tcp_transport_->stop();
        tcp_transport_.reset();
    }

    for (auto& p : event_publishers_) {
        if (p.second != nullptr) {
            p.second->shutdown();
        }
    }
    event_publishers_.clear();

    if (event_subscriber_ != nullptr) {
        event_subscriber_->shutdown();
        event_subscriber_.reset();
    }

    for (auto& s : rpc_servers_) {
        if (s.second != nullptr) {
            s.second->shutdown();
        }
    }
    rpc_servers_.clear();
    rpc_registered_methods_.clear();
    eventgroup_subscriptions_done_.clear();

    if (rpc_client_ != nullptr) {
        rpc_client_->shutdown();
        rpc_client_.reset();
    }

    if (sd_client_ != nullptr) {
        sd_client_->shutdown();
        sd_client_.reset();
    }
    if (sd_server_ != nullptr) {
        sd_server_->shutdown();
        sd_server_.reset();
    }

    {
        std::lock_guard<std::mutex> g(pending_rpc_mutex_);
        pending_by_correlation_.clear();
    }

    return someip::Result::SUCCESS;
}

someip::Result Iceoryx2Gateway::on_someip_message(const someip::Message& msg) {
    if (!is_running()) {
        return someip::Result::INVALID_STATE;
    }
    const ServiceMapping* m = resolve_mapping(msg);
    if (m == nullptr) {
        return someip::Result::INVALID_SERVICE_ID;
    }
    if (!should_forward_to_external(*m)) {
        return someip::Result::SUCCESS;
    }

    if (msg.get_message_type() == someip::MessageType::NOTIFICATION) {
        return bridge_pub_sub_someip_to_external(msg, *m);
    }
    if (msg.is_request() || msg.get_message_type() == someip::MessageType::REQUEST_NO_RETURN) {
        return bridge_rpc_someip_to_external(msg, *m);
    }

    std::string name = Iceoryx2Translator::build_iceoryx2_service_name(
        config_.service_name_prefix, m->someip_service_id, m->someip_instance_id,
        msg.get_method_id(), 'R');
    return publish_toward_iceoryx2(
        name, translator_.someip_to_sample(msg, m->someip_instance_id, m->mode), m);
}

void Iceoryx2Gateway::inject_iceoryx2_sample(const std::string& iceoryx2_service_name,
                                            const std::vector<uint8_t>& sample) {
    std::optional<Iceoryx2Envelope> env = translator_.parse_sample(sample);
    if (!env.has_value()) {
        record_translation_error();
        return;
    }
    handle_incoming_envelope(*env, iceoryx2_service_name);
}

}  // namespace iceoryx2
}  // namespace gateway
}  // namespace opensomeip
