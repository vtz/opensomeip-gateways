/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/dbus/dbus_gateway.h"

#include "events/event_types.h"
#include "someip/types.h"

#ifdef HAVE_SYSTEMD
#include <systemd/sd-bus.h>
#endif

namespace opensomeip {
namespace gateway {
namespace dbus {

class DbusGateway::UdpBridgeListener : public someip::transport::ITransportListener {
public:
    explicit UdpBridgeListener(DbusGateway& gateway) : gateway_(gateway) {
    }

    void on_message_received(someip::MessagePtr message,
                             const someip::transport::Endpoint&) override {
        if (message) {
            gateway_.on_someip_message(*message);
        }
    }

    void on_connection_lost(const someip::transport::Endpoint&) override {
    }

    void on_connection_established(const someip::transport::Endpoint&) override {
    }

    void on_error(someip::Result) override {
    }

private:
    DbusGateway& gateway_;
};

#ifdef HAVE_SYSTEMD

struct DbusSlotCtx {
    DbusGateway* gateway{nullptr};
    uint16_t service_id{0};
    uint16_t instance_id{0};
};

static int dbus_handle_invoke(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
    auto* ctx = static_cast<DbusSlotCtx*>(userdata);
    uint16_t method_id = 0;
    size_t n = 0;
    const void* p = nullptr;
    int r = sd_bus_message_read(m, "qay", &method_id, &n, &p);
    if (r < 0) {
        sd_bus_error_set_errno(ret_error, -r);
        return r;
    }
    std::vector<uint8_t> payload;
    if (n > 0 && p != nullptr) {
        const auto* b = static_cast<const uint8_t*>(p);
        payload.assign(b, b + n);
    }
    if (ctx->gateway) {
        ctx->gateway->emit_external_rpc(ctx->service_id, ctx->instance_id, method_id, payload);
    }
    return sd_bus_reply_method_return(m, "ay", 0, nullptr);
}

static const sd_bus_vtable dbus_gateway_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("InvokeRpc", "qay", "ay", dbus_handle_invoke, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("SomeipNotification", "qay", 0),
    SD_BUS_SIGNAL("SomeipRequest", "qay", 0),
    SD_BUS_VTABLE_END
};

#endif

class DbusGateway::Impl {
public:
    Impl() = default;

    void set_gateway(DbusGateway* g) {
        gateway_ = g;
    }

    void set_enabled(bool e) {
        enabled_ = e;
    }

    someip::Result start() {
        if (!enabled_ || gateway_ == nullptr) {
            return someip::Result::SUCCESS;
        }
#ifdef HAVE_SYSTEMD
        int r = (gateway_->get_dbus_config().bus_type == DbusBusType::SYSTEM) ? sd_bus_open_system(&bus_)
                                                                               : sd_bus_open_user(&bus_);
        if (r < 0) {
            return someip::Result::NOT_INITIALIZED;
        }
        for (const auto& map : gateway_->get_service_mappings()) {
            register_one(map);
        }
#endif
        return someip::Result::SUCCESS;
    }

    void stop() {
#ifdef HAVE_SYSTEMD
        slots_.clear();
        ctx_storage_.clear();
        if (bus_ != nullptr) {
            sd_bus_flush_close_unref(bus_);
            bus_ = nullptr;
        }
#endif
    }

    someip::Result poll(std::chrono::milliseconds timeout) {
        if (!enabled_) {
            return someip::Result::SUCCESS;
        }
#ifdef HAVE_SYSTEMD
        if (bus_ == nullptr) {
            return someip::Result::SUCCESS;
        }
        uint64_t usec = static_cast<uint64_t>(timeout.count()) * 1000ULL;
        int r = sd_bus_wait(bus_, usec);
        if (r < 0) {
            return someip::Result::NOT_INITIALIZED;
        }
        r = sd_bus_process(bus_, nullptr);
        if (r < 0) {
            return someip::Result::NOT_INITIALIZED;
        }
#else
        (void)timeout;
#endif
        return someip::Result::SUCCESS;
    }

    someip::Result emit_signal(const std::string& path,
                               const std::string& iface,
                               const char* member,
                               uint16_t method_id,
                               const std::vector<uint8_t>& payload) {
        if (!enabled_) {
            return someip::Result::SUCCESS;
        }
#ifdef HAVE_SYSTEMD
        if (bus_ == nullptr || gateway_ == nullptr) {
            return someip::Result::SUCCESS;
        }
        size_t n = payload.size();
        const void* data = n > 0 ? payload.data() : nullptr;
        int r = sd_bus_emit_signal(bus_, path.c_str(), iface.c_str(), member, "qay", method_id, n,
                                   data);
        if (r < 0) {
            return someip::Result::NOT_INITIALIZED;
        }
#else
        (void)path;
        (void)iface;
        (void)member;
        (void)method_id;
        (void)payload;
#endif
        return someip::Result::SUCCESS;
    }

private:
#ifdef HAVE_SYSTEMD
    void register_one(const ServiceMapping& map) {
        if (bus_ == nullptr || gateway_ == nullptr) {
            return;
        }
        const DbusTranslator& tr = gateway_->translator();
        const std::string path = tr.build_object_path(map.someip_service_id, map.someip_instance_id);
        const std::string iface = DbusTranslator::build_interface_name(map.someip_service_id);
        const std::string bname = tr.build_bus_name(map.someip_service_id, map.someip_instance_id);

        sd_bus_request_name(bus_, bname.c_str(),
                            SD_BUS_NAME_ALLOW_REPLACEMENT | SD_BUS_NAME_REPLACE_EXISTING);

        auto ctx = std::make_unique<DbusSlotCtx>();
        ctx->gateway = gateway_;
        ctx->service_id = map.someip_service_id;
        ctx->instance_id = map.someip_instance_id;
        DbusSlotCtx* raw = ctx.get();

        sd_bus_slot* slot = nullptr;
        int r = sd_bus_add_object_vtable(bus_, &slot, path.c_str(), iface.c_str(), dbus_gateway_vtable,
                                         raw);
        if (r < 0) {
            return;
        }
        ctx_storage_.push_back(std::move(ctx));
        slots_.push_back(std::unique_ptr<sd_bus_slot, SlotDeleter>(slot));
    }

    sd_bus* bus_{nullptr};
    std::vector<std::unique_ptr<DbusSlotCtx>> ctx_storage_;

    struct SlotDeleter {
        void operator()(sd_bus_slot* s) const {
            if (s != nullptr) {
                sd_bus_slot_unref(s);
            }
        }
    };
    std::vector<std::unique_ptr<sd_bus_slot, SlotDeleter>> slots_;
#endif

    DbusGateway* gateway_{nullptr};
    bool enabled_{true};
};

DbusGateway::DbusGateway(DbusConfig config)
    : GatewayBase("opensomeip-dbus-gateway", "dbus"),
      config_(std::move(config)),
      translator_(config_.bus_name_prefix, config_.object_path_prefix),
      impl_(std::make_unique<Impl>()) {
    impl_->set_gateway(this);
}

DbusGateway::~DbusGateway() {
    if (is_running()) {
        stop();
    }
}

void DbusGateway::set_someip_outbound_sink(SomeipOutboundSink sink) {
    std::lock_guard<std::mutex> lk(sink_mutex_);
    someip_outbound_sink_ = std::move(sink);
}

void DbusGateway::enable_someip_udp_bridge(const someip::transport::Endpoint& bind_ep,
                                           const someip::transport::UdpTransportConfig& cfg) {
    udp_transport_ = std::make_unique<someip::transport::UdpTransport>(bind_ep, cfg);
    udp_listener_ = std::make_unique<UdpBridgeListener>(*this);
    udp_transport_->set_listener(udp_listener_.get());
}

void DbusGateway::attach_rpc_client(const std::shared_ptr<someip::rpc::RpcClient>& rpc) {
    rpc_client_ = rpc;
}

void DbusGateway::attach_rpc_server(const std::shared_ptr<someip::rpc::RpcServer>& server) {
    rpc_server_ = server;
}

void DbusGateway::attach_sd_client(const std::shared_ptr<someip::sd::SdClient>& sd) {
    sd_client_ = sd;
}

void DbusGateway::attach_sd_server(const std::shared_ptr<someip::sd::SdServer>& sd_server) {
    sd_server_ = sd_server;
}

bool DbusGateway::register_event_publisher(uint16_t service_id, uint16_t instance_id,
                                           std::unique_ptr<someip::events::EventPublisher> publisher) {
    const uint64_t key = (static_cast<uint64_t>(service_id) << 16) | instance_id;
    event_publishers_[key] = std::move(publisher);
    return true;
}

bool DbusGateway::subscribe_someip_eventgroup(uint16_t service_id, uint16_t instance_id,
                                              uint16_t eventgroup_id) {
    if (!event_subscriber_) {
        event_subscriber_ =
            std::make_unique<someip::events::EventSubscriber>(config_.rpc_client_id);
        event_subscriber_->initialize();
    }

    return event_subscriber_->subscribe_eventgroup(
        service_id, instance_id, eventgroup_id,
        [this](const someip::events::EventNotification& n) {
            someip::MessageId msg_id(n.service_id, n.event_id);
            someip::RequestId req_id(n.client_id, n.session_id);
            someip::Message msg(msg_id, req_id, someip::MessageType::NOTIFICATION);
            msg.set_payload(n.event_data);
            on_someip_message(msg);
        });
}

someip::Result DbusGateway::start() {
    if (udp_transport_ != nullptr) {
        const auto r = udp_transport_->start();
        if (r != someip::Result::SUCCESS) {
            return r;
        }
    }

    if (sd_client_) {
        sd_client_->initialize();
    }
    if (sd_server_) {
        sd_server_->initialize();
    }

    const auto r = impl_->start();
    if (r != someip::Result::SUCCESS) {
        return r;
    }

    set_running(true);
    return someip::Result::SUCCESS;
}

someip::Result DbusGateway::stop() {
    if (event_subscriber_) {
        event_subscriber_->shutdown();
    }
    if (rpc_client_) {
        rpc_client_->shutdown();
    }
    if (rpc_server_) {
        rpc_server_->shutdown();
    }
    if (sd_client_) {
        sd_client_->shutdown();
    }
    if (sd_server_) {
        sd_server_->shutdown();
    }
    if (udp_transport_) {
        udp_transport_->stop();
    }

    impl_->stop();
    set_running(false);
    return someip::Result::SUCCESS;
}

someip::Result DbusGateway::on_someip_message(const someip::Message& msg) {
    if (!is_running()) {
        return someip::Result::NOT_INITIALIZED;
    }

    const ServiceMapping* mapping =
        find_mapping_for_service(msg.get_service_id(), config_.default_someip_instance_id);
    if (mapping == nullptr) {
        mapping = find_mapping_for_service(msg.get_service_id(), 0x0001);
    }
    if (mapping == nullptr) {
        return someip::Result::SERVICE_NOT_FOUND;
    }

    if (!should_forward_to_external(*mapping)) {
        return someip::Result::SUCCESS;
    }

    const auto mt = msg.get_message_type();
    const bool is_notification = (mt == someip::MessageType::NOTIFICATION) ||
                                 (mt == someip::MessageType::TP_NOTIFICATION);
    const bool is_request = msg.is_request();

    if (!is_notification && !is_request) {
        return someip::Result::SUCCESS;
    }

    const std::string path =
        translator_.build_object_path(mapping->someip_service_id, mapping->someip_instance_id);
    const std::string iface = DbusTranslator::build_interface_name(mapping->someip_service_id);
    const char* signal_name =
        is_notification ? "SomeipNotification" : "SomeipRequest";
    const auto r =
        impl_->emit_signal(path, iface, signal_name, msg.get_method_id(), msg.get_payload());
    if (r == someip::Result::SUCCESS) {
        record_someip_to_external(msg.get_payload().size());
    } else {
        record_translation_error();
    }
    return r;
}

someip::Result DbusGateway::poll_dbus(std::chrono::milliseconds timeout) {
    return impl_->poll(timeout);
}

someip::Result DbusGateway::emit_external_rpc(uint16_t service_id, uint16_t instance_id,
                                               uint16_t method_id,
                                               const std::vector<uint8_t>& payload) {
    if (!is_running()) {
        return someip::Result::NOT_INITIALIZED;
    }

    const ServiceMapping* m = find_mapping_for_service(service_id, instance_id);
    if (m == nullptr) {
        m = find_mapping_for_service(service_id, config_.default_someip_instance_id);
    }
    if (m == nullptr) {
        return someip::Result::SERVICE_NOT_FOUND;
    }
    if (!should_forward_to_someip(*m)) {
        return someip::Result::SUCCESS;
    }

    someip::MessageId mid(service_id, method_id);
    const uint16_t sess = next_session_.fetch_add(1, std::memory_order_relaxed);
    someip::RequestId rid(config_.rpc_client_id, sess);
    someip::Message out(mid, rid, someip::MessageType::REQUEST);
    out.set_payload(payload);

    record_external_to_someip(payload.size());

    {
        std::lock_guard<std::mutex> lk(sink_mutex_);
        if (someip_outbound_sink_) {
            someip_outbound_sink_(out);
        }
    }
    if (external_message_callback_) {
        external_message_callback_(service_id, method_id, payload);
    }
    return someip::Result::SUCCESS;
}

void DbusGateway::test_set_dbus_enabled(bool enabled) {
    impl_->set_enabled(enabled);
}

}  // namespace dbus
}  // namespace gateway
}  // namespace opensomeip
