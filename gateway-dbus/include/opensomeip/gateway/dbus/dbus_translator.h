/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef OPENSOMEIP_GATEWAY_DBUS_DBUS_TRANSLATOR_H
#define OPENSOMEIP_GATEWAY_DBUS_DBUS_TRANSLATOR_H

#include <cstdint>
#include <string>
#include <vector>

namespace opensomeip {
namespace gateway {
namespace dbus {

/**
 * @brief Builds D-Bus names, object paths, type signatures, and introspection XML
 *        for SOME/IP service and instance identifiers.
 */
class DbusTranslator {
public:
    explicit DbusTranslator(std::string bus_name_prefix, std::string object_path_prefix);

    [[nodiscard]] std::string build_bus_name(uint16_t service_id, uint16_t instance_id) const;
    [[nodiscard]] std::string build_object_path(uint16_t service_id, uint16_t instance_id) const;
    [[nodiscard]] static std::string build_interface_name(uint16_t service_id);

    /**
     * @brief Map a SOME/IP scalar type name (e.g. "uint16", "float") to a D-Bus type string.
     *        Unknown names map to "ay" (byte array / opaque payload).
     */
    [[nodiscard]] static std::string someip_type_to_dbus_signature(const std::string& someip_type);

    /**
     * @brief Compose a D-Bus signature from an ordered list of SOME/IP type names.
     */
    [[nodiscard]] static std::string build_signature_from_someip_types(
        const std::vector<std::string>& someip_types);

    /**
     * @brief Generate introspection XML for the gateway interface on a service.
     * @param service_id SOME/IP service id (used in interface name)
     * @param method_arg_types Ordered SOME/IP types for InvokeRpc arguments (beyond fixed ids)
     * @param include_signal When true, include SomeipNotification signal
     */
    [[nodiscard]] std::string generate_introspection_xml(
        uint16_t service_id,
        const std::vector<std::string>& method_arg_types = {},
        bool include_signal = true) const;

    [[nodiscard]] const std::string& bus_name_prefix() const { return bus_name_prefix_; }
    [[nodiscard]] const std::string& object_path_prefix() const { return object_path_prefix_; }

private:
    static std::string normalize_path_prefix(std::string p);
    static std::string normalize_bus_prefix(std::string p);

    std::string bus_name_prefix_;
    std::string object_path_prefix_;
};

}  // namespace dbus
}  // namespace gateway
}  // namespace opensomeip

#endif  // OPENSOMEIP_GATEWAY_DBUS_DBUS_TRANSLATOR_H
