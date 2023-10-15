cmake_minimum_required(VERSION 3.10)

project(sRPC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

# Add include directories
include_directories(
    ..
    .
    ../rpc
    ../base
    ../rpc/dpdk_transport
)

# Find and use pkg-config to get DPDK flags and libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)
SET(DPDK_LIBS "-Wl,--whole-archive -ldpdk -Wl,--no-whole-archive")
#include_directories(${DPDK_INCLUDE_DIRS})
#link_directories(${DPDK_LIBRARY_DIRS})

# Add source files
add_library(rpc STATIC
    ../rpc/client.cpp
    ../misc/marshal.cpp
    ../rpc/polling.cpp
    ../rpc/server.cpp
    ../rpc/utils.cpp
    ../rpc/UDPServer.cpp
    ../rpc/reporter.cpp
    ../rpc/dpdk_transport/transport.cpp
    ../rpc/dpdk_transport/config.cpp
    ../rpc/dpdk_transport/transport_connection.cpp
)

add_library(base STATIC
    ../base/basetypes.cpp
    ../base/logging.cpp
    ../base/misc.cpp
    ../base/strop.cpp
    ../base/threading.cpp
    ../base/unittest.cpp
)

# Add executable targets
add_executable(server s.cpp)
target_link_libraries(server PRIVATE rpc base ${DPDK_LIBS} pthread yaml-cpp)

add_executable(client c.cpp)
target_link_libraries(client PRIVATE rpc base ${DPDK_LIBS} pthread yaml-cpp)

# Additional targets
add_executable(benchmarks benchmarks.cpp)
target_link_libraries(benchmarks PRIVATE rpc base ${DPDK_LIBS} pthread yaml-cpp)

# Add a clean target
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/cmake/clean-all.cmake
)

# Custom clean script
configure_file(cmake/clean-all.cmake.in clean-all.cmake @ONLY)

add_custom_target(dpdk_debug
    COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Debug -DLOG_LEVEL_AS_DEBUG=ON -DRPC_STATISTICS=ON
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
)

add_custom_target(dpdk_stat
    COMMAND ${CMAKE_COMMAND} -DLOG_LEVEL_AS_DEBUG=OFF -DRPC_STATISTICS=ON
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
)