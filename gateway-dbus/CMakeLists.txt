# Copyright (c) 2025 Vinicius Tadeu Zein
# SPDX-License-Identifier: Apache-2.0

find_package(PkgConfig QUIET)
set(_dbus_have_systemd FALSE)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBSYSTEMD libsystemd)
    if(LIBSYSTEMD_FOUND)
        set(_dbus_have_systemd TRUE)
    endif()
endif()

add_library(opensomeip-gateway-dbus
    src/dbus_gateway.cpp
    src/dbus_translator.cpp
)

target_include_directories(opensomeip-gateway-dbus
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(opensomeip-gateway-dbus
    PUBLIC
        opensomeip-gateway-common
)

if(_dbus_have_systemd)
    target_compile_definitions(opensomeip-gateway-dbus PRIVATE HAVE_SYSTEMD=1)
    target_include_directories(opensomeip-gateway-dbus PRIVATE ${LIBSYSTEMD_INCLUDE_DIRS})
    target_link_libraries(opensomeip-gateway-dbus PRIVATE ${LIBSYSTEMD_LIBRARIES})
else()
    message(WARNING "libsystemd not found: D-Bus gateway builds without sd-bus (signals/methods are no-ops at runtime unless you rebuild with systemd).")
endif()

if(BUILD_TESTS)
    add_executable(test_dbus_gateway tests/test_dbus_gateway.cpp)
    target_link_libraries(test_dbus_gateway PRIVATE opensomeip-gateway-dbus GTest::gtest_main)
    add_test(NAME DbusGatewayTests COMMAND test_dbus_gateway)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
