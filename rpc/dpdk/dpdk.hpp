#pragma once
//#define ALLOW_EXPERIMENTAL_API_RING 128

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>
#include <unordered_map>

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <mutex>

#include <rte_common.h>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_byteorder.h>
#include <rte_atomic.h>
#include <rte_launch.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_debug.h>
#include <rte_ring.h>
#include <rte_log.h>
#include <rte_mempool.h>
#include <rte_memcpy.h>
#include <rte_mbuf.h>
#include <rte_interrupts.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_malloc.h>
#include <rte_string_fns.h>
#include <rte_cycles.h>

#include <rte_ring.h>
#include <rte_cycles.h>
#include <rte_launch.h>
#include <rte_pause.h>
#include <rte_random.h>
#include <rte_malloc.h>
#include <rte_spinlock.h>

#include "misc/marshal.hpp"
#

#include <atomic>

namespace rrr {

  static volatile bool DPDK_CLIENT_INIT_FINISHED = false;
  static volatile bool DPDK_CLIENT_INIT = false;
  static int DPDK_PORT_NUM = 0;

  // configures
  static unsigned send_clients;
  static unsigned recv_clients;
  static unsigned num_clients;
  static const char* DPDK_DEST_MAC;
  static const char* DPDK_DEST_IP;
  static uint16_t DPDK_DEST_PORT;
  static const char* DPDK_SRC_MAC;
  static const char* DPDK_SRC_IP;
  static uint16_t DPDK_SRC_PORT;
  static const char* DPDK_START_OPTIONS;

  class DpdkClient{
  typedef struct dpdk_payload_t {
    uint8_t client_addr[12];
    uint8_t checksum[2];
    uint8_t type;
    uint8_t par_id;
    uint8_t content[17];
    uint8_t latency[4];
    uint8_t app_checksum[4];
    int64_t xid;
    uint8_t sw_resp;
        // padding for testing different packet size.
    uint8_t padding[0];
    } dpdk_payload;
#pragma pack(pop) 
      void init();
      void send(Marshal& req, int64_t xid, uint8_t par_id);
      int recv();
      virtual void handle_read(void*, uint64_t, int) = 0;
      void set_loc_id(uint16_t loc_id) {loc_id_ = loc_id;}
      uint16_t loc_id() {return loc_id_;}
      void set_config(int n_send, int n_recv, 
          const char* src_mac, const char* src_ip, int src_port,
          const char* dest_mac, const char* dest_ip, int dest_port, 
          const char* start_options, bool simple_mode, bool no_stat);
      bool dpdk_simple_mode_;
      bool dpdk_no_stat_;

    private:
      struct rte_eth_dev_info ethdev_info;
      struct rte_eth_rxconf eth_rx_conf;
      struct rte_eth_txconf eth_tx_conf;
      uint16_t rte_queue_id;
      uint16_t loc_id_ ;
      SpinLock init_lock_;

      void generate_header(uint8_t*,int);
      void generate_msg(uint8_t*, uint8_t);
      void generate_msg_xid(uint8_t*, int64_t);

      SpinLock send_lock_;
      int64_t count_ = 0;
  };

  // Each client has a dpdk object.  
  static volatile void* DPDK_Objs[1000];
}
  
#define MAX_CLIENTS 128 

struct rx_stats{
  uint64_t rx[RTE_MAX_ETHPORTS];
} __rte_cache_aligned;

struct tx_stats{
  uint64_t tx[RTE_MAX_ETHPORTS];
  uint64_t tx_drop[RTE_MAX_ETHPORTS];
} __rte_cache_aligned;

struct port_info {
  int16_t num_ports;
  int16_t id[RTE_MAX_ETHPORTS];
  volatile struct rx_stats rx_stats;
  volatile struct tx_stats tx_stats[MAX_CLIENTS];
};

/* define common names for structures shared between server and client */
#define MP_CLIENT_RXQ_NAME "MProc_Client_%u_RX"
#define PKTMBUF_POOL_NAME "MProc_pktmbuf_pool_%u"
#define MZ_PORT_INFO "MProc_port_info"
#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

/*
 * Given the rx queue name template above, get the queue name
 */
  static inline const char *get_rx_queue_name(unsigned id) {
  /* buffer for return value. Size calculated by %u being replaced
  * by maximum 3 digits (plus an extra byte for safety) */
  static char buffer[sizeof(MP_CLIENT_RXQ_NAME) + 2];

  snprintf(buffer, sizeof(buffer), MP_CLIENT_RXQ_NAME, id);
  return buffer;
}

  static inline const char *get_mbuf_pool_name(unsigned id) {
  /* buffer for return value. Size calculated by %u being replaced
  * by maximum 3 digits (plus an extra byte for safety) */
  static char buffer[sizeof(PKTMBUF_POOL_NAME) + 2];

  snprintf(buffer, sizeof(buffer), PKTMBUF_POOL_NAME, id);
  return buffer;
}

