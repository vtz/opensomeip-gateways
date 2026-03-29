/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

find_package(Protobuf QUIET)
find_package(gRPC QUIET)

add_library(opensomeip-gateway-grpc
    src/grpc_gateway.cpp
    src/grpc_translator.cpp
)

target_include_directories(opensomeip-gateway-grpc
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(opensomeip-gateway-grpc
    PUBLIC opensomeip-gateway-common
)

if(gRPC_FOUND AND Protobuf_FOUND)
    target_link_libraries(opensomeip-gateway-grpc PRIVATE gRPC::grpc++ protobuf::libprotobuf)
    target_compile_definitions(opensomeip-gateway-grpc PRIVATE HAVE_GRPC=1)
endif()

if(BUILD_TESTS)
    add_executable(test_grpc_gateway tests/test_grpc_gateway.cpp)
    target_link_libraries(test_grpc_gateway PRIVATE opensomeip-gateway-grpc GTest::gtest_main)
    add_test(NAME GrpcGatewayTests COMMAND test_grpc_gateway)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
