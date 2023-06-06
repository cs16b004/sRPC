
#include <cstdint>
#include "transport.hpp"
#include <rte_ethdev.h>
#include <rte_eth_ctrl.h>

#define DPDK_RX_DESC_SIZE           1024
#define DPDK_TX_DESC_SIZE           1024

#define DPDK_NUM_MBUFS              8192
#define DPDK_MBUF_CACHE_SIZE        250
#define DPDK_RX_BURST_SIZE          64
#define DPDK_TX_BURST_SIZE          1

#define DPDK_RX_WRITEBACK_THRESH    64

#define DPDK_PREFETCH_NUM           2
namespace rrr{

char* DpdkTransport::getMacFromIp(std::string ip){
    return "02:de:ad:be:ef:60";
}

uint32_t DpdkTransport::connect(std::string server_ip,uint32_t port){


    NetAddress *s_addr = new NetAddress(getMacFromIp(server_ip),server_ip.c_str(),port);
    Connection *conn = new Connection(*s_addr);
    uint32_t conn_id;
    conn_lock.lock();
    conn_counter++;
    conn_id = conn_counter;
    conn_lock.unlock();
    this->connections_[conn_id] = conn;
    return conn_id;
}
int DpdkTransport::dpdk_rx_loop(void* arg) {
    auto rx_info = reinterpret_cast<dpdk_thread_info*>(arg);
    auto dpdk_th = rx_info->dpdk_th;
    unsigned lcore_id = rte_lcore_id();
    uint16_t port_id = rx_info->port_id;

    Log_info("Enter receive thread %d on core %d on port_id %d on queue %d",
             rx_info->thread_id, lcore_id, port_id, rx_info->queue_id);

    while (!dpdk_th->force_quit) {
        struct rte_mbuf **bufs = rx_info->buf;
        const uint16_t max_size = rx_info->max_size;
        uint16_t nb_rx = rte_eth_rx_burst(port_id, rx_info->queue_id, 
                                          bufs, max_size);
        if (unlikely(nb_rx == 0))
            continue;
        /* Log_debug("Thread %d received %d packets on queue %d and port_id %d", */
        /*           rx_info->thread_id, nb_rx, rx_info->queue_id, port_id); */

        rx_info->count = nb_rx;

        dpdk_th->process_incoming_packets(rx_info);
    }
    return 0;
}

void DpdkTransport::process_incoming_packets(dpdk_thread_info* rx_info) {
    /* Prefetch packets */
    for (int i = 0; (i < DPDK_PREFETCH_NUM) && (i < rx_info->count); i++)
        rte_prefetch0(rte_pktmbuf_mtod(rx_info->buf[i], uint8_t*));

    for (int i = 0; i < rx_info->count; i++) {
        uint8_t* pkt_ptr = rte_pktmbuf_mtod((struct rte_mbuf*)rx_info->buf[i], uint8_t*);
        unsigned pkt_offset = 0;

        /* Check Ethernet Header */
        auto eth_hdr = reinterpret_cast<struct rte_ether_hdr*>(pkt_ptr);
        if (eth_hdr == nullptr)
            Log_fatal("Received packet was NULL");
        if (eth_hdr->ether_type != htons(EtherTypeIP)) {
            /* Log_debug("Thread %d: packet droped as eth type is %04x", */
            /*           rx_info->thread_id, ntohs(eth_hdr->ether_type)); */
            rx_info->stat.pkt_error++;
            rx_info->stat.pkt_eth_type_err++;
            continue;
        }

        /* Check IPv4 Header */
        pkt_offset += sizeof(struct rte_ether_hdr);
        auto ipv4_hdr = reinterpret_cast<struct rte_ipv4_hdr*>(pkt_ptr + pkt_offset);
        if (ipv4_hdr == nullptr)
            Log_fatal("IPv4 is NULL");
        if (ipv4_hdr->next_proto_id != IPProtUDP) {
            /* Log_debug("Thread %d: packet droped as ipv4 protocol is %02x", */
            /*           rx_info->thread_id, ipv4_hdr->next_proto_id); */
            rx_info->stat.pkt_error++;
            rx_info->stat.pkt_ip_prot_err++;
            continue;
        }

        /* Check UDP Header */
        pkt_offset += sizeof(struct rte_ipv4_hdr);
        auto udp_hdr = reinterpret_cast<struct rte_udp_hdr*>(pkt_ptr + pkt_offset);
        if (udp_hdr == nullptr)
            Log_fatal("UDP is NULL");
        uint16_t dest_port = ntohs(udp_hdr->dst_port);
        uint16_t rx_udp_port = src_addr_[rx_info->port_id].port/* + rx_info->queue_id */;
        if (dest_port != rx_udp_port) {
            /* Log_debug("Thread %d: packet droped as udp port is %d was not %d", */
            /*           rx_info->thread_id, dest_port, rx_udp_port); */
            rx_info->stat.pkt_error++;
            rx_info->stat.pkt_port_num_err++;
            continue;
        }

        int server_id = -1;
        uint16_t src_port = ntohs(udp_hdr->src_port);
        NetAddress addr(eth_hdr->s_addr.addr_bytes, (uint32_t) ipv4_hdr->src_addr, src_port);
        for (auto& dest : dest_addr_) {
            if (addr == dest.second) {
                rx_info->stat.pkt_port_dest[dest.first]++;
                server_id = dest.first;
                break;
            }
        }
        if (server_id == -1) {
            server_id = dest_addr_.size();
            dest_addr_[server_id] = addr;
            rx_info->stat.pkt_port_dest[server_id] = 1;
        }

        Log_debug("Thread %d on queue %d and port_id %d",
                  rx_info->thread_id, rx_info->queue_id, rx_info->port_id);

        pkt_offset += sizeof(struct rte_udp_hdr);

        uint8_t* payload = reinterpret_cast<uint8_t*>(pkt_ptr + pkt_offset);
        int payload_len = ntohs(udp_hdr->dgram_len) - sizeof(struct rte_udp_hdr);
        int app_offset = response_handler(payload, payload_len,
                                          server_id, rx_info->thread_id);
        if (app_offset < 0) {
            Log_debug("Thread %d: packet drop as application process had error: %d",
                      app_offset);
            rx_info->stat.pkt_error++;
            rx_info->stat.pkt_app_err++;
            continue;
        }
        rx_info->stat.pkt_count++;

        /* TODO: check the total packet size with how much we processed */

        /* Prefetch packets */
        int prefetch_idx = i + DPDK_PREFETCH_NUM;
        if (prefetch_idx < rx_info->count)
            rte_prefetch0(rte_pktmbuf_mtod(rx_info->buf[prefetch_idx], uint8_t*));
    }

    for (int i = 0; i < rx_info->count; i++)
        rte_pktmbuf_free(rx_info->buf[i]);
}

void DpdkTransport::send(uint8_t* payload, unsigned length,
                      int server_id, int client_id) {
    dpdk_thread_info* tx_info = &(thread_tx_info[client_id]);
    if (tx_info->count == 0) {
        int ret = tx_info->buf_alloc(tx_mbuf_pool[tx_info->thread_id]);
        if (ret < 0)
            rte_panic("couldn't allocate mbufs");
    }

    uint8_t* pkt_buf = rte_pktmbuf_mtod(tx_info->buf[tx_info->count], uint8_t*);

    int hdr_len = make_pkt_header(pkt_buf, length, tx_info->port_id,
                                  server_id, tx_info->udp_port_id);
    /* We want to have uniform distribution accross all of rx threads */
    /* tx_info->udp_port_id = (tx_info->udp_port_id + 1) % rx_queue_; */
    memcpy(pkt_buf + hdr_len, payload, length);

    int data_size = hdr_len + length;
    tx_info->buf[tx_info->count]->ol_flags = (PKT_TX_IPV4);
    tx_info->buf[tx_info->count]->nb_segs = 1;
    tx_info->buf[tx_info->count]->pkt_len = data_size;
    tx_info->buf[tx_info->count]->data_len = data_size;
    Log_debug("Thread %d send packet to server %d with size of %d",
              client_id, server_id, data_size);

    tx_info->count++;
    if (unlikely(tx_info->count == tx_info->max_size)) {
        int ret = rte_eth_tx_burst(tx_info->port_id, tx_info->queue_id,
                                   tx_info->buf, tx_info->count);
        if (unlikely(ret < 0))
            rte_panic("Tx couldn't send\n");

        if (unlikely(ret != tx_info->count))
            rte_panic("Couldn't send all packets\n");

        tx_info->stat.pkt_count += tx_info->count;
        tx_info->count = 0;
    }
    else
        tx_info->stat.pkt_error += tx_info->count;
}

int DpdkTransport::make_pkt_header(uint8_t *pkt, int payload_len,
                                int src_id, int dest_id, int port_offset) {
    NetAddress& src_addr = src_addr_[src_id];
    NetAddress& dest_addr = dest_addr_[dest_id];

    unsigned pkt_offset = 0;
    eth_hdr_t* eth_hdr = reinterpret_cast<eth_hdr_t*>(pkt);
    gen_eth_header(eth_hdr, src_addr.mac, dest_addr.mac);

    pkt_offset += sizeof(eth_hdr_t);
    ipv4_hdr_t* ipv4_hdr = reinterpret_cast<ipv4_hdr_t*>(pkt + pkt_offset);
    gen_ipv4_header(ipv4_hdr, src_addr.ip, dest_addr.ip, payload_len);

    pkt_offset += sizeof(ipv4_hdr_t);
    udp_hdr_t* udp_hdr = reinterpret_cast<udp_hdr_t*>(pkt + pkt_offset);
    int client_port_addr = src_addr.port + port_offset;
    gen_udp_header(udp_hdr, client_port_addr, dest_addr.port, payload_len);

    pkt_offset += sizeof(udp_hdr_t);
    return pkt_offset;
}

void DpdkTransport::init(Config* config) {
    addr_config(config->host_name_, config->get_net_info());

    Config::CpuInfo cpu_info = config->get_cpu_info();
    const char* argv_str = config->get_dpdk_options();
    tx_threads_ = config->get_host_threads();
    rx_threads_ = (int) (config->get_dpdk_rxtx_thread_ratio() * (float) tx_threads_);
    if ((rx_threads_ > cpu_info.max_rx_threads) || (rx_threads_ < 1)) {
        Log_error("Total number of threads %d cannot be more than %d or less than 1. "
                  "Change the rxtx_threads", rx_threads_, cpu_info.max_rx_threads);
        assert(false);
    }

    /* TODO: remove this */
    tx_threads_ = rx_threads_;

    main_thread = std::thread([this, argv_str](){
        this->init_dpdk_main_thread(argv_str);
    });
    sleep(2);
}

void DpdkTransport::init_dpdk_main_thread(const char* argv_str) {
    std::vector<const char*> dpdk_argv;
    char* tmp_arg = const_cast<char*>(argv_str);
    const char* arg_tok = strtok(tmp_arg, " ");
    while (arg_tok != NULL) {
        dpdk_argv.push_back(arg_tok);
        arg_tok = strtok(NULL, " ");
    }
    int argc = dpdk_argv.size();
    char** argv = const_cast<char**>(dpdk_argv.data());

    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

    port_num_ = rte_eth_dev_count_avail();
    if (port_num_ < 1)
        rte_exit(EXIT_FAILURE, "Error with insufficient number of ports\n");

    /* HACK: to have same number of queues across all ports (T_T) */
    int tx_th_remainder = (tx_threads_ % port_num_);
    if (tx_th_remainder) {
        Log_info("Adjusting Tx thread number from %d to %d",
                 tx_threads_, tx_threads_ + (port_num_ - tx_th_remainder));
        tx_threads_ += (port_num_ - tx_th_remainder);
    }

    int rx_th_remainder = (rx_threads_ % port_num_);
    if (rx_th_remainder) {
        Log_info("Adjusting Rx thread number from %d to %d",
                 rx_threads_, rx_threads_ + (port_num_ - rx_th_remainder));
        rx_threads_ += (port_num_ - rx_th_remainder);
    }

    tx_queue_ = tx_threads_ / port_num_;
    rx_queue_ = rx_threads_ / port_num_;

    tx_mbuf_pool = new struct rte_mempool*[tx_threads_];
    for (int pool_idx = 0; pool_idx < tx_threads_; pool_idx++) {
        char pool_name[1024];
        sprintf(pool_name, "TX_MBUF_POOL_%d", pool_idx);
        /* TODO: Fix it for machines with more than one NUMA node */
        tx_mbuf_pool[pool_idx] = rte_pktmbuf_pool_create(pool_name, DPDK_NUM_MBUFS,
                                                         DPDK_MBUF_CACHE_SIZE, 0, 
                                                         RTE_MBUF_DEFAULT_BUF_SIZE, 
                                                         rte_socket_id());
        if (tx_mbuf_pool[pool_idx] == NULL)
            rte_exit(EXIT_FAILURE, "Cannot create tx mbuf pool %d\n", pool_idx);
    }

    rx_mbuf_pool = new struct rte_mempool*[rx_threads_];
    for (int pool_idx = 0; pool_idx < rx_threads_; pool_idx++) {
        char pool_name[1024];
        sprintf(pool_name, "RX_MBUF_POOL_%d", pool_idx);
        /* TODO: Fix it for machines with more than one NUMA node */
        rx_mbuf_pool[pool_idx] = rte_pktmbuf_pool_create(pool_name, DPDK_NUM_MBUFS,
                                                         DPDK_MBUF_CACHE_SIZE, 0, 
                                                         RTE_MBUF_DEFAULT_BUF_SIZE, 
                                                         rte_socket_id());
        if (rx_mbuf_pool[pool_idx] == NULL)
            rte_exit(EXIT_FAILURE, "Cannot create rx mbuf pool %d\n", pool_idx);
    }

    /* Will initialize buffers in port_init function */
    thread_rx_info = new dpdk_thread_info[rx_threads_];
    thread_tx_info = new dpdk_thread_info[tx_threads_];

    // port_info_ = new qdma_port_info[port_num_];

     uint16_t portid;
     RTE_ETH_FOREACH_DEV(portid) {
    //     int32_t config_bar, user_bar, bypass_bar;
    //     ret = rte_pmd_qdma_get_bar_details(portid, &config_bar, &user_bar, &bypass_bar);
    //     if (ret < 0)
    //         rte_exit(EXIT_FAILURE, "Couldn't read qdma bar\n");

    //     port_info_[portid].config_bar_idx = config_bar;
    //     port_info_[portid].user_bar_idx = user_bar;
    //     port_info_[portid].bypass_bar_idx = bypass_bar;

    //     Log_info("PCIe bars for port %d: config bar: %d, user bar: %d, bypass bar: %d",
    //              portid, config_bar, user_bar, bypass_bar);

        if (port_init(portid) != 0)
            rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n",
                     portid);
    }

    Log_info("DPDK tx threads %d, rx threads %d", tx_threads_, rx_threads_);

    uint16_t lcore;
    for (lcore = 1; lcore < rx_threads_; lcore++) {
        int retval = rte_eal_remote_launch(dpdk_rx_loop, &thread_rx_info[lcore], lcore);
        if (retval < 0)
            rte_exit(EXIT_FAILURE, "Couldn't lunch core %d\n", lcore);
    }

    lcore = 0;
    dpdk_rx_loop(&thread_rx_info[lcore]);
}

void DpdkTransport::addr_config(std::string host_name,
                       std::vector<Config::NetworkInfo> net_info) {
    Log_info("Setting up netowkr info....");
    for (auto& net : net_info) {
        std::map<int, NetAddress>* addr;
        if (host_name == net.name)
            addr = &src_addr_;
        else
            addr = &dest_addr_;

        /* if (net.type == host_type) */
        /*     addr = &src_addr_; */
        /* else */
        /*     addr = &dest_addr_; */

        auto it = addr->find(net.id);
        assert(it == addr->end());
        Log_info("Adding a host with id %d : info :\n %s",net.id,net.to_string().c_str());
        addr->emplace(std::piecewise_construct,
                      std::forward_as_tuple(net.id),
                      std::forward_as_tuple(net.mac.c_str(),
                                            net.ip.c_str(),
                                            net.port));
    }
}

int DpdkTransport::port_init(uint16_t port_id) {
    struct rte_eth_conf port_conf;
    uint16_t nb_rxd = DPDK_RX_DESC_SIZE;
    uint16_t nb_txd = DPDK_TX_DESC_SIZE;
    int retval;
    uint16_t q;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_txconf txconf;
    struct rte_eth_rxconf rxconf;
    struct rte_device *dev;

    dev = rte_eth_devices[port_id].device;
    if (dev == nullptr) {
        Log_error("Port %d is already removed", port_id);
        return -1;
    }

    if (!rte_eth_dev_is_valid_port(port_id))
        return -1;

    retval = rte_eth_dev_info_get(port_id, &dev_info);
    if (retval != 0) {
        Log_error("Error during getting device (port %u) info: %s",
                  port_id, strerror(-retval));
        return retval;
    }

    memset(&port_conf, 0x0, sizeof(struct rte_eth_conf));
    memset(&txconf, 0x0, sizeof(struct rte_eth_txconf));
    memset(&rxconf, 0x0, sizeof(struct rte_eth_rxconf));

    retval = rte_eth_dev_configure(port_id, rx_queue_, tx_queue_, &port_conf);
    if (retval != 0) {
        Log_error("Error during device configuration (port %u) info: %s",
                  port_id, strerror(-retval));
        return retval;
    }

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port_id, &nb_rxd, &nb_txd);
    if (retval != 0) {
        Log_error("Error during setting number of rx/tx descriptor (port %u) info: %s",
                  port_id, strerror(-retval));
        return retval;
    }

    rxconf.rx_thresh.wthresh = DPDK_RX_WRITEBACK_THRESH;
    for (q = 0; q < rx_queue_; q++) {
        /* TODO: Maybe we should set the type of queue in QDMA
         * to be stream/memory mapped */
        int pool_idx = port_id * rx_queue_ + q;
        retval = rte_eth_rx_queue_setup(port_id, q, nb_rxd,
                                        rte_eth_dev_socket_id(port_id),
                                        &rxconf, rx_mbuf_pool[pool_idx]);
        if (retval < 0) {
            Log_error("Error during rx queue %d setup (port %u) info: %s",
                      q, port_id, strerror(-retval));
            return retval;
        }
    }

    for (q = 0; q < tx_queue_; q++) {
        /* TODO: Maybe we should set the type of queue in QDMA
         * to be stream/memory mapped */
        retval = rte_eth_tx_queue_setup(port_id, q, nb_txd,
                                        rte_eth_dev_socket_id(port_id),
                                        &txconf);
        if (retval < 0) {
            Log_error("Error during tx queue %d setup (port %u) info: %s",
                      q, port_id, strerror(-retval));
            return retval;
        }
    }

    retval = rte_eth_dev_start(port_id);
    if (retval < 0) {
        Log_error("Error during starting device (port %u) info: %s",
                  port_id, strerror(-retval));
        return retval;
    }

    for (int i = 0; i < rx_queue_; i++) {
        int thread_id = port_id * rx_queue_ + i;
        Log_debug("Create thread %d info on port %d and queue %d",
                    thread_id, port_id, i);
        thread_rx_info[thread_id].init(this, thread_id, port_id, i, DPDK_RX_BURST_SIZE);
    }

    for (int i = 0; i < tx_queue_; i++) {
        int thread_id = port_id * tx_queue_ + i;
        thread_tx_info[thread_id].init(this, thread_id, port_id, i, DPDK_TX_BURST_SIZE);
    }

    return 0;
}

int DpdkTransport::port_close(uint16_t port_id) {
    // struct rte_pmd_qdma_dev_attributes dev_attr;
    // int retval;

    // retval = rte_pmd_qdma_get_device_capabilities(port_id, &dev_attr);
    // if (retval < 0) {
    //     Log_error("rte_pmd_qdma_get_device_capabilities failed for port: %d",
    //               port_id);
    //     return retval;
    // }

    // int user_bar_idx = port_info_[port_id].user_bar_idx;
    // if ((dev_attr.device_type == RTE_PMD_QDMA_DEVICE_SOFT) &&
    //     (dev_attr.ip_type == RTE_PMD_EQDMA_SOFT_IP)) {
    //     qdma_port_info::qdma_reg_write(user_bar_idx, C2H_CONTROL_REG,
    //                                    C2H_STREAM_MARKER_PKT_GEN_VAL, port_id);

    //     uint16_t retry = 50;
    //     while (retry) {
    //         usleep(500);
    //         int reg_val = qdma_port_info::qdma_reg_read(user_bar_idx, C2H_STATUS_REG, port_id);
    //         if (reg_val & MARKER_RESPONSE_COMPLETION_BIT)
    //             break;

    //         Log_error("Failed to receive c2h marker completion, retry count = %u\n",
    //                   (50 - (retry-1)));
    //         retry--;
    //     }
    // }

    rte_eth_dev_stop(port_id);
   // rte_pmd_qdma_dev_close(port_id);
    return 0;
}

int DpdkTransport::port_reset(uint16_t port_id) {
    struct rte_device* dev = rte_eth_devices[port_id].device;
    if (dev == nullptr) {
        Log_error("Port %d is already removed", port_id);
        return -1;
    }

    int retval = port_close(port_id);
    if (retval < 0) {
        Log_error("Error: Failed to close device for port: %d", port_id);
        return retval;
    }

    retval = rte_eth_dev_reset(port_id);
    if (retval < 0) {
        Log_error("Error: Failed to reset device for port: %d", port_id);
        return -1;
    }

    retval = port_init(port_id);
    if (retval < 0) {
        Log_error("Error: Failed to initialize device for port %d", port_id);
        return -1;
    }

    return 0;
}

void DpdkTransport::shutdown() {
    main_thread.join();
    rte_eal_mp_wait_lcore();

  //  qdma_port_info::print_opennic_regs(port_info_[0].user_bar_idx);
    for (int port_id = 0; port_id < port_num_; port_id++) {
        struct rte_device *dev = rte_eth_devices[port_id].device;
        if (dev == nullptr) {
            Log_error("Port %d is already removed", port_id);
            continue;
        }

        rte_eth_dev_stop(port_id);
        rte_eth_dev_close(port_id);
        int ret = rte_dev_remove(dev);
        if (ret < 0)
            Log_error("Failed to remove device on port: %d", port_id);

    }

    rte_eal_cleanup();

    packet_stats total_rx_stat, total_tx_stat;
    for (int i = 0; i < tx_threads_; i++)
        total_tx_stat.merge(thread_tx_info[i].stat);
    for (int i = 0; i < rx_threads_; i++) {
        Log_info("Log Rx tid: %d, port id: %d, qid: %d", 
                 thread_rx_info[i].thread_id, thread_rx_info[i].port_id,
                 thread_rx_info[i].queue_id);
        thread_rx_info[i].stat.show_statistics();
        total_rx_stat.merge(thread_rx_info[i].stat);
    }
    Log_info("Number of packets: TX: %ld, RX: %ld",
             total_tx_stat.pkt_count, total_rx_stat.pkt_count);
    total_rx_stat.show_statistics();
}

void DpdkTransport::trigger_shutdown() {
    force_quit = true;
}

void DpdkTransport::register_resp_callback() {
    response_handler = [&](uint8_t* data, int data_len,
                          int server_id, int client_id) -> int {
        Log_debug("client %d got xid %ld", client_id, *reinterpret_cast<uint64_t*>(data));
        this->send(data, data_len, server_id, client_id);
        return data_len;
    };
}

/* void DpdkTransport::register_resp_callback(Workload* app) { */
/*     response_handler = [app](uint8_t* data, int data_len, int id) -> int { */
/*         return app->process_workload(data, data_len, id); */
/*     }; */
/* } */

void dpdk_thread_info::init(DpdkTransport* th, int th_id, int p_id, 
                                        int q_id, int burst_size) {
    dpdk_th = th;
    thread_id = th_id;
    port_id = p_id;
    queue_id = q_id;
    max_size = burst_size;
    buf = new struct rte_mbuf*[burst_size];
}

int dpdk_thread_info::buf_alloc(struct rte_mempool* mbuf_pool) {
    int retval = rte_pktmbuf_alloc_bulk(mbuf_pool, buf, max_size);
    return retval;
}

void NetAddress::init(const char* mac_i, const char* ip_i, const int port_i) {
    mac_from_str(mac_i, mac);
    ip = ipv4_from_str(ip_i);
    port = port_i;
}

NetAddress::NetAddress(const char* mac_i, const char* ip_i, const int port_i) {
    init(mac_i, ip_i, port_i);
}

NetAddress::NetAddress(const uint8_t* mac_i, const uint32_t ip_i, const int port_i) {
    memcpy(mac, mac_i, sizeof(mac));
    ip = ip_i;
    port = port_i;
}

bool NetAddress::operator==(const NetAddress& other) {
    if (&other == this)
        return true;

    for (uint8_t i = 0; i < sizeof(mac); i++)
        if (this->mac[i] != other.mac[i])
            return false;

    if ((this->ip != other.ip) || (this->port != other.port))
        return false;

    return true;
}

NetAddress& NetAddress::operator=(const NetAddress& other) {
    if (this == &other)
        return *this;

    memcpy(this->mac, other.mac, sizeof(this->mac));
    this->ip = other.ip;
    this->port = other.port;

    return *this;
}

void packet_stats::merge(packet_stats& other) {
    pkt_count += other.pkt_count;
    for (auto& pair : other.pkt_port_dest) {
        auto it = pkt_port_dest.find(pair.first);
        if (it != pkt_port_dest.end())
            it->second += pair.second;
        else
            pkt_port_dest[pair.first] = pair.second;
    }
    pkt_error += other.pkt_error;
    pkt_eth_type_err += other.pkt_eth_type_err;
    pkt_ip_prot_err += other.pkt_ip_prot_err;
    pkt_port_num_err += other.pkt_port_num_err;
    pkt_app_err += other.pkt_app_err;
}

void packet_stats::show_statistics() {
    if ((pkt_count == 0) && (pkt_error == 0)) return;

    Log_info("===================");
    Log_info("Network Statistics");
    Log_info("===================");
    for (auto& pair : pkt_port_dest)
        if (pair.second > 0)
            Log_info("Number of Packets from server %d: %ld", 
                     pair.first, pair.second);

    if (pkt_error == 0) return;

    Log_info("Total Errors: %ld", pkt_error);
    if (pkt_eth_type_err > 0)
        Log_info("Error on EtherType: %ld", pkt_eth_type_err);
    if (pkt_ip_prot_err > 0)
        Log_info("Error on IP Protocol: %ld", pkt_ip_prot_err);
    if (pkt_port_num_err > 0)
        Log_info("Error on Port Number: %ld", pkt_port_num_err);
    if (pkt_app_err > 0)
        Log_info("Error on Application: %ld", pkt_app_err);
}

// void qdma_port_info::print_opennic_regs(uint32_t bar_idx) {
//     Log_info("===================");
//     Log_info("OpenNIC Statistics:");
//     Log_info("===================");
//     uint32_t tx_packet_sent_0 = qdma_reg_read(0, bar_idx, TX_PACKET_SENT_0);
//     uint32_t tx_packet_dropped_0 = qdma_reg_read(0, bar_idx, TX_PACKET_DROPPED_0);
//     uint32_t rx_packet_received_0 = qdma_reg_read(0, bar_idx, RX_PACKET_RECEIVED_0);
//     uint32_t rx_packet_dropped_0 = qdma_reg_read(0, bar_idx, RX_PACKET_DROPPED_0);
//     uint32_t rx_packet_w_error_0 = qdma_reg_read(0, bar_idx, RX_PACKET_W_ERROR_0);

//     uint32_t tx_packet_sent_1 = qdma_reg_read(0, bar_idx, TX_PACKET_SENT_1);
//     uint32_t tx_packet_dropped_1 = qdma_reg_read(0, bar_idx, TX_PACKET_DROPPED_1);
//     uint32_t rx_packet_received_1 = qdma_reg_read(0, bar_idx, RX_PACKET_RECEIVED_1);
//     uint32_t rx_packet_dropped_1 = qdma_reg_read(0, bar_idx, RX_PACKET_DROPPED_1);
//     uint32_t rx_packet_w_error_1 = qdma_reg_read(0, bar_idx, RX_PACKET_W_ERROR_1);

//     if (tx_packet_sent_0 > 0)
//         Log_info("CMAC 0 Tx packets sent %ld", tx_packet_sent_0);
//     if (tx_packet_dropped_0 > 0)
//         Log_info("CMAC 0 Tx packets dropped %ld", tx_packet_dropped_0);
//     if (rx_packet_received_0 > 0)
//         Log_info("CMAC 0 Rx packets received %ld", rx_packet_received_0);
//     if (rx_packet_dropped_0 > 0)
//         Log_info("CMAC 0 Rx packets dropped %ld", rx_packet_dropped_0);
//     if (rx_packet_w_error_0 > 0)
//         Log_info("CMAC 0 Rx packets with error %ld", rx_packet_w_error_0);

//     if (tx_packet_sent_1 > 0)
//         Log_info("CMAC 1 Tx packets sent %ld", tx_packet_sent_1);
//     if (tx_packet_dropped_1 > 0)
//         Log_info("CMAC 1 Tx packets dropped %ld", tx_packet_dropped_1);
//     if (rx_packet_received_1 > 0)
//         Log_info("CMAC 1 Rx packets received %ld", rx_packet_received_1);
//     if (rx_packet_dropped_1 > 0)
//         Log_info("CMAC 1 Rx packets dropped %ld", rx_packet_dropped_1);
//     if (rx_packet_w_error_1 > 0)
//         Log_info("CMAC 1 Rx packets with error %ld", rx_packet_w_error_1);
// }

// void DpdkTransport::qdma_port_info::qdma_reg_write(int port_id, uint32_t bar,
//                                                 uint32_t offset, uint32_t value) {
//     qdma_pci_write_reg(&rte_eth_devices[port_id], bar, offset, value);
// }

// uint32_t DpdkTransport::qdma_port_info::qdma_reg_read(int port_id, uint32_t bar,
//                                                    uint32_t offset) {
//     return qdma_pci_read_reg(&rte_eth_devices[port_id], bar, offset);
// }

void DpdkTransport::install_flow_rule(size_t phy_port, size_t qp_id, uint32_t ipv4_addr,uint16_t udp_port){
    
  bool installed = false;

//   const int ntuple_filter_supported =
//       rte_eth_dev_filter_supported(phy_port, RTE_ETH_FILTER_NTUPLE);

//   const int fdir_filter_supported =
//       rte_eth_dev_filter_supported(phy_port, RTE_ETH_FILTER_FDIR);

//   if (ntuple_filter_supported != 0 && fdir_filter_supported != 0) {
//     Log_warn("No flow steering supported by NIC. Apps likely won't work.\n");
//     return;
//   }

//   // Try the simplest filter first. I couldn't get FILTER_FDIR to work with
//   // ixgbe, although it technically supports flow director.
//   if (ntuple_filter_supported == 0) {
//     struct rte_eth_ntuple_filter ntuple;
//     memset(&ntuple, 0, sizeof(ntuple));
//     ntuple.flags = RTE_5TUPLE_FLAGS;
//     ntuple.dst_port = rte_cpu_to_be_16(udp_port);
//     ntuple.dst_port_mask = UINT16_MAX;
//     ntuple.dst_ip = rte_cpu_to_be_32(ipv4_addr);
//     ntuple.dst_ip_mask = UINT32_MAX;
//     ntuple.proto = IPPROTO_UDP;
//     ntuple.proto_mask = UINT8_MAX;
//     ntuple.priority = 1;
//     ntuple.queue = qp_id;

//     int ret = rte_eth_dev_filter_ctrl(phy_port, RTE_ETH_FILTER_NTUPLE,
//                                       RTE_ETH_FILTER_ADD, &ntuple);
//     if (ret != 0) {
//       Log_warn("Failed to add ntuple filter. This could be survivable.\n");
//     } else {
//       Log_warn("Installed ntuple flow rule. Queue %zu, RX UDP port = %u.\n",
//                 qp_id, udp_port);
//     }
//     installed = (ret == 0);
//   }

//   if (!installed && fdir_filter_supported == 0) {
//     // Use fdir filter for i40e (5-tuple not supported)
//     rte_eth_fdir_filter filter;
//     memset(&filter, 0, sizeof(filter));
//     filter.soft_id = qp_id;
//     filter.input.flow_type = RTE_ETH_FLOW_NONFRAG_IPV4_UDP;
//     filter.input.flow.udp4_flow.dst_port = rte_cpu_to_be_16(udp_port);
//     filter.input.flow.udp4_flow.ip.dst_ip = rte_cpu_to_be_32(ipv4_addr);
//     filter.action.rx_queue = qp_id;
//     filter.action.behavior = RTE_ETH_FDIR_ACCEPT;
//     filter.action.report_status = RTE_ETH_FDIR_NO_REPORT_STATUS;

//     int ret = rte_eth_dev_filter_ctrl(phy_port, RTE_ETH_FILTER_FDIR,
//                                       RTE_ETH_FILTER_ADD, &filter);
//     rt_assert(ret == 0, "Failed to add fdir flow rule: ", strerror(-1 * ret));

//     Log_warn("Installed flow-director rule. Queue %zu, RX UDP port = %u.\n",
//               qp_id, udp_port);
//   }
}
}
 }