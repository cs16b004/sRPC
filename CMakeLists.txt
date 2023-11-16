cmake_minimum_required(VERSION 3.16.3)
project(sRPC)

include(CMakeDependentOption)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DLOG_LEVEL_AS_DEBUG -DRPC_STATISTICS")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DLOG_LEVEL_AS_DEBUG -DRPC_STATISTICS")

find_package(PkgConfig REQUIRED)

if (PKG_CONFIG_FOUND)
       pkg_check_modules(DPDK "libdpdk")
endif()
pkg_check_modules(DPDK "libdpdk")
if (DPDK_FOUND)
  add_definitions(${DPDK_STATIC_CFLAGS})
  set(DPDK_LIBS ${DPDK_STATIC_LDFLAGS})
  include_directories(${DPDK_INCLUDE_DIR})
  link_libraries(${DPDK_LIBS})
  add_definitions(-DDPDK)
endif(DPDK_FOUND)
 add_definitions(-DLOG_LEVEL_AS_DEBUG -DRPC_STATISTICS)

include_directories( include )

file(GLOB SRPC_SRC "src/base/*.cpp"
"src/misc/*.cpp"
"src/rpc/*.cpp"
"src/rpc/dpdk_transport/*.cpp")
set(SOURCES 
    ${SRPC_SRC}
 )


add_library(rpc 
    ${SOURCES})