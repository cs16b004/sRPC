cmake_minimum_required(VERSION 3.16.3)
project(rpc_test)



include(CMakeDependentOption)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DLOG_LEVEL_AS_DEBUG -DRPC_STATISTICS")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DLOG_LEVEL_AS_DEBUG -DRPC_STATISTICS")




find_package(PkgConfig REQUIRED)

 add_definitions(-DLOG_LEVEL_AS_DEBUG -DRPC_STATISTICS)

# Adding srpc as dependency
 add_subdirectory(../../ ../../build/)



file(GLOB SRC "./src/benchmarks.cpp")

 set(SOURCES 
     ${SRC}
 )
 
add_executable(server "./src/s.cpp" ${SRC})

add_executable(client "./src/c.cpp" ${SRC})

if (PKG_CONFIG_FOUND)
       pkg_check_modules(DPDK "libdpdk")
endif()
pkg_check_modules(DPDK "libdpdk")
if (DPDK_FOUND)
  add_definitions(${DPDK_STATIC_CFLAGS})
  set(DPDK_LIBS ${DPDK_STATIC_LDFLAGS})
  target_include_directories(server PRIVATE ${DPDK_INCLUDE_DIR})
  target_include_directories(client PRIVATE ${DPDK_INCLUDE_DIR})
  #link_libraries(${DPDK_LIBS})
  add_definitions(-DDPDK)
endif(DPDK_FOUND)


target_include_directories(server PRIVATE include ../../include)
target_include_directories(client PRIVATE include ../../include)

target_link_libraries(server PRIVATE rpc yaml-cpp pthread )
target_link_libraries(client PRIVATE rpc yaml-cpp pthread )

# target_link_directories(server PRIVATE ../../build)
# target_link_directories(client PRIVATE ../../build)


