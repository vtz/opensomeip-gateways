/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/mqtt/mqtt_gateway.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>

#include "events/event_types.h"
#include "someip/types.h"

namespace opensomeip {
namespace gateway {

/** Complete type for @ref MqttGateway::paho_; kept here so @c unique_ptr is well-formed. */
struct MqttGatewayPahoContext {};

// ── OfflineMqttRingBuffer ───────────────────────────────────────────────────

OfflineMqttRingBuffer::OfflineMqttRingBuffer(std::size_t capacity)
    : capacity_(capacity) {
}

bool OfflineMqttRingBuffer::push(BufferedMqttPublish item) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (queue_.size() >= capacity_) {
        queue_.pop_front();
    }
    queue_.push_back(std::move(item));
    return true;
}

bool OfflineMqttRingBuffer::pop(BufferedMqttPublish& out) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (queue_.empty()) {
        return false;
    }
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

std::size_t OfflineMqttRingBuffer::size() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return queue_.size();
}

std::size_t OfflineMqttRingBuffer::capacity() const {
    return capacity_;
}

void OfflineMqttRingBuffer::clear() {
    std::lock_guard<std::mutex> lk(mutex_);
    queue_.clear();
}

// ── MqttGateway ─────────────────────────────────────────────────────────────

MqttGateway::MqttGateway(MqttConfig mqtt_config)
    : GatewayBase(mqtt_config.client_id, "mqtt"),
      mqtt_config_(std::move(mqtt_config)),
      translator_(mqtt_config_.topic_prefix, mqtt_config_.vin),
      offline_buffer_(mqtt_config_.offline_buffer_capacity),
      current_backoff_(mqtt_config_.reconnect_min_delay) {
}

MqttGateway::~MqttGateway() {
    if (is_running()) {
        stop();
    }
}

void MqttGateway::set_someip_outbound_sink(SomeipOutboundSink sink) {
    std::lock_guard<std::mutex> lk(sink_mutex_);
    someip_outbound_sink_ = std::move(sink);
}

void MqttGateway::enable_someip_udp_bridge(
    const someip::transport::Endpoint& bind_ep,
    const someip::transport::UdpTransportConfig& cfg) {
    udp_transport_ = std::make_unique<someip::transport::UdpTransport>(bind_ep, cfg);
    udp_listener_ = std::make_unique<GatewayUdpBridgeListener>(*this);
    udp_transport_->set_listener(udp_listener_.get());
}

void MqttGateway::attach_rpc_client(const std::shared_ptr<someip::rpc::RpcClient>& rpc) {
    rpc_client_ = rpc;
}

void MqttGateway::attach_rpc_server(const std::shared_ptr<someip::rpc::RpcServer>& server) {
    rpc_server_ = server;
}

void MqttGateway::attach_sd_client(const std::shared_ptr<someip::sd::SdClient>& sd) {
    sd_client_ = sd;
}

void MqttGateway::attach_sd_server(const std::shared_ptr<someip::sd::SdServer>& sd_server) {
    sd_server_ = sd_server;
}

bool MqttGateway::register_event_publisher(
    uint16_t service_id, uint16_t instance_id,
    std::unique_ptr<someip::events::EventPublisher> publisher) {

    uint64_t key = (static_cast<uint64_t>(service_id) << 16) | instance_id;
    event_publishers_[key] = std::move(publisher);
    return true;
}

bool MqttGateway::subscribe_someip_eventgroup(uint16_t service_id, uint16_t instance_id,
                                               uint16_t eventgroup_id) {
    if (!event_subscriber_) {
        event_subscriber_ = std::make_unique<someip::events::EventSubscriber>(someip_client_id_);
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

someip::Result MqttGateway::start() {
    if (udp_transport_) {
        auto r = udp_transport_->start();
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

    mqtt_connected_.store(false);
    stop_reconnect_.store(false);
    current_backoff_ = mqtt_config_.reconnect_min_delay;

    set_running(true);
    return someip::Result::SUCCESS;
}

someip::Result MqttGateway::stop() {
    stop_reconnect_.store(true);

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

    set_running(false);
    return someip::Result::SUCCESS;
}

someip::Result MqttGateway::on_someip_message(const someip::Message& msg) {
    if (!is_running()) {
        return someip::Result::NOT_INITIALIZED;
    }

    const auto* mapping = find_mapping_for_service(
        msg.get_service_id(), mqtt_config_.default_someip_instance_id);
    if (!mapping) {
        mapping = find_mapping_for_service(msg.get_service_id(), 0x0001);
    }
    if (!mapping) {
        return someip::Result::SERVICE_NOT_FOUND;
    }

    if (!should_forward_to_external(*mapping)) {
        return someip::Result::SUCCESS;
    }

    if (mqtt_config_.use_e2e && mqtt_config_.e2e_config.has_value()) {
        auto validate_result = e2e_.validate(msg, mqtt_config_.e2e_config.value());
        if (validate_result != someip::Result::SUCCESS) {
            record_translation_error();
            return validate_result;
        }
    }

    std::string topic = route_someip_to_mqtt_topic(msg, *mapping);
    std::vector<uint8_t> payload = translator_.encode_outbound(msg, mqtt_config_.outbound_encoding);

    int qos = msg.is_request() ? qos_for_outbound_rpc(msg.get_method_id())
                               : qos_for_outbound_event(msg.get_method_id());

    std::string response_topic;
    std::vector<uint8_t> correlation_data;
    if (msg.is_request() && mqtt_config_.use_mqtt_v5_request_response) {
        response_topic = topic + "/response";
        correlation_data = translator_.build_correlation_data(
            msg.get_client_id(), msg.get_session_id());
    }

    auto result = publish_mqtt(topic, payload, qos, false, response_topic, correlation_data);

    if (result == someip::Result::SUCCESS) {
        record_someip_to_external(payload.size());
    }

    return result;
}

std::string MqttGateway::route_someip_to_mqtt_topic(
    const someip::Message& msg, const ServiceMapping& mapping) const {
    return translator_.build_mqtt_topic(
        mapping.someip_service_id, mapping.someip_instance_id,
        msg.get_method_id(), msg.is_request());
}

int MqttGateway::qos_for_outbound_event(uint16_t event_id) const {
    auto it = mqtt_config_.qos_by_event_id.find(event_id);
    return (it != mqtt_config_.qos_by_event_id.end()) ? it->second
                                                       : mqtt_config_.default_publish_qos;
}

int MqttGateway::qos_for_outbound_rpc(uint16_t method_id) const {
    auto it = mqtt_config_.qos_by_method_id.find(method_id);
    return (it != mqtt_config_.qos_by_method_id.end()) ? it->second : 1;
}

std::size_t MqttGateway::offline_buffer_occupancy() const {
    return offline_buffer_.size();
}

someip::Result MqttGateway::flush_offline_buffer() {
    BufferedMqttPublish item;
    while (offline_buffer_.pop(item)) {
        publish_mqtt(item.topic, item.payload, item.qos, item.retain, "", {});
    }
    return someip::Result::SUCCESS;
}

void MqttGateway::test_set_mqtt_connected(bool connected) {
    mqtt_connected_.store(connected);
}

someip::Result MqttGateway::publish_mqtt(
    const std::string& topic, const std::vector<uint8_t>& payload,
    int qos, bool retain, const std::string& /*response_topic*/,
    const std::vector<uint8_t>& /*correlation_data*/) {

    if (!mqtt_connected_.load()) {
        BufferedMqttPublish item;
        item.topic = topic;
        item.payload = payload;
        item.qos = qos;
        item.retain = retain;
        offline_buffer_.push(std::move(item));
        return someip::Result::NOT_CONNECTED;
    }

    // With Paho linked, this would call mqtt::async_client::publish().
    // Without Paho, we record the outbound for observability.
    return someip::Result::SUCCESS;
}

void MqttGateway::handle_inbound_mqtt_command(
    const std::string& /*topic*/, const std::vector<uint8_t>& body) {

    auto msg = translator_.decode_inbound(body, mqtt_config_.inbound_encoding);
    if (!msg.is_valid()) {
        record_translation_error();
        return;
    }

    record_external_to_someip(body.size());

    std::lock_guard<std::mutex> lk(sink_mutex_);
    if (someip_outbound_sink_) {
        someip_outbound_sink_(msg);
    }
}

const ServiceMapping* MqttGateway::find_mapping_by_topic_prefix(
    const std::string& topic) const {
    return find_mapping_if([&topic](const ServiceMapping& m) {
        return topic.find(m.external_identifier) != std::string::npos;
    });
}

}  // namespace gateway
}  // namespace opensomeip
