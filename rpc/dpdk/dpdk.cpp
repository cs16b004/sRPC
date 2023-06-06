/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */
#ifdef USE_DPDK_CLIENT
#include "boost/filesystem.hpp"
#include <iostream>
#include <chrono> 
#include "base/all.hpp"
#include "dpdk.hpp"
#include "dpdkdef.hpp"

using namespace std;
using namespace std::chrono;
using namespace boost::filesystem;

// Client definitions.
#define DPDK_SEND_BATCH (64)
// #define DPDK_SEND_BATCH (8)
#define PACKET_READ_SIZE (256)
#define PACKET_BUFFER_SIZE (PACKET_READ_SIZE*2)

#define MBUF_CACHE_SIZE (128)
//#define MBUF_CACHE_SIZE (512)

#define RTE_MP_TX_DESC_DEFAULT (8192)
#define RTE_MP_RX_DESC_DEFAULT (2<<14)
//#define RTE_MP_TX_DESC_DEFAULT (2<<12)
#define CLIENT_QUEUE_RINGSIZE (4096*128)

#define NO_FLAGS 0

/// Key used for RSS hashing
static constexpr uint8_t default_rss_key[] = {
    0x2c, 0xc6, 0x81, 0xd1, 0x5b, 0xdb, 0xf4, 0xf7, 0xfc, 0xa2,
    0x83, 0x19, 0xdb, 0x1a, 0x3e, 0x94, 0x6b, 0x9e, 0x38, 0xd9,
    0x2c, 0x9c, 0x03, 0xd1, 0xad, 0x99, 0x44, 0xa7, 0xd9, 0x56,
    0x3d, 0x59, 0x06, 0x3c, 0x25, 0xf3, 0xfc, 0x1f, 0xdc, 0x2a,
};

// Can be changed based on the recv threads number.
static int DPDK_RECV_QUEUE_PER_PORT = 1;

struct client_rx_buf {
  struct rte_mbuf *buffer[PACKET_BUFFER_SIZE];
  int16_t count;
};

struct client {
  struct rte_ring *rx_q;
  unsigned client_id;
};

/* The mbuf pool for packet rx */
static struct rte_mempool **pktmbuf_pools {nullptr};

/* array of info/queues for clients */
static struct client *dpdk_clients = NULL;

static struct client_rx_buf *cl_rx_buf = NULL;

/* the port details */
struct port_info *ports;

namespace rrr {

  void DpdkClient::generate_header(uint8_t* pkt, int src_idx) {
    int port_num = loc_id_ % DPDK_PORT_NUM;
    size_t data_size = InetHdrsTotSize + sizeof(dpdk_payload) ;	
    uint32_t client_ip = ipv4_from_str(DPDK_SRC_IP);
    uint32_t server_ip = ipv4_from_str(DPDK_DEST_IP);
    uint8_t client_mac[6], server_mac[6];
    mac_from_str(DPDK_SRC_MAC, client_mac);
    mac_from_str(DPDK_DEST_MAC, server_mac);
    eth_hdr_t *eth_hdr = (eth_hdr_t *)(pkt);
    ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt + sizeof(eth_hdr_t));
    udp_hdr_t *udp_hdr = (udp_hdr_t *)(pkt + sizeof(eth_hdr_t) +
        sizeof(ipv4_hdr_t));

    // TODO: yidawu tmp solution
    if (port_num == 1) {
      const char* mac_addr = "24:8a:07:1e:33:b3";
      client_ip = ipv4_from_str("192.168.2.5");
      mac_from_str(mac_addr, client_mac);
    }

    uint16_t src_port =  DPDK_SRC_PORT + loc_id_;
    gen_eth_header(eth_hdr, client_mac, server_mac);
    gen_ipv4_header(ip_hdr, client_ip, server_ip, data_size);
    ((rte_ipv4_hdr*)ip_hdr)->hdr_checksum = rte_ipv4_cksum((rte_ipv4_hdr*)ip_hdr);
    gen_udp_header(udp_hdr, src_port, DPDK_DEST_PORT, data_size);

    Log_debug("Src port %d, des port %d, Header size: %d, calc header size: %d", src_port, 
        DPDK_DEST_PORT, InetHdrsTotSize, sizeof(eth_hdr_t) + sizeof(ipv4_hdr_t) + sizeof(udp_hdr_t) );
  }

  void DpdkClient::generate_msg(uint8_t* pkt, uint8_t par_id) {
    dpdk_payload *payload = (dpdk_payload*) (pkt + InetHdrsTotSize);
    memset((uint8_t*)payload, 0, sizeof(dpdk_payload));
    payload->type = 'o';
    for(int i=0; i< sizeof(payload->content); i++) {
      payload->content[i] = 0;
    }
    for(int i=0; i< sizeof(payload->client_addr); i++) {
      payload->client_addr[i] = 0;
    }
    payload->checksum[0] = 0;
    payload->checksum[1] = 0;
    payload->par_id = par_id;
    for(int i = 0; i < sizeof(payload->latency); i++) payload->latency[i] = 0;

    payload->app_checksum[0] = 0x6f;
    payload->app_checksum[1] = par_id;
    payload->app_checksum[2] = 0;
    payload->app_checksum[3] = 0;

    payload->sw_resp = 0x70;
  }

  void DpdkClient::generate_msg_xid(uint8_t* pkt, int64_t xid ) {
    dpdk_payload *payload = (dpdk_payload*) (pkt + InetHdrsTotSize);
    payload->xid = xid;
  }  

  static const struct timespec delay = {0, 50};

  void DpdkClient::send(Marshal& req, int64_t xid, uint8_t par_id) {
    Log_debug("DpdkClient start send");
    if (DPDK_Objs[loc_id_] == nullptr) {
      DPDK_Objs[loc_id_] = this;
      Log_info("Set dpdk obj loc id %d addr %p", loc_id_, &DPDK_Objs);
    }

    int ret;
    uint8_t *data;
    int port_num = loc_id_ % DPDK_PORT_NUM;

    send_lock_.lock();

    uint16_t queue_id = loc_id_ / DPDK_PORT_NUM;

    struct rte_mbuf ** bufs = cl_rx_buf[recv_clients + loc_id_].buffer;

    if (bufs[count_] == nullptr) {
/*       int half = DPDK_SEND_BATCH >> 1;
     int src_port_idx = count_ & (8 - 1);
      src_port_idx = loc_id_ + src_port_idx << 3;*/
      int src_port_idx = loc_id_;
      bufs[count_] = rte_pktmbuf_alloc(pktmbuf_pools[recv_clients+loc_id_+1]);
      if(bufs[count_] == nullptr) {
        rte_panic("alloc failed");
      }
      uint8_t *pkt = rte_pktmbuf_mtod(bufs[count_], uint8_t *);
      ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt + sizeof(eth_hdr_t));
      udp_hdr_t *udp_hdr = (udp_hdr_t *)(pkt + sizeof(eth_hdr_t) + sizeof(ipv4_hdr_t));
      generate_header(pkt, src_port_idx);
      generate_msg(pkt, par_id);

      size_t data_size = InetHdrsTotSize + sizeof(dpdk_payload) ;
      bufs[count_]->ol_flags |= PKT_TX_IPV4 | PKT_TX_IP_CKSUM | PKT_TX_UDP_CKSUM;
      bufs[count_]->nb_segs = 1;
      bufs[count_]->pkt_len = InetHdrsTotSize + data_size;
      bufs[count_]->data_len = bufs[count_]->pkt_len; 
    }

    uint8_t *pkt = rte_pktmbuf_mtod(bufs[count_], uint8_t *);
    ipv4_hdr_t *ip_hdr = (ipv4_hdr_t *)(pkt + sizeof(eth_hdr_t));
    udp_hdr_t *udp_hdr = (udp_hdr_t *)(pkt + sizeof(eth_hdr_t) + sizeof(ipv4_hdr_t));
    generate_msg_xid(pkt, xid);
    req.read(pkt + InetHdrsTotSize + sizeof(dpdk_payload), req.content_size());
    ((rte_udp_hdr*)udp_hdr)->dgram_cksum = 0;
    ((rte_udp_hdr*)udp_hdr)->dgram_cksum = rte_ipv4_udptcp_cksum((rte_ipv4_hdr*)ip_hdr, (void*)udp_hdr);
    
    if(unlikely(++count_ == DPDK_SEND_BATCH)) {
      ret = rte_eth_tx_burst(port_num, queue_id, bufs, DPDK_SEND_BATCH);
      if (unlikely(ret < 0)) {
        rte_panic("Can't send burst\n");
      }
      if (unlikely(ret != count_)) {
        //Log_info("Shit happens!!! ret %d count_ %d", ret, count_);
        //verify(false);
        uint64_t num_pkts = count_;
        size_t retry_count = 0;
        while (ret != num_pkts) {
          ret += rte_eth_tx_burst(port_num, queue_id, &bufs[ret],
              num_pkts - ret);
          retry_count++;
          if (unlikely(retry_count == 1000000000)) {
            Log_info("stuck in rte_eth_tx_burst in port %u queue %u", port_num, queue_id);
            retry_count = 0;
          }
        }
      }
      // TODO: yidawu for test
      //usleep(1);
      //nanosleep(&delay, NULL);
      /*for(int i=0; i < count_; i++) {
        rte_pktmbuf_free(bufs[i]);
        bufs[i] = nullptr;
        }*/
      count_ = 0;
    }
    /*for(int i=0; i < out.size(); i++) {
      rte_pktmbuf_free(bufs[i]);
      }*/
    send_lock_.unlock();
    Log_debug("DpdkClient end send");
  }

  int DpdkClient::recv() {
    Log_debug("DpdkClient start recv");
    Log_debug("DpdkClient end recv");
    return 0;
  }

  static void process_recv_pkts(void **pkts, int rx_pkts) {
    unsigned lcore_id = rte_lcore_id();
    for(unsigned i=0; i < rx_pkts; i++) {
      uint8_t *data = rte_pktmbuf_mtod((struct rte_mbuf *)pkts[i], uint8_t *);
      struct rte_ether_hdr *eth_hdr = (struct rte_ether_hdr*)data;
      if (eth_hdr == NULL)
        rte_panic("Can't send burst\n");
      if (eth_hdr->ether_type != htons(IPEtherType)) {
        Log_debug("core %u: message abandoned due to ether_type is %04x \n", lcore_id, eth_hdr->ether_type);
        continue;
      }
      struct rte_ipv4_hdr *ipv4_hdr = (struct rte_ipv4_hdr*)((unsigned char *)eth_hdr + sizeof(struct rte_ether_hdr));
      if (ipv4_hdr == NULL)
        rte_panic("Can't send burst\n");
      if (ipv4_hdr->next_proto_id != IPHdrProtocol) {
        Log_debug("core %u: message abandoned due to proto type is %04x \n", lcore_id, ipv4_hdr->next_proto_id);
        continue;
      }
      struct rte_udp_hdr *udp = (struct rte_udp_hdr *)((unsigned char *)ipv4_hdr + sizeof(struct rte_ipv4_hdr));
      if (udp == NULL)
        rte_panic("Can't send burst\n");
      unsigned char *payload = (unsigned char *) (udp + 1);
      if (payload == NULL)
        rte_panic("Can't send burst\n");
      uint16_t src_port = ntohs(udp->src_port);
      uint16_t dst_port = ntohs(udp->dst_port);
      uint16_t idx = dst_port - DPDK_SRC_PORT;
      if (idx > sizeof(DPDK_Objs) || idx > 1000) {
        Log_info("core %u: Received an error UDP message src_port %d dst_port %d idx %d dpdk %p\n",
            lcore_id, src_port, dst_port, idx, &DPDK_Objs);
        continue;
      }
      //Log_debug("core %u: Received a UDP message src_port %d dst_port %d dpdk %p\n",
      //    lcore_id, src_port, dst_port, &DPDK_Objs);
      // idx &= 3;
      DpdkClient* dpdk_obj = (DpdkClient*) DPDK_Objs[idx];
      if(dpdk_obj == nullptr) {
        Log_info("core %u idx %d",lcore_id, idx);
        // verify(false);
        continue;
      }
      dpdk_obj->handle_read((void*)payload, ntohs(udp->dgram_len) - sizeof(struct rte_udp_hdr), lcore_id);
    }
    for(unsigned i=0; i < rx_pkts; i++) {
      rte_pktmbuf_free((struct rte_mbuf *)pkts[i]);
    }
  }

  static int dpdk_recv_loop(__rte_unused void *arg) {
    unsigned lcore_id = rte_lcore_id();
    // TODO: the port num is not right
    unsigned port_num = lcore_id/DPDK_RECV_QUEUE_PER_PORT;
    uint8_t queue_id = lcore_id - port_num * DPDK_RECV_QUEUE_PER_PORT;
    int client_id = port_num * 16 + queue_id;
    // unsigned port_num = *(int*)arg;
    // uint8_t queue_id = lcore_id - port_num * DPDK_RECV_QUEUE_PER_PORT;
    auto start_t = high_resolution_clock::now();
    uint64_t count = 0;
    for (;;) {
      struct rte_mbuf *buf[PACKET_READ_SIZE];
      uint16_t rx_count = 0;
      rx_count = rte_eth_rx_burst(port_num, queue_id, buf, PACKET_READ_SIZE);
      if (likely(rx_count > 0)) {
        Log_debug("receive port_num %d queue id %d rx count %d", port_num, queue_id, rx_count);
        count += rx_count;
        if(count > 4000000) {
          auto stop_t = high_resolution_clock::now();
          auto duration = duration_cast<microseconds>(stop_t - start_t);
          Log_info("port %d queue %d recv %ld in %ld microsecond in client %d",  port_num, queue_id, count, duration.count(), client_id);
          fflush(stdout);
          start_t = stop_t ;
          count = 0 ;
        }
        process_recv_pkts((void **)buf, rx_count);
      }
    }
  }

  static int
    init_mbuf_pools(void)
    {
      ports->num_ports = DPDK_PORT_NUM;
      const unsigned int num_mbufs_server =
        RTE_MP_RX_DESC_DEFAULT * ports->num_ports;
      const unsigned int num_mbufs_client =
        num_clients * (CLIENT_QUEUE_RINGSIZE +
            RTE_MP_TX_DESC_DEFAULT * ports->num_ports);
      const unsigned int num_mbufs_mp_cache =
        (num_clients + 1) * MBUF_CACHE_SIZE;
      // const unsigned int num_mbufs =
      //  num_mbufs_server + num_mbufs_client + num_mbufs_mp_cache;

      const uint32_t num_mbufs_const = 8192;
      //const uint32_t num_mbufs_const = 8192 * 8;
      if (pktmbuf_pools == nullptr) {
        pktmbuf_pools = (struct rte_mempool**)malloc((num_clients+1)
            * sizeof(struct rte_mempool *));
      }

      // don't pass single-producer/single-consumer flags to mbuf create as it seems
      // faster to use a cache instead.
      for(int i=0; i < num_clients + 1; i++) {
        const char *name = get_mbuf_pool_name(i);
        uint32_t cache_size = MBUF_CACHE_SIZE;
        uint32_t num_mbufs = num_mbufs_const;
        if (i < recv_clients) {
        // if (i < DPDK_PORT_NUM * DPDK_RECV_QUEUE_PER_PORT) {
          cache_size = cache_size;
          num_mbufs = num_mbufs * 256;
        } else{
          num_mbufs = num_mbufs * 8;
        }
        pktmbuf_pools[i] = rte_pktmbuf_pool_create(name, num_mbufs,
            MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
        Log_debug("Creating mbuf pool '%s' [%u mbufs] ...\n", name, num_mbufs);
        if (pktmbuf_pools[i] == NULL)
          rte_panic("create mbuf pool failed\n");
      }

      return pktmbuf_pools[0] == NULL;
    }

  static int init_port(uint16_t port_num) {
    /* for port configuration all features are off by default */
    struct rte_eth_conf port_conf;
    memset(&port_conf, 0, sizeof(struct rte_eth_conf));
    //port_conf.rxmode.mq_mode = ETH_MQ_RX_NONE;
    port_conf.rxmode.mq_mode = ETH_MQ_RX_RSS;
    // Use hash on UDP for recv queues.
    port_conf.rx_adv_conf.rss_conf.rss_key =
      const_cast<uint8_t *>(default_rss_key);
    port_conf.rx_adv_conf.rss_conf.rss_hf = ETH_RSS_UDP;
    port_conf.rx_adv_conf.rss_conf.rss_key_len = 40;
    //port_conf.rxmode.max_rx_pkt_len = RTE_ETHER_MAX_LEN;
    port_conf.rxmode.offloads = 0;
    port_conf.txmode.mq_mode = ETH_MQ_TX_NONE;
    //port_conf.txmode.mq_mode = ETH_MQ_TX_RSS;
    //port_conf.txmode.offloads = DEV_TX_OFFLOAD_MULTI_SEGS;  
    const uint16_t rx_rings = DPDK_RECV_QUEUE_PER_PORT, tx_rings = send_clients/DPDK_PORT_NUM;
    uint16_t rx_ring_size = RTE_MP_RX_DESC_DEFAULT;
    uint16_t tx_ring_size = RTE_MP_TX_DESC_DEFAULT;

    rte_eth_rxconf eth_rx_conf;
    memset(&eth_rx_conf, 0, sizeof(eth_rx_conf));
    eth_rx_conf.rx_thresh.pthresh = 8;
    eth_rx_conf.rx_thresh.hthresh = 0;
    eth_rx_conf.rx_thresh.wthresh = 0;
    eth_rx_conf.rx_free_thresh = 0;
    eth_rx_conf.rx_drop_en = 0;

    rte_eth_txconf eth_tx_conf;
    memset(&eth_tx_conf, 0, sizeof(eth_tx_conf));
    eth_tx_conf.tx_thresh.pthresh = 32;
    eth_tx_conf.tx_thresh.hthresh = 0;
    eth_tx_conf.tx_thresh.wthresh = 0;
    eth_tx_conf.tx_free_thresh = 0;
    eth_tx_conf.tx_rs_thresh = 0;
    //eth_tx_conf.offloads = DEV_TX_OFFLOAD_MULTI_SEGS;

    uint16_t q;
    int retval;

    Log_debug("Port %u init ...", port_num);
    fflush(stdout);

    /* Standard DPDK port initialisation - config port, then set up
     * rx and tx rings */
    if ((retval = rte_eth_dev_configure(port_num, rx_rings, tx_rings,
            &port_conf)) != 0)
      return retval;
    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port_num, &rx_ring_size,
        &tx_ring_size);
    Log_info("Port %u init ... rx ring size:%d", port_num, rx_ring_size);
    if (retval != 0)
      return retval;
    for (q = 0; q < rx_rings; q++) {
      retval = rte_eth_rx_queue_setup(port_num, q, rx_ring_size,
          rte_eth_dev_socket_id(port_num),
          &eth_rx_conf, pktmbuf_pools[q]);
          //NULL, pktmbuf_pools[port_num]);
      if (retval < 0) return retval;
    }
    for ( q = 0; q < tx_rings; q ++ ) {
      retval = rte_eth_tx_queue_setup(port_num, q, tx_ring_size,
          rte_eth_dev_socket_id(port_num),
          &eth_tx_conf);
      if (retval < 0) return retval;
    }

    retval = rte_eth_promiscuous_enable(port_num);
    if (retval < 0)
      return retval;

    retval  = rte_eth_dev_start(port_num);
    if (retval < 0) return retval;

    Log_debug( "done: \n");

    return 0;
  }

  static int
    init_shm_rings(void)
    {
      unsigned i;
      unsigned socket_id;
      const char * q_name;
      const unsigned ringsize = CLIENT_QUEUE_RINGSIZE;

      dpdk_clients = (struct client*) rte_malloc("client details",
          sizeof(*dpdk_clients) * num_clients, 0);
      if (dpdk_clients == NULL)
        rte_exit(EXIT_FAILURE, "Cannot allocate memory for client program details\n");

      for (i = 0; i < num_clients; i++) {
        /* Create an RX queue for each client */
        socket_id = rte_socket_id();
        q_name = get_rx_queue_name(i);
        dpdk_clients[i].rx_q = rte_ring_create(q_name,
            ringsize, socket_id,
            RING_F_SP_ENQ | RING_F_SC_DEQ ); /* single prod, single cons */
        if (dpdk_clients[i].rx_q == NULL)
          rte_exit(EXIT_FAILURE, "Cannot create rx ring queue for client %d\n", i);
      }
      return 0;
    }

  static void* dpdk_primary_init(void* arg) {
    int retval;
    int port_id = 0;

    const struct rte_memzone *mz;

    char *argv =  const_cast<char *>(DPDK_START_OPTIONS);

    const char *rte_argv[100];
    int i = 0;
    vector<const char *> argvs;
    argvs.push_back(strtok(argv, " "));
    while(true)
    {
      const char* tmp = strtok(NULL, " ");
      if (tmp == NULL) {
        break;
      }
      argvs.push_back(tmp);
    }

    path cur_path(current_path());
    Log_info(cur_path.string().c_str());

    int rte_argc = argvs.size();
    retval = rte_eal_init(rte_argc, const_cast<char **>(argvs.data()));
    if (retval < 0)
      rte_panic("Cannot init EAL\n");  

    /* set up array for port data */
    mz = rte_memzone_reserve(MZ_PORT_INFO, sizeof(*ports), rte_socket_id(), NO_FLAGS);
    if (mz == NULL)
      rte_exit(EXIT_FAILURE, "Cannot reserve memory zone for port information\n");
    memset(mz->addr, 0, sizeof(*ports));
    ports = (struct port_info *) mz->addr;
    
    RTE_ETH_FOREACH_DEV(port_id) {
      DPDK_PORT_NUM++;
    }

    DPDK_RECV_QUEUE_PER_PORT = recv_clients/DPDK_PORT_NUM ;

    /* initialise mbuf pools */
    retval = init_mbuf_pools();
    if (retval != 0)
      rte_exit(EXIT_FAILURE, "Cannot create needed mbuf pools\n");

    /* now initialise the ports we will use */
    RTE_ETH_FOREACH_DEV(port_id) {
      retval = init_port(port_id);
      if (retval != 0)
        rte_exit(EXIT_FAILURE, "Cannot initialise port %d\n", 0);
    }

    /* initialise the client queues/rings for inter-eu comms */
    // init_shm_rings();

    cl_rx_buf = (struct client_rx_buf *)calloc(num_clients, sizeof(cl_rx_buf[0]));  
    assert (DPDK_PORT_NUM * DPDK_RECV_QUEUE_PER_PORT <= recv_clients);
    int core_id = DPDK_PORT_NUM * DPDK_RECV_QUEUE_PER_PORT;
    // Only support at most two ports.
    if (DPDK_PORT_NUM > 1) {
      int port_id_1 = 1;
      for (int i=0; i < DPDK_RECV_QUEUE_PER_PORT; i++) {
        rte_eal_remote_launch(dpdk_recv_loop, &port_id_1, --core_id);
      }
    }
    int port_id_0 = 0;
    for (int i=0; i < DPDK_RECV_QUEUE_PER_PORT-1; i++) {
      rte_eal_remote_launch(dpdk_recv_loop, &port_id_0, --core_id);
    }
    verify(--core_id == 0);
    DPDK_CLIENT_INIT_FINISHED = true;
    Log_info("DpdkClient set finished true");
    dpdk_recv_loop(&port_id_0);
    /*DPDK_CLIENT_INIT_FINISHED = true;
    Log_info("DpdkClient set finished true");
    while(true);*/
    return 0;
  }  

  void DpdkClient::set_config(int n_send, int n_recv, 
      const char* src_mac, const char* src_ip, int src_port,
      const char* dest_mac, const char* dest_ip, int dest_port, 
      const char* start_options, bool simple_mode, bool no_stat){
    send_clients = n_send;
    recv_clients = n_recv;
    DPDK_DEST_MAC = dest_mac;
    DPDK_DEST_IP = dest_ip;
    DPDK_DEST_PORT = dest_port;
    DPDK_SRC_MAC = src_mac;
    DPDK_SRC_IP = src_ip;
    DPDK_SRC_PORT = src_port;
    num_clients = send_clients + recv_clients;
    DPDK_START_OPTIONS = start_options;
    dpdk_simple_mode_ = simple_mode;
    dpdk_no_stat_ = no_stat;
  }

  void DpdkClient::init() {
    Log_debug("DpdkClient start init");
    init_lock_.lock();
    if (!DPDK_CLIENT_INIT) {
      DPDK_CLIENT_INIT = true;
      pthread_t ptid; 
      // Creating a new thread for receiving loop. 
      pthread_create(&ptid, NULL, &dpdk_primary_init, NULL);
      while (!DPDK_CLIENT_INIT_FINISHED) {
        Log_info("Wait for DpdkClient set finished true");
        sleep(1);
      }
      Log_debug("DpdkClient end init");    
    }
    init_lock_.unlock();
  }
}
#endif
