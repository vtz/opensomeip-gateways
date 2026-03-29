/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_ZENOH_GATEWAY_H
#define OPENSOMEIP_GATEWAY_ZENOH_GATEWAY_H

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "e2e/e2e_config.h"
#include "opensomeip/gateway/gateway_base.h"
#include "opensomeip/gateway/zenoh/zenoh_translator.h"

namespace opensomeip {
namespace gateway {
namespace zenoh {

/** @brief Zenoh session topology (see Zenoh configuration reference). */
enum class ZenohSessionMode : uint8_t {
    PEER = 0,
    CLIENT = 1,
    ROUTER = 2
};

/** @brief Optional shared-memory transport tuning (Zenoh `transport/shared_memory/*`). */
struct ZenohShmConfig {
    bool enabled{false};
    /** @brief Minimum payload size (bytes) before SHM is considered (implementation-defined threshold). */
    size_t threshold_bytes{32768};
};

/**
 * @brief Gateway-side configuration for Zenoh and the co-located SOME/IP leg.
 *
 * `to_json5()` produces a JSON/JSON5 document suitable for `zenoh::Config::from_str` on zenoh-c backends.
 */
struct ZenohConfig {
    ZenohSessionMode mode{ZenohSessionMode::PEER};
    std::vector<std::string> connect_endpoints;
    std::vector<std::string> listen_endpoints;
    /** @brief Hierarchical prefix for keys (e.g. vehicle/ecu1/someip). */
    std::string key_prefix{"someip"};
    ZenohShmConfig shm;
    /** @brief When non-empty, load this file first; explicit fields below are merged via insert_json5. */
    std::string zenoh_config_file;

    ZenohPayloadEncoding payload_encoding{ZenohPayloadEncoding::RAW};

    /** @brief Local UDP bind for the SOME/IP transport listener. */
    std::string someip_bind_address{"0.0.0.0"};
    uint16_t someip_bind_port{30500};
    /** @brief Default peer for responses when no sender endpoint is cached. */
    std::string someip_remote_address{"127.0.0.1"};
    uint16_t someip_remote_port{30501};

    uint16_t rpc_client_id{0x0042};
    bool enable_udp_transport{true};
    bool enable_service_discovery{true};
    /** @brief Bridge Zenoh liveliness to SOME/IP SD client notifications. */
    bool enable_liveliness_sd_bridge{true};
    std::string liveliness_subscription_key;  // empty => `<key_prefix>/liveliness/**`

    bool enable_e2e_validation{false};
    someip::e2e::E2EConfig e2e_config{};

    /** @brief Serialize mode, endpoints, and SHM knobs for Zenoh-c `Config::from_str`. */
    std::string to_json5() const {
        std::ostringstream j;
        j << "{";
        switch (mode) {
            case ZenohSessionMode::PEER:
                j << "\"mode\":\"peer\"";
                break;
            case ZenohSessionMode::CLIENT:
                j << "\"mode\":\"client\"";
                break;
            case ZenohSessionMode::ROUTER:
                j << "\"mode\":\"router\"";
                break;
        }
        j << ",\"connect\":{\"endpoints\":[";
        for (size_t i = 0; i < connect_endpoints.size(); ++i) {
            if (i > 0) {
                j << ",";
            }
            j << '"';
            for (char c : connect_endpoints[i]) {
                if (c == '"' || c == '\\') {
                    j << '\\';
                }
                j << c;
            }
            j << '"';
        }
        j << "]},\"listen\":{\"endpoints\":[";
        for (size_t i = 0; i < listen_endpoints.size(); ++i) {
            if (i > 0) {
                j << ",";
            }
            j << '"';
            for (char c : listen_endpoints[i]) {
                if (c == '"' || c == '\\') {
                    j << '\\';
                }
                j << c;
            }
            j << '"';
        }
        j << "]},\"transport\":{\"shared_memory\":{";
        j << "\"enabled\":" << (shm.enabled ? "true" : "false");
        j << ",\"threshold\":" << shm.threshold_bytes << "}}}";
        j << "}";
        return j.str();
    }
};

/**
 * @brief SOME/IP <-> Zenoh bridge: UDP SOME/IP framing, Events, RPC, SD, E2E, serialization.
 *
 * Zenoh resources (publishers, subscribers, queryables, liveliness) are managed in the
 * implementation translation unit so consumers can include this header without zenoh.hxx.
 */
class ZenohGateway : public GatewayBase {
public:
    ZenohGateway(std::string name, ZenohConfig config);
    ~ZenohGateway() override;

    ZenohGateway(const ZenohGateway&) = delete;
    ZenohGateway& operator=(const ZenohGateway&) = delete;
    ZenohGateway(ZenohGateway&&) = delete;
    ZenohGateway& operator=(ZenohGateway&&) = delete;

    someip::Result start() override;
    someip::Result stop() override;

    /** @brief Entry point when SOME/IP messages are injected by a host stack or UDP listener. */
    someip::Result on_someip_message(const someip::Message& msg) override;

    const ZenohConfig& zenoh_config() const { return config_; }
    ZenohConfig& zenoh_config() { return config_; }

    ZenohTranslator& translator() { return translator_; }
    const ZenohTranslator& translator() const { return translator_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ZenohConfig config_;
    ZenohTranslator translator_;
};

}  // namespace zenoh
}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_ZENOH_GATEWAY_H
