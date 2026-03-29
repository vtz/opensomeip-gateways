# Copyright (c) 2025 Vinicius Tadeu Zein
# SPDX-License-Identifier: Apache-2.0

find_package(Protobuf QUIET)
find_package(gRPC QUIET CONFIG)

if(NOT gRPC_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GRPCPP grpc++)
        if(GRPCPP_FOUND AND Protobuf_FOUND)
            set(gRPC_FOUND TRUE)
            set(_grpc_via_pkgconfig TRUE)
        endif()
    endif()
endif()

if(NOT gRPC_FOUND OR NOT Protobuf_FOUND)
    message(FATAL_ERROR
        "gRPC and Protobuf are required to build gateway-grpc.\n"
        "  Ubuntu: sudo apt install libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc\n"
        "  Fedora: sudo dnf install grpc-devel protobuf-devel grpc-plugins\n"
        "  Or disable: -DBUILD_GATEWAY_GRPC=OFF"
    )
endif()

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

target_compile_definitions(opensomeip-gateway-grpc PUBLIC HAVE_GRPC=1)

if(_grpc_via_pkgconfig)
    target_include_directories(opensomeip-gateway-grpc PRIVATE ${GRPCPP_INCLUDE_DIRS})
    target_link_libraries(opensomeip-gateway-grpc PRIVATE ${GRPCPP_LIBRARIES} protobuf::libprotobuf)
else()
    target_link_libraries(opensomeip-gateway-grpc PRIVATE gRPC::grpc++ protobuf::libprotobuf)
endif()

if(BUILD_TESTS)
    add_executable(test_grpc_gateway tests/test_grpc_gateway.cpp)
    target_link_libraries(test_grpc_gateway PRIVATE opensomeip-gateway-grpc GTest::gtest_main)
    add_test(NAME GrpcGatewayTests COMMAND test_grpc_gateway)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
