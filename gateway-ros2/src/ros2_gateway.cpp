/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/ros2/ros2_gateway.h"

#include <atomic>
#include <memory>
#include <utility>

#ifdef OPENSOMEIP_GATEWAY_ROS2_HAS_RCLCPP
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/u_int8_multi_array.hpp>
#endif

#include "events/event_types.h"
#include "someip/types.h"

namespace opensomeip {
namespace gateway {
namespace ros2 {

#ifdef OPENSOMEIP_GATEWAY_ROS2_HAS_RCLCPP
namespace {

rclcpp::QoS qos_from_profile(const Ros2QosProfile& p) {
    rclcpp::QoS qos(static_cast<int>(p.history_depth));
    if (p.reliable) {
        qos.reliable();
    } else {
        qos.best_effort();
    }
    if (p.durability_transient_local) {
        qos.transient_local();
    }
    return qos;
}

}  // namespace
#endif

class Ros2GatewayImpl {
public:
    explicit Ros2GatewayImpl(Ros2Gateway* owner) : owner_(owner) {
    }

    ~Ros2GatewayImpl() {
        stop();
    }

    void start(const Ros2Config& cfg, const Ros2Translator& translator) {
        cfg_ = cfg;
#ifdef OPENSOMEIP_GATEWAY_ROS2_HAS_RCLCPP
        subs_.clear();
        pubs_.clear();
        node_.reset();

        if (rclcpp::ok()) {
            std::string ns = cfg.ros_namespace;
            if (!ns.empty() && ns.front() != '/') {
                ns.insert(ns.begin(), '/');
            }
            node_ = std::make_shared<rclcpp::Node>(cfg.node_name, ns);

            for (const auto& m : owner_->get_service_mappings()) {
                if (!owner_->should_forward_to_someip(m)) {
                    continue;
                }
                if (m.external_identifier.empty()) {
                    continue;
                }
                Ros2QosProfile q = translator.qos_for_someip_transport(cfg.default_someip_transport);
                const std::string& topic = m.external_identifier;
                auto sub = node_->create_subscription<std_msgs::msg::UInt8MultiArray>(
                    topic, qos_from_profile(q),
                    [this, topic](const std_msgs::msg::UInt8MultiArray::SharedPtr msg) {
                        if (!msg) {
                            return;
                        }
                        std::vector<uint8_t> bytes(msg->data.begin(), msg->data.end());
                        owner_->inject_ros2_message(topic, bytes);
                    });
                subs_.push_back(std::move(sub));
            }
        }
#endif
        (void)translator;
    }

    void stop() {
#ifdef OPENSOMEIP_GATEWAY_ROS2_HAS_RCLCPP
        subs_.clear();
        pubs_.clear();
        node_.reset();
#endif
    }

    void publish(const std::string& topic, const Ros2QosProfile& qos,
                 const std::vector<uint8_t>& data) {
#ifdef OPENSOMEIP_GATEWAY_ROS2_HAS_RCLCPP
        if (node_) {
            auto it = pubs_.find(topic);
            if (it == pubs_.end()) {
                auto pub = node_->create_publisher<std_msgs::msg::UInt8MultiArray>(
                    topic, qos_from_profile(qos));
                it = pubs_.insert({topic, std::move(pub)}).first;
            }
            std_msgs::msg::UInt8MultiArray ros_msg;
            ros_msg.data = data;
            it->second->publish(ros_msg);
            return;
        }
#endif
        owner_->emit_ros2_publish(topic, data);
    }

private:
    Ros2Gateway* owner_{nullptr};
    Ros2Config cfg_{};

#ifdef OPENSOMEIP_GATEWAY_ROS2_HAS_RCLCPP
    rclcpp::Node::SharedPtr node_;
    std::unordered_map<std::string, rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>::SharedPtr>
        pubs_;
    std::vector<rclcpp::Subscription<std_msgs::msg::UInt8MultiArray>::SharedPtr> subs_;
#endif
};

class Ros2Gateway::UdpBridgeListener : public someip::transport::ITransportListener {
public:
    explicit UdpBridgeListener(Ros2Gateway& gateway) : gateway_(gateway) {
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
    Ros2Gateway& gateway_;
};

Ros2Gateway::Ros2Gateway(Ros2Config config)
    : GatewayBase(config.node_name, "ros2"),
      config_(std::move(config)),
      translator_(config_.topic_prefix, config_.ros_namespace),
      impl_(std::make_unique<Ros2GatewayImpl>(this)) {
}

Ros2Gateway::~Ros2Gateway() {
    if (is_running()) {
        stop();
    }
}

void Ros2Gateway::set_someip_outbound_sink(SomeipOutboundSink sink) {
    std::lock_guard<std::mutex> lk(sink_mutex_);
    someip_outbound_sink_ = std::move(sink);
}

void Ros2Gateway::set_ros2_publish_callback(Ros2PublishCallback cb) {
    std::lock_guard<std::mutex> lk(sink_mutex_);
    ros2_publish_callback_ = std::move(cb);
}

void Ros2Gateway::enable_someip_udp_bridge(const someip::transport::Endpoint& bind_ep,
                                           const someip::transport::UdpTransportConfig& cfg) {
    udp_transport_ = std::make_unique<someip::transport::UdpTransport>(bind_ep, cfg);
    udp_listener_ = std::make_unique<UdpBridgeListener>(*this);
    udp_transport_->set_listener(udp_listener_.get());
}

void Ros2Gateway::attach_rpc_client(const std::shared_ptr<someip::rpc::RpcClient>& rpc) {
    rpc_client_ = rpc;
}

void Ros2Gateway::attach_rpc_server(uint16_t service_id,
                                    const std::shared_ptr<someip::rpc::RpcServer>& server) {
    rpc_servers_[service_id] = server;
}

void Ros2Gateway::attach_sd_client(const std::shared_ptr<someip::sd::SdClient>& sd) {
    sd_client_ = sd;
}

void Ros2Gateway::attach_sd_server(const std::shared_ptr<someip::sd::SdServer>& sd_server) {
    sd_server_ = sd_server;
}

bool Ros2Gateway::register_event_publisher(uint16_t service_id, uint16_t instance_id,
                                           std::unique_ptr<someip::events::EventPublisher> publisher) {
    const uint64_t key = (static_cast<uint64_t>(service_id) << 16) | instance_id;
    event_publishers_[key] = std::move(publisher);
    return true;
}

bool Ros2Gateway::subscribe_someip_eventgroup(uint16_t service_id, uint16_t instance_id,
                                              uint16_t eventgroup_id) {
    if (!event_subscriber_) {
        event_subscriber_ =
            std::make_unique<someip::events::EventSubscriber>(config_.rpc_client_id);
        event_subscriber_->initialize();
    }

    return event_subscriber_->subscribe_eventgroup(
        service_id, instance_id, eventgroup_id,
        [this](const someip::events::EventNotification& n) {
            someip::Message msg(
                someip::MessageId{n.service_id, n.event_id},
                someip::RequestId{n.client_id, n.session_id}, someip::MessageType::NOTIFICATION,
                someip::ReturnCode::E_OK);
            msg.set_payload(n.event_data);
            on_someip_message(msg);
        });
}

someip::Result Ros2Gateway::start() {
    if (config_.enable_udp_transport && !udp_transport_) {
        const someip::transport::Endpoint ep(config_.someip_bind_address, config_.someip_bind_port);
        enable_someip_udp_bridge(ep, {});
    }

    if (udp_transport_) {
        const auto ur = udp_transport_->start();
        if (ur != someip::Result::SUCCESS) {
            return ur;
        }
    }

    if (config_.enable_rpc_client && !rpc_client_) {
        rpc_client_ = std::make_shared<someip::rpc::RpcClient>(config_.rpc_client_id);
    }
    if (rpc_client_) {
        rpc_client_->initialize();
    }

    for (auto& e : rpc_servers_) {
        if (e.second) {
            e.second->initialize();
        }
    }

    if (config_.enable_sd_client && !sd_client_) {
        sd_client_ = std::make_shared<someip::sd::SdClient>(config_.sd_client_config);
    }
    if (config_.enable_sd_server && !sd_server_) {
        sd_server_ = std::make_shared<someip::sd::SdServer>(config_.sd_server_config);
    }
    if (sd_client_) {
        sd_client_->initialize();
    }
    if (sd_server_) {
        sd_server_->initialize();
    }

    for (auto& kv : event_publishers_) {
        if (kv.second) {
            kv.second->initialize();
        }
    }

    if (config_.enable_event_subscriber && !event_subscriber_) {
        event_subscriber_ =
            std::make_unique<someip::events::EventSubscriber>(config_.rpc_client_id);
        event_subscriber_->initialize();
    }

    impl_->start(config_, translator_);

    set_running(true);
    return someip::Result::SUCCESS;
}

someip::Result Ros2Gateway::stop() {
    impl_->stop();

    if (event_subscriber_) {
        event_subscriber_->shutdown();
    }
    if (rpc_client_) {
        rpc_client_->shutdown();
    }
    for (auto& e : rpc_servers_) {
        if (e.second) {
            e.second->shutdown();
        }
    }
    if (sd_client_) {
        sd_client_->shutdown();
    }
    if (sd_server_) {
        sd_server_->shutdown();
    }
    for (auto& kv : event_publishers_) {
        if (kv.second) {
            kv.second->shutdown();
        }
    }
    if (udp_transport_) {
        udp_transport_->stop();
    }

    set_running(false);
    return someip::Result::SUCCESS;
}

someip::Result Ros2Gateway::on_someip_message(const someip::Message& msg) {
    if (!is_running()) {
        return someip::Result::NOT_INITIALIZED;
    }

    const ServiceMapping* mapping =
        find_mapping_for_service(msg.get_service_id(), config_.default_someip_instance_id);
    if (!mapping) {
        mapping = find_mapping_for_service(msg.get_service_id(), 0x0001);
    }
    if (!mapping) {
        return someip::Result::SERVICE_NOT_FOUND;
    }

    if (!should_forward_to_external(*mapping)) {
        return someip::Result::SUCCESS;
    }

    if (config_.use_e2e && config_.e2e_config.has_value()) {
        const auto vr = e2e_.validate(msg, config_.e2e_config.value());
        if (vr != someip::Result::SUCCESS) {
            record_translation_error();
            return vr;
        }
    }

    const std::string topic = route_someip_to_ros2_topic(msg, *mapping);
    const std::vector<uint8_t> payload = translator_.convert_someip_to_ros2_bytes(msg);
    const Ros2QosProfile qos =
        translator_.qos_for_someip_transport(config_.default_someip_transport);

    publish_to_ros2_side(topic, qos, payload);
    record_someip_to_external(payload.size());
    return someip::Result::SUCCESS;
}

std::string Ros2Gateway::route_someip_to_ros2_topic(const someip::Message& msg,
                                                    const ServiceMapping& mapping) const {
    if (!mapping.external_identifier.empty()) {
        return mapping.external_identifier;
    }
    return translator_.build_ros2_topic(mapping.someip_service_id, mapping.someip_instance_id,
                                        msg.get_method_id());
}

const ServiceMapping* Ros2Gateway::find_mapping_by_ros_topic(const std::string& topic) const {
    for (const auto& m : get_service_mappings()) {
        if (m.external_identifier == topic) {
            return &m;
        }
        if (!m.external_identifier.empty() && topic.size() >= m.external_identifier.size() &&
            topic.compare(0, m.external_identifier.size(), m.external_identifier) == 0 &&
            (topic.size() == m.external_identifier.size() || topic[m.external_identifier.size()] == '/')) {
            return &m;
        }
    }
    return nullptr;
}

uint16_t Ros2Gateway::command_method_for_mapping(const ServiceMapping& mapping) const {
    if (!mapping.someip_method_ids.empty()) {
        return mapping.someip_method_ids.front();
    }
    return 0x0001;
}

someip::Result Ros2Gateway::inject_ros2_message(const std::string& ros2_topic,
                                                const std::vector<uint8_t>& payload) {
    if (!is_running()) {
        return someip::Result::NOT_INITIALIZED;
    }

    const ServiceMapping* mapping = find_mapping_by_ros_topic(ros2_topic);
    if (!mapping) {
        return someip::Result::SERVICE_NOT_FOUND;
    }

    if (!should_forward_to_someip(*mapping)) {
        return someip::Result::SUCCESS;
    }

    const uint16_t method_id = command_method_for_mapping(*mapping);
    someip::MessageId mid(mapping->someip_service_id, method_id);
    static std::atomic<uint16_t> session{1};
    const uint16_t sid = session.fetch_add(1, std::memory_order_relaxed);
    someip::RequestId rid(config_.rpc_client_id, sid);
    someip::Message req(mid, rid, someip::MessageType::REQUEST, someip::ReturnCode::E_OK);
    translator_.convert_ros2_bytes_to_someip(payload, req);

    if (config_.use_e2e && config_.e2e_config.has_value()) {
        e2e_.protect(req, config_.e2e_config.value());
    }

    if (rpc_client_) {
        someip::rpc::RpcTimeout tmo;
        const auto sync = rpc_client_->call_method_sync(mapping->someip_service_id, method_id,
                                                        req.get_payload(), tmo);
        (void)sync;
    } else {
        std::lock_guard<std::mutex> lk(sink_mutex_);
        if (someip_outbound_sink_) {
            someip_outbound_sink_(req);
        }
    }

    record_external_to_someip(payload.size());
    return someip::Result::SUCCESS;
}

void Ros2Gateway::publish_to_ros2_side(const std::string& topic, const Ros2QosProfile& qos,
                                       const std::vector<uint8_t>& bytes) {
    impl_->publish(topic, qos, bytes);
}

void Ros2Gateway::emit_ros2_publish(const std::string& topic,
                                    const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lk(sink_mutex_);
    if (ros2_publish_callback_) {
        ros2_publish_callback_(topic, payload);
    }
}

}  // namespace ros2
}  // namespace gateway
}  // namespace opensomeip
