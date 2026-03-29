/********************************************************************************
 * Copyright (c) 2025 Vinicius Tadeu Zein
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

add_executable(zenoh_edge_bridge zenoh_edge_bridge.cpp)
target_link_libraries(zenoh_edge_bridge PRIVATE opensomeip-gateway-zenoh)
