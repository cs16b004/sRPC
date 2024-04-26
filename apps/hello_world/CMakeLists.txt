cmake_minimum_required(VERSION 3.16.3)
project(hello_world)



include(CMakeDependentOption)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_COLOR_MAKEFILE   ON)
#set(CMAKE_BINARY_DIR ${CMAKE_CURRENT_LIST_DIR}../../bin)


set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

if (PKG_CONFIG_FOUND)
       pkg_check_modules(DPDK "libdpdk")
endif()
pkg_check_modules(DPDK "libdpdk")
if (DPDK_FOUND)
  include_directories(${DPDK_INCLUDE_DIR})
  add_definitions(-DDPDK)
endif(DPDK_FOUND)

file(GLOB SRC  "./src/AppConfig.cpp")

 set(SOURCES 
     ${SRC}
 )
add_custom_command(
  OUTPUT src/hello.h
  COMMAND ${RPCC} --cpp --dpdk ${CMAKE_CURRENT_SOURCE_DIR}/src/hello.rpc
)
add_executable(h_server "./src/s.cpp" ${SRC} src/hello.h)

add_executable(h_client "./src/c.cpp" ${SRC} src/hello.h)
target_include_directories(h_server PRIVATE src 
                                          ../../src/base 
                                          ../../src/coroutine 
                                          ../../src/misc 
                                          ../../src/utils 
                                          ../../src/mq 
                                          ../../src/rpc 
                                          ../../src/rpc/dpdk_transport)
target_include_directories(h_client PRIVATE src 
                                          ../../src/base 
                                          ../../src/coroutine 
                                          ../../src/misc 
                                          ../../src/utils
                                          ../../src/mq 
                                          ../../src/rpc 
                                          ../../src/rpc/dpdk_transport)

target_link_libraries(h_server PRIVATE rpc yaml-cpp pthread )
target_link_libraries(h_client PRIVATE rpc yaml-cpp pthread )


#target_link_directories(server PRIVATE ../../build)
#target_link_directories(client PRIVATE ../../build)