cmake_minimum_required(VERSION 3.16.3)
project(sRPC)

include(CMakeDependentOption)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
#set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_COLOR_MAKEFILE   ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)
set(RPCC ${CMAKE_CURRENT_LIST_DIR}/bin/rpcgen)

if(PERF)
  message(STATUS "Compilation optimized for performance.")
  message(STATUS "Debugging is disabled.")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O02")
else(PERF)
  message(STATUS "Compilation not optimized for performance.")
  message(STATUS "Debugging is enabled. Perf will be low.")
  set(CMAKE_BUILD_TYPE Debug)
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -Wall")

endif(PERF)



find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED COMPONENTS context)


include_directories( GLOB src src/base src/misc src/rpc src/rpc/dpdk_transport src/coroutine src/utils )

file(GLOB SRPC_SRC "src/base/*.cpp"
"src/misc/*.cpp"
"src/rpc/*.cpp"
"src/rpc/dpdk_transport/*.cpp"
"src/coroutine/*.cc"

)
set(SOURCES 
    ${SRPC_SRC}
 )


add_library(rpc
    ${SOURCES} )


target_link_libraries(rpc PRIVATE ${Boost_LIBRARIES})
target_include_directories(rpc PUBLIC ${Boost_INCLUDE_DIRS})

if (PKG_CONFIG_FOUND)
       pkg_check_modules(DPDK "libdpdk")
endif()
pkg_check_modules(DPDK "libdpdk")
if (DPDK_FOUND)
  
  add_definitions(${DPDK_STATIC_CFLAGS})
  set(DPDK_LIBS ${DPDK_STATIC_LDFLAGS})
  include_directories(${DPDK_INCLUDE_DIR})
  target_link_libraries(rpc PRIVATE ${DPDK_LIBS})
  add_definitions(-DDPDK)
endif(DPDK_FOUND)
if(PERF)
  add_definitions(-DRPC_STATISTICS -DLATENCY )
else(PERF)
  add_definitions(-DRPC_STATISTICS -DLOG_LEVEL_AS_DEBUG -DLATENCY )
endif(PERF)

add_subdirectory(apps/rpc_test)
add_subdirectory(apps/hello_world)