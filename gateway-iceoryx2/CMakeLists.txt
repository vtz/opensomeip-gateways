/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

find_package(iceoryx2-cxx QUIET)

add_library(opensomeip-gateway-iceoryx2
    src/iceoryx2_gateway.cpp
    src/iceoryx2_translator.cpp
)

target_include_directories(opensomeip-gateway-iceoryx2
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(opensomeip-gateway-iceoryx2
    PUBLIC opensomeip-gateway-common
)

if(iceoryx2-cxx_FOUND)
    target_link_libraries(opensomeip-gateway-iceoryx2 PRIVATE iceoryx2-cxx::iceoryx2-cxx)
    target_compile_definitions(opensomeip-gateway-iceoryx2 PRIVATE HAVE_ICEORYX2=1)
endif()

if(BUILD_TESTS)
    add_executable(test_iceoryx2_gateway
        tests/test_iceoryx2_gateway.cpp
    )
    target_link_libraries(test_iceoryx2_gateway
        PRIVATE opensomeip-gateway-iceoryx2 GTest::gtest_main
    )
    add_test(NAME Iceoryx2GatewayTests COMMAND test_iceoryx2_gateway)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
