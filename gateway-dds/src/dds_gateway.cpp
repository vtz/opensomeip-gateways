/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/dds/dds_gateway.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <dds/dds.h>

#include "vehicle_data.h"

namespace opensomeip {
namespace gateway {
namespace dds {

namespace {

void apply_dds_qos_profile(dds_qos_t* qos, const DdsQosProfile& p) {
    if (p.reliability == DdsReliabilityKind::Reliable) {
        dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_MSECS(100));
    } else {
        dds_qset_reliability(qos, DDS_RELIABILITY_BEST_EFFORT, DDS_MSECS(0));
    }

    switch (p.durability) {
        case DdsDurabilityKind::Volatile:
            dds_qset_durability(qos, DDS_DURABILITY_VOLATILE);
            break;
        case DdsDurabilityKind::TransientLocal:
            dds_qset_durability(qos, DDS_DURABILITY_TRANSIENT_LOCAL);
            break;
        case DdsDurabilityKind::Persistent:
            dds_qset_durability(qos, DDS_DURABILITY_PERSISTENT);
            break;
    }

    if (p.history_kind == DdsHistoryKind::KeepAll) {
        dds_qset_history(qos, DDS_HISTORY_KEEP_ALL, 1);
    } else {
        dds_qset_history(qos, DDS_HISTORY_KEEP_LAST, static_cast<int32_t>(std::max(1, p.history_depth)));
    }

    if (!DdsQosProfile::deadline_is_infinite(p.deadline)) {
        dds_qset_deadline(qos, DDS_MSECS(static_cast<dds_time_t>(p.deadline.count())));
    }
}

dds_entity_t ensure_topic(dds_entity_t participant, const std::string& name,
                          std::unordered_map<std::string, dds_entity_t>& cache) {
    auto it = cache.find(name);
    if (it != cache.end()) {
        return it->second;
    }
    dds_entity_t topic = dds_create_topic(participant, &vehicle_SomeipBridgeSample_desc, name.c_str(),
                                          nullptr, nullptr);
    cache.emplace(name, topic);
    return topic;
}

}  // namespace

struct DdsGatewayImpl {
    explicit DdsGatewayImpl(DdsGateway& owner) : owner_(owner) {
    }

    ~DdsGatewayImpl() {
        stop_internal();
    }

    someip::Result start() {
        if (participant_ != 0) {
            return someip::Result::SUCCESS;
        }

        if (!owner_.config().qos_profile_file.empty()) {
#ifndef _WIN32
            const std::string uri = "file://" + owner_.config().qos_profile_file;
            ::setenv("CYCLONEDDS_URI", uri.c_str(), 1);
#endif
        }

        participant_ = dds_create_participant(static_cast<dds_domainid_t>(owner_.config().domain_id),
                                              nullptr, nullptr);
        if (participant_ <= 0) {
            return someip::Result::NOT_INITIALIZED;
        }

        for (const auto& mapping : owner_.get_service_mappings()) {
            if (!owner_.should_forward_to_someip(mapping)) {
                continue;
            }
            uint16_t mid = 0;
            if (!mapping.someip_method_ids.empty()) {
                mid = mapping.someip_method_ids.front();
            } else if (!mapping.someip_event_group_ids.empty()) {
                mid = mapping.someip_event_group_ids.front();
            }
            const std::string topic =
                mapping.external_identifier.empty()
                    ? DdsTranslator::build_dds_topic(mapping.someip_service_id, mapping.someip_instance_id,
                                                     mid)
                    : mapping.external_identifier;

            dds_qos_t* rqos = dds_create_qos();
            apply_dds_qos_profile(rqos, DdsTranslator::qos_for_service_mapping(mapping, false));
            dds_entity_t tp = ensure_topic(participant_, topic, topics_);
            dds_entity_t reader = dds_create_reader(participant_, tp, rqos, nullptr);
            dds_delete_qos(rqos);
            if (reader <= 0) {
                continue;
            }
            readers_[topic] = reader;
        }

        reader_stop_.store(false);
        reader_thread_ = std::thread([this] { reader_loop(); });
        return someip::Result::SUCCESS;
    }

    someip::Result stop() {
        stop_internal();
        return someip::Result::SUCCESS;
    }

    someip::Result write_someip(const someip::Message& msg, const std::string& topic,
                                const ServiceMapping& mapping, uint16_t instance_id) {
        if (participant_ <= 0) {
            return someip::Result::NOT_INITIALIZED;
        }

        dds_qos_t* wqos = dds_create_qos();
        const bool tcp_sem = owner_.config().someip.assume_tcp_semantics_for_rpc && msg.is_request();
        apply_dds_qos_profile(wqos,
                              DdsTranslator::qos_for_someip_message(msg, tcp_sem));

        dds_entity_t tp = ensure_topic(participant_, topic, topics_);
        dds_entity_t writer = 0;
        auto wit = writers_.find(topic);
        if (wit == writers_.end()) {
            writer = dds_create_writer(participant_, tp, wqos, nullptr);
            if (writer > 0) {
                writers_[topic] = writer;
            }
        } else {
            writer = wit->second;
        }
        dds_delete_qos(wqos);

        if (writer <= 0) {
            return someip::Result::INTERNAL_ERROR;
        }

        vehicle_SomeipBridgeSample sample{};
        sample.service_id = msg.get_service_id();
        sample.instance_id = instance_id;
        sample.method_or_event_id = msg.get_method_id();
        sample.message_type = static_cast<uint8_t>(msg.get_message_type());

        const auto& pl = msg.get_payload();
        const uint32_t len = static_cast<uint32_t>(pl.size());
        sample.payload._length = len;
        sample.payload._maximum = len;
        if (len > 0) {
            sample.payload._buffer = static_cast<uint8_t*>(dds_alloc(len));
            if (!sample.payload._buffer) {
                return someip::Result::INTERNAL_ERROR;
            }
            std::memcpy(sample.payload._buffer, pl.data(), len);
            sample.payload._release = true;
        } else {
            sample.payload._buffer = nullptr;
            sample.payload._release = false;
        }

        const dds_return_t wr = dds_write(writer, &sample);
        dds_sample_free(reinterpret_cast<void*>(&sample), &vehicle_SomeipBridgeSample_desc,
                        DDS_FREE_CONTENTS);

        if (wr != DDS_RETCODE_OK) {
            return someip::Result::INTERNAL_ERROR;
        }
        return someip::Result::SUCCESS;
    }

private:
    void stop_internal() {
        reader_stop_.store(true);
        if (reader_thread_.joinable()) {
            reader_thread_.join();
        }
        writers_.clear();
        readers_.clear();
        topics_.clear();
        if (participant_ > 0) {
            dds_delete_participant(participant_);
            participant_ = 0;
        }
    }

    void reader_loop() {
        while (!reader_stop_.load()) {
            std::vector<std::pair<std::string, dds_entity_t>> snapshot(readers_.begin(), readers_.end());

            for (const auto& kv : snapshot) {
                void* samples[2] = {nullptr, nullptr};
                dds_sample_info_t infos[2];
                const dds_return_t n = dds_take(kv.second, samples, infos, 2, 2);
                if (n <= 0) {
                    continue;
                }
                for (int32_t i = 0; i < n; ++i) {
                    if (!infos[static_cast<size_t>(i)].valid_data || !samples[static_cast<size_t>(i)]) {
                        continue;
                    }
                    auto* s =
                        static_cast<vehicle_SomeipBridgeSample*>(samples[static_cast<size_t>(i)]);
                    std::vector<uint8_t> pl(s->payload._length);
                    if (s->payload._length > 0 && s->payload._buffer) {
                        std::memcpy(pl.data(), s->payload._buffer, pl.size());
                    }
                    someip::Message reconstructed;
                    DdsTranslator::bridge_sample_to_someip(s->service_id, s->instance_id,
                                                           s->method_or_event_id, s->message_type, pl,
                                                           reconstructed);
                    owner_.deliver_inbound_from_dds(reconstructed, pl.size());
                }
                dds_return_loan(kv.second, samples, n);
            }
            dds_sleepfor(DDS_MSECS(20));
        }
    }

    DdsGateway& owner_;
    dds_entity_t participant_{0};
    std::unordered_map<std::string, dds_entity_t> topics_;
    std::unordered_map<std::string, dds_entity_t> writers_;
    std::unordered_map<std::string, dds_entity_t> readers_;
    std::thread reader_thread_;
    std::atomic<bool> reader_stop_{true};
};

class DdsGateway::UdpBridgeListener : public someip::transport::ITransportListener {
public:
    explicit UdpBridgeListener(DdsGateway& gateway) : gateway_(gateway) {
    }

    void on_message_received(someip::MessagePtr message, const someip::transport::Endpoint&) override {
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
    DdsGateway& gateway_;
};

DdsGateway::DdsGateway(DdsConfig config)
    : GatewayBase(config.participant_name.empty() ? std::string{"opensomeip-dds-gateway"}
                                                  : config.participant_name,
                  "dds"),
      config_(std::move(config)),
      impl_(std::make_unique<DdsGatewayImpl>(*this)) {
}

DdsGateway::~DdsGateway() {
    if (is_running()) {
        stop();
    }
}

void DdsGateway::set_someip_outbound_sink(SomeipOutboundSink sink) {
    std::lock_guard<std::mutex> lk(sink_mutex_);
    someip_outbound_sink_ = std::move(sink);
}

void DdsGateway::enable_someip_udp_bridge(const someip::transport::Endpoint& bind_ep,
                                          const someip::transport::UdpTransportConfig& cfg) {
    udp_transport_ = std::make_unique<someip::transport::UdpTransport>(bind_ep, cfg);
    udp_listener_ = std::make_unique<UdpBridgeListener>(*this);
    udp_transport_->set_listener(udp_listener_.get());
}

void DdsGateway::attach_rpc_client(const std::shared_ptr<someip::rpc::RpcClient>& rpc) {
    rpc_client_ = rpc;
}

void DdsGateway::attach_rpc_server(const std::shared_ptr<someip::rpc::RpcServer>& server) {
    rpc_server_ = server;
}

void DdsGateway::attach_sd_client(const std::shared_ptr<someip::sd::SdClient>& sd) {
    sd_client_ = sd;
}

void DdsGateway::attach_sd_server(const std::shared_ptr<someip::sd::SdServer>& sd_server) {
    sd_server_ = sd_server;
}

bool DdsGateway::register_event_publisher(uint16_t service_id, uint16_t instance_id,
                                          std::unique_ptr<someip::events::EventPublisher> publisher) {
    const uint64_t key = (static_cast<uint64_t>(service_id) << 16) | instance_id;
    event_publishers_[key] = std::move(publisher);
    return true;
}

bool DdsGateway::subscribe_someip_eventgroup(uint16_t service_id, uint16_t instance_id,
                                             uint16_t eventgroup_id) {
    if (!event_subscriber_) {
        event_subscriber_ = std::make_unique<someip::events::EventSubscriber>(config_.rpc_client_id);
        event_subscriber_->initialize();
    }

    return event_subscriber_->subscribe_eventgroup(
        service_id, instance_id, eventgroup_id,
        [this](const someip::events::EventNotification& n) {
            someip::Message notification(
                someip::MessageId{n.service_id, n.event_id},
                someip::RequestId{n.client_id, n.session_id}, someip::MessageType::NOTIFICATION,
                someip::ReturnCode::E_OK);
            notification.set_payload(n.event_data);
            on_someip_message(notification);
        });
}

void DdsGateway::deliver_inbound_from_dds(const someip::Message& reconstructed,
                                           std::size_t byte_count) {
    record_external_to_someip(byte_count);
    std::lock_guard<std::mutex> lk(sink_mutex_);
    if (someip_outbound_sink_) {
        someip_outbound_sink_(reconstructed);
    }
}

someip::Result DdsGateway::start() {
    if (udp_transport_) {
        const auto ur = udp_transport_->start();
        if (ur != someip::Result::SUCCESS) {
            return ur;
        }
    }

    if (sd_client_) {
        sd_client_->initialize();
    }
    if (sd_server_) {
        sd_server_->initialize();
    }

    const auto dr = impl_->start();
    if (dr != someip::Result::SUCCESS) {
        return dr;
    }

    set_running(true);
    return someip::Result::SUCCESS;
}

someip::Result DdsGateway::stop() {
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

std::string DdsGateway::resolve_dds_topic(const someip::Message& msg,
                                           const ServiceMapping& mapping) const {
    if (!mapping.external_identifier.empty()) {
        return mapping.external_identifier;
    }
    return DdsTranslator::build_dds_topic(mapping.someip_service_id, mapping.someip_instance_id,
                                          msg.get_method_id(), msg);
}

someip::Result DdsGateway::on_someip_message(const someip::Message& msg) {
    if (!is_running()) {
        return someip::Result::NOT_INITIALIZED;
    }

    const ServiceMapping* mapping =
        find_mapping_for_service(msg.get_service_id(), config_.someip.default_someip_instance_id);
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

    const std::string topic = resolve_dds_topic(msg, *mapping);
    const auto mode = mapping->mode;
    someip::Message to_send = msg;
    if (mode == TranslationMode::TYPED) {
        to_send.set_payload(DdsTranslator::encode_outbound(msg, TranslationMode::TYPED));
    }

    const auto wr =
        impl_->write_someip(to_send, topic, *mapping, mapping->someip_instance_id);
    if (wr == someip::Result::SUCCESS) {
        record_someip_to_external(to_send.get_payload().size());
    }
    return wr;
}

someip::Result DdsGateway::inject_dds_sample(const std::string& /*dds_topic*/,
                                             const std::vector<uint8_t>& payload, uint16_t service_id,
                                             uint16_t instance_id, uint16_t method_id,
                                             uint8_t someip_message_type_byte) {
    someip::Message m;
    if (!DdsTranslator::bridge_sample_to_someip(service_id, instance_id, method_id,
                                                someip_message_type_byte, payload, m)) {
        record_translation_error();
        return someip::Result::INTERNAL_ERROR;
    }
    deliver_inbound_from_dds(m, payload.size());
    return someip::Result::SUCCESS;
}

}  // namespace dds
}  // namespace gateway
}  // namespace opensomeip
