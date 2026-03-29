/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_GATEWAY_BASE_H
#define OPENSOMEIP_GATEWAY_GATEWAY_BASE_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "someip/message.h"
#include "common/result.h"

namespace opensomeip {
namespace gateway {

struct GatewayStats {
    uint64_t messages_someip_to_external{0};
    uint64_t messages_external_to_someip{0};
    uint64_t translation_errors{0};
    uint64_t bytes_someip_to_external{0};
    uint64_t bytes_external_to_someip{0};
    std::chrono::steady_clock::time_point started_at{};

    std::chrono::milliseconds uptime() const {
        if (started_at == std::chrono::steady_clock::time_point{}) {
            return std::chrono::milliseconds{0};
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at);
    }
};

enum class GatewayDirection : uint8_t {
    SOMEIP_TO_EXTERNAL,
    EXTERNAL_TO_SOMEIP,
    BIDIRECTIONAL
};

enum class TranslationMode : uint8_t {
    OPAQUE,
    TYPED
};

struct ServiceMapping {
    uint16_t someip_service_id{0};
    uint16_t someip_instance_id{0};
    std::vector<uint16_t> someip_method_ids;
    std::vector<uint16_t> someip_event_group_ids;

    std::string external_identifier;

    GatewayDirection direction{GatewayDirection::BIDIRECTIONAL};
    TranslationMode mode{TranslationMode::OPAQUE};
};

using ExternalMessageCallback =
    std::function<void(uint16_t service_id, uint16_t method_id,
                       const std::vector<uint8_t>& payload)>;

class IGateway {
public:
    virtual ~IGateway() = default;

    virtual someip::Result start() = 0;
    virtual someip::Result stop() = 0;
    virtual bool is_running() const = 0;

    virtual someip::Result on_someip_message(const someip::Message& msg) = 0;

    virtual std::string get_name() const = 0;
    virtual std::string get_protocol() const = 0;
    virtual GatewayStats get_stats() const = 0;
};

class GatewayBase : public IGateway {
public:
    explicit GatewayBase(const std::string& name, const std::string& protocol);
    ~GatewayBase() override = default;

    GatewayBase(const GatewayBase&) = delete;
    GatewayBase& operator=(const GatewayBase&) = delete;
    GatewayBase(GatewayBase&&) = delete;
    GatewayBase& operator=(GatewayBase&&) = delete;

    bool is_running() const override;
    std::string get_name() const override;
    std::string get_protocol() const override;
    GatewayStats get_stats() const override;

    void add_service_mapping(const ServiceMapping& mapping);
    const std::vector<ServiceMapping>& get_service_mappings() const;

    void set_external_message_callback(ExternalMessageCallback callback);

protected:
    void set_running(bool running);

    void record_someip_to_external(size_t bytes);
    void record_external_to_someip(size_t bytes);
    void record_translation_error();

    const ServiceMapping* find_mapping_for_service(uint16_t service_id,
                                                   uint16_t instance_id) const;

    bool should_forward_to_external(const ServiceMapping& mapping) const;
    bool should_forward_to_someip(const ServiceMapping& mapping) const;

    ExternalMessageCallback external_message_callback_;

private:
    std::string name_;
    std::string protocol_;
    std::atomic<bool> running_{false};
    mutable std::mutex stats_mutex_;
    GatewayStats stats_;
    std::vector<ServiceMapping> service_mappings_;
    mutable std::mutex mappings_mutex_;
};

}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_GATEWAY_BASE_H
