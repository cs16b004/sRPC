cmake_minimum_required(VERSION 3.16.3)
project(rpc_test)



include(CMakeDependentOption)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_COLOR_MAKEFILE   ON)
#set(CMAKE_BINARY_DIR ${CMAKE_CURRENT_LIST_DIR}../../bin)


set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DLOG_LEVEL_AS_DEBUG -DRPC_STATISTICS")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DLOG_LEVEL_AS_DEBUG -DRPC_STATISTICS")


add_definitions(-DLOG_LEVEL_AS_DEBUG -DRPC_STATISTICS)

if (PKG_CONFIG_FOUND)
       pkg_check_modules(DPDK "libdpdk")
endif()
pkg_check_modules(DPDK "libdpdk")
if (DPDK_FOUND)
  include_directories(${DPDK_INCLUDE_DIR})
  add_definitions(-DDPDK)
endif(DPDK_FOUND)


file(GLOB SRC "./src/benchmarks.cpp")

 set(SOURCES 
     ${SRC}
 )
add_custom_command(
  OUTPUT src/counter.h
  COMMAND ${RPCC} ${CMAKE_CURRENT_SOURCE_DIR}/src/counter.rpc
)
add_executable(server "./src/s.cpp" ${SRC} src/counter.h)

add_executable(client "./src/c.cpp" ${SRC} src/counter.h)
target_include_directories(server PRIVATE src 
                                          ../../src/base 
                                          ../../src/coroutine 
                                          ../../src/misc 
                                          ../../src/utils 
                                          ../../src/mq 
                                          ../../src/rpc 
                                          ../../src/rpc/dpdk_transport)
target_include_directories(client PRIVATE src 
                                          ../../src/base 
                                          ../../src/coroutine 
                                          ../../src/misc 
                                          ../../src/utils
                                          ../../src/mq 
                                          ../../src/rpc 
                                          ../../src/rpc/dpdk_transport)

target_link_libraries(server PRIVATE rpc yaml-cpp pthread )
target_link_libraries(client PRIVATE rpc yaml-cpp pthread )


#target_link_directories(server PRIVATE ../../build)
#target_link_directories(client PRIVATE ../../build)