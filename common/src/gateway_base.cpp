/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include "opensomeip/gateway/gateway_base.h"

#include <algorithm>

namespace opensomeip {
namespace gateway {

GatewayBase::GatewayBase(const std::string& name, const std::string& protocol)
    : name_(name), protocol_(protocol) {
}

bool GatewayBase::is_running() const {
    return running_.load(std::memory_order_acquire);
}

std::string GatewayBase::get_name() const {
    return name_;
}

std::string GatewayBase::get_protocol() const {
    return protocol_;
}

GatewayStats GatewayBase::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void GatewayBase::add_service_mapping(const ServiceMapping& mapping) {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    service_mappings_.push_back(mapping);
}

const std::vector<ServiceMapping>& GatewayBase::get_service_mappings() const {
    return service_mappings_;
}

void GatewayBase::set_external_message_callback(ExternalMessageCallback callback) {
    external_message_callback_ = std::move(callback);
}

void GatewayBase::set_running(bool running) {
    running_.store(running, std::memory_order_release);
    if (running) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.started_at = std::chrono::steady_clock::now();
    }
}

void GatewayBase::record_someip_to_external(size_t bytes) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.messages_someip_to_external++;
    stats_.bytes_someip_to_external += bytes;
}

void GatewayBase::record_external_to_someip(size_t bytes) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.messages_external_to_someip++;
    stats_.bytes_external_to_someip += bytes;
}

void GatewayBase::record_translation_error() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.translation_errors++;
}

const ServiceMapping* GatewayBase::find_mapping_for_service(
    uint16_t service_id, uint16_t instance_id) const {

    std::lock_guard<std::mutex> lock(mappings_mutex_);
    auto it = std::find_if(service_mappings_.begin(), service_mappings_.end(),
                           [service_id, instance_id](const ServiceMapping& m) {
                               return m.someip_service_id == service_id &&
                                      m.someip_instance_id == instance_id;
                           });
    return (it != service_mappings_.end()) ? &(*it) : nullptr;
}

bool GatewayBase::should_forward_to_external(const ServiceMapping& mapping) const {
    return mapping.direction == GatewayDirection::SOMEIP_TO_EXTERNAL ||
           mapping.direction == GatewayDirection::BIDIRECTIONAL;
}

bool GatewayBase::should_forward_to_someip(const ServiceMapping& mapping) const {
    return mapping.direction == GatewayDirection::EXTERNAL_TO_SOMEIP ||
           mapping.direction == GatewayDirection::BIDIRECTIONAL;
}

}  // namespace gateway
}  // namespace opensomeip
