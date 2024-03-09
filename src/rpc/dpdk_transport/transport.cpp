#include <cstdint>
#include "transport.hpp"
#include <rte_ring.h>
#include <rte_ring_core.h>
#include "utils.hpp"
#define DPDK_RX_DESC_SIZE 1024
#define DPDK_TX_DESC_SIZE 1024

#define DPDK_NUM_MBUFS 8192
#define DPDK_MBUF_CACHE_SIZE 250

#define MAX_PATTERN_NUM 3
#define MAX_ACTION_NUM 2
#define DPDK_RX_WRITEBACK_THRESH 64

namespace rrr
{

    const uint8_t RPC[97] = {0x09,                                           // PKT TYPE RR
                             0x54, 0x00, 0x00, 0x00,                         // Request Size 84
                             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Future ID
                             0x03, 0x00, 0x00, 0x10,                         // RPC_ID

                             0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // String size
                             0x6e, 0x77, 0x6c, 0x72, 0x62, 0x62, 0x6d, 0x71, // String
                             0x62, 0x68, 0x63, 0x64, 0x61, 0x72, 0x7a, 0x6f,
                             0x77, 0x6b, 0x6b, 0x79, 0x68, 0x69, 0x64, 0x64,
                             0x71, 0x73, 0x63, 0x64, 0x78, 0x72, 0x6a, 0x6d,
                             0x6f, 0x77, 0x66, 0x72, 0x78, 0x73, 0x6a, 0x79,
                             0x62, 0x6c, 0x64, 0x62, 0x65, 0x66, 0x73, 0x61,
                             0x72, 0x63, 0x62, 0x79, 0x6e, 0x65, 0x63, 0x64,
                             0x79, 0x67, 0x67, 0x78, 0x78, 0x70, 0x6b, 0x6c,
                             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    // Singleton tranport_layer
    DpdkTransport *DpdkTransport::transport_l = nullptr;
    std::unordered_map<uint64_t, TransportConnection *> DpdkTransport::out_connections;

    std::unordered_map<uint64_t, rte_ring *> DpdkTransport::in_rings;

    RPCConfig * DpdkTransport::config_;

    uint16_t rx_queue_ = 1, tx_queue_ = 1;
    struct rte_mempool**  DpdkTransport::tx_mbuf_pool;
    struct rte_mempool**  DpdkTransport::rx_mbuf_pool;

    // Session Management rings for each thread;
    //    (Single consumer multiple producer)
    struct rte_ring ** DpdkTransport::sm_rings;

    Counter DpdkTransport::u_port_counter(9000);
    Counter DpdkTransport::conn_counter(0);
    rrr::SpinLock pc_l;
    //  std::map<uint16_t,rrr::Connection*> connections_;
    SpinLock DpdkTransport::conn_th_lock;
    SpinLock DpdkTransport::init_lock;
    
    rrr::UDPServer* DpdkTransport::us_server=nullptr;
    std::unordered_map<i32, USHWrapper*> DpdkTransport::handlers;

    
    Counter DpdkTransport::next_thread_;

    std::unordered_map<std::string, NetAddress> DpdkTransport::src_addr_;
    std::unordered_map<std::string, NetAddress> DpdkTransport::dest_addr_;
    //std::unordered_map<i32, NetAddress> DpdkTransport::dest_addr_;
    
    SpinLock DpdkTransport::sm_queue_l;
    std::queue<Marshal *> DpdkTransport::sm_queue;

    void DpdkTransport::init(RPCConfig *config)
    {
        // src_addr_ = new NetAddress();
        init_lock.lock();
        if (!initiated)
        {
            config_ = config;
            addr_config(config->host_name_, config->get_net_info());

            RPCConfig::CpuInfo cpu_info = config->get_cpu_info();
            const char *argv_str = config->get_dpdk_options();

            num_threads_ = config->num_threads_;
            
            std::bitset<128> affinity_mask;
            for (int i = config->core_affinity_mask_[0]; i <= config->core_affinity_mask_[1]; i++)
                affinity_mask.set(i);
            
          //   std::thread main_thread([this, argv_str](){
            this->init_dpdk_main_thread(argv_str);
        //});

            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            int core_id;
            for (core_id = 0; core_id < affinity_mask.size(); core_id++)
            {
                if (affinity_mask.test(core_id))
                {
                    // LOG_DEBUG("Setting cpu affinity to cpu: %d for thread id %s-%d",core_id,stringify(type_).c_str(),thread_id_);
                    CPU_SET(core_id, &cpuset);
                }
            }

            int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
            assert((core_id <= num_cores));
        }
        init_lock.unlock();
    }

    void DpdkTransport::create_transport(RPCConfig *config)
    {
        if (transport_l == nullptr)
        {
            transport_l = new DpdkTransport;
        }
        transport_l->init(config);
    }
    DpdkTransport *DpdkTransport::get_transport()
    {
        verify(transport_l != nullptr);
        return transport_l;
    }
    void DpdkTransport::init_dpdk_main_thread(const char *argv_str)
    {
        bool in_numa_node = false;
        RPCConfig *conf = RPCConfig::get_config();
        // #ifdef RPC_STATISTICS
        // rs = new ring_stat();
        // rs->num_sample=0;
        // rs->free_c=0;
        // rs->used_c=0;
        // #endif
        while (1)
        {
            if (sched_getcpu() >= (conf->cpu_info_.numa) * (conf->cpu_info_.core_per_numa) || sched_getcpu() <= (conf->cpu_info_.numa + 1) * (conf->cpu_info_.core_per_numa))
            {
                break;
            }
            else
            {
                Log_warn("Waiting for scheduled on right node");
                sleep(1);
            }
        }
        std::vector<const char *> dpdk_argv;
        char *tmp_arg = const_cast<char *>(argv_str);
        const char *arg_tok = strtok(tmp_arg, " ");
        while (arg_tok != NULL)
        {
            dpdk_argv.push_back(arg_tok);
            arg_tok = strtok(NULL, " ");
        }
        int argc = dpdk_argv.size();
        char **argv = const_cast<char **>(dpdk_argv.data());

        int ret = rte_eal_init(argc, argv);
        if (ret < 0)
            rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

        port_num_ = rte_eth_dev_count_avail();
        if (port_num_ < 1)
            rte_exit(EXIT_FAILURE, "Error with insufficient number of ports\n");

        sm_rings = new struct rte_ring *[num_threads_];

        tx_queue_ = num_threads_;
        rx_queue_ = num_threads_;

        tx_mbuf_pool = new struct rte_mempool *[num_threads_];
        for (int pool_idx = 0; pool_idx < num_threads_; pool_idx++)
        {
            char pool_name[1024];
            sprintf(pool_name, "sRPC_TX_MBUF_POOL_%d", pool_idx);
            tx_mbuf_pool[pool_idx] = rte_pktmbuf_pool_create(pool_name, 32 * DPDK_NUM_MBUFS - 1,
                                                             DPDK_MBUF_CACHE_SIZE, 0,
                                                             RTE_MBUF_DEFAULT_BUF_SIZE,
                                                             rte_socket_id());
            if (tx_mbuf_pool[pool_idx] == NULL)
                rte_exit(EXIT_FAILURE, "Cannot create tx mbuf pool %d\n", pool_idx);
        }

        rx_mbuf_pool = new struct rte_mempool *[num_threads_];
        for (int pool_idx = 0; pool_idx < num_threads_; pool_idx++)
        {
            char pool_name[1024];
            sprintf(pool_name, "sRPC_RX_MBUF_POOL_%d", pool_idx);
            rx_mbuf_pool[pool_idx] = rte_pktmbuf_pool_create(pool_name, DPDK_NUM_MBUFS,
                                                             DPDK_MBUF_CACHE_SIZE, 0,
                                                             RTE_MBUF_DEFAULT_BUF_SIZE,
                                                             rte_socket_id());
            if (rx_mbuf_pool[pool_idx] == NULL)
                rte_exit(EXIT_FAILURE, "Cannot create rx mbuf pool %d\n", pool_idx);
        }

        /* Will initialize buffers in port_init function */
        thread_ctx_arr = new d_thread_ctx *[num_threads_];

        for (int i = 0; i < num_threads_; i++)
        {
            thread_ctx_arr[i] = new d_thread_ctx();
        }

        uint16_t portid;
        RTE_ETH_FOREACH_DEV(portid)
        {

            if (port_init(portid) != 0)
                rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n",
                         portid);
        }

        Log_info("DPDK num threads %d", num_threads_);

        uint16_t total_lcores = rte_lcore_count();

        Log_info("Total Cores available: %d", total_lcores);
        uint16_t rx_lcore_lim = num_threads_;

        uint8_t numa_id = config_->get_cpu_info().numa;
        // Add core per numa so that threads are scheduled on rigt lcores
        uint16_t lcore;
        initiated = true;
        rx_lcore_lim += numa_id * RPCConfig::get_config()->cpu_info_.core_per_numa;
        Log_info("thread_core limit: %d, my core_id %d ", rx_lcore_lim + 1, sched_getcpu());
        for (lcore = numa_id * RPCConfig::get_config()->cpu_info_.core_per_numa + 1; lcore < rx_lcore_lim + 1; lcore++)
        {
            int retval = rte_eal_remote_launch(ev_loop, thread_ctx_arr[lcore % num_threads_], lcore);
            if (retval < 0)
                rte_exit(EXIT_FAILURE, "Couldn't launch core %d\n", lcore % total_lcores);
        }
        initiated = true;
    }

    void DpdkTransport::addr_config(std::string host_name,
                                    std::vector<RPCConfig::NetworkInfo> net_info)
    {
        Log_info("Setting up network info....");
        for (auto &net : net_info)
        {
            std::unordered_map<std::string, NetAddress> *addr;
            if (host_name == net.name)
            {
                addr = &src_addr_;
                Log_info("Configuring local address %d, %s", net.id, ipv4_to_string(net.ip));
            }
            else
                addr = &dest_addr_;

            /* if (net.type == host_type) */
            /*     addr = &src_addr_; */
            /* else */
            /*     addr = &dest_addr_; */

            auto it = addr->find(host_name);
           // Log_info("Adding a host with name %s : info :\n %s", net.name.c_str(), net.to_string().c_str());
            verify(it == addr->end());

            addr->emplace(std::piecewise_construct,
                          std::forward_as_tuple(net.name),
                          std::forward_as_tuple(net.mac,
                                                net.ip,
                                                net.port));
        }
    }

    int DpdkTransport::port_init(uint16_t port_id)
    {
        RPCConfig *conf = rrr::RPCConfig::get_config();
        struct rte_eth_conf port_conf;
        uint16_t nb_rxd = DPDK_RX_DESC_SIZE;
        uint16_t nb_txd = DPDK_TX_DESC_SIZE;
        int retval;
        uint16_t q;
        struct rte_eth_dev_info dev_info;
        struct rte_eth_txconf txconf;
        struct rte_eth_rxconf rxconf;

        if (!rte_eth_dev_is_valid_port(port_id))
            return -1;

        retval = rte_eth_dev_info_get(port_id, &dev_info);
        if (retval != 0)
        {
            Log_error("Error during getting device (port %u) info: %s",
                      port_id, strerror(-retval));
            return retval;
        }
        isolate(port_id);

        memset(&port_conf, 0x0, sizeof(struct rte_eth_conf));
        memset(&txconf, 0x0, sizeof(struct rte_eth_txconf));
        memset(&rxconf, 0x0, sizeof(struct rte_eth_rxconf));
        port_conf = {
            .txmode = {
                .offloads =
                    DEV_TX_OFFLOAD_VLAN_INSERT |
                    DEV_TX_OFFLOAD_IPV4_CKSUM |
                    DEV_TX_OFFLOAD_UDP_CKSUM |
                    DEV_TX_OFFLOAD_TCP_CKSUM |
                    DEV_TX_OFFLOAD_SCTP_CKSUM |
                    DEV_TX_OFFLOAD_TCP_TSO,
            },
        };

        port_conf.txmode.offloads &= dev_info.tx_offload_capa;
        memcpy((void *)(&rxconf), (void *)&(dev_info.default_rxconf), sizeof(struct rte_eth_rxconf));
        rxconf.offloads = port_conf.rxmode.offloads;
        /**Configure nic port with offloads like CKSUM/SEGMENTATION and other features*/
        retval = rte_eth_dev_configure(port_id, rx_queue_, tx_queue_, &port_conf);

        if (retval != 0)
        {
            Log_error("Error during device configuration (port %u) info: %s",
                      port_id, strerror(-retval));
            return retval;
        }

        retval = rte_eth_dev_adjust_nb_rx_tx_desc(port_id, &nb_rxd, &nb_txd);
        if (retval != 0)
        {
            Log_error("Error during setting number of rx/tx descriptor (port %u) info: %s",
                      port_id, strerror(-retval));
            return retval;
        }

        rxconf.rx_thresh.wthresh = DPDK_RX_WRITEBACK_THRESH;
        // Setup rx_queues each thread will have its own mbuf pool to avoid
        // synchronisation while allocating space for packets or rings
        for (q = 0; q < rx_queue_; q++)
        {
            retval = rte_eth_rx_queue_setup(port_id, q, nb_rxd,
                                            rte_eth_dev_socket_id(port_id),
                                            &rxconf, rx_mbuf_pool[q]);
            if (retval < 0)
            {
                Log_error("Error during rx queue %d setup (port %u) info: %s",
                          q, port_id, strerror(-retval));
                return retval;
            }
        }
        // Setting tx_queue for each thread;
        for (q = 0; q < tx_queue_; q++)
        {
            retval = rte_eth_tx_queue_setup(port_id, q, nb_txd,
                                            rte_eth_dev_socket_id(port_id),
                                            &txconf);
            if (retval < 0)
            {
                Log_error("Error during tx queue %d setup (port %u) info: %s",
                          q, port_id, strerror(-retval));
                return retval;
            }
        }
        // start the port
        retval = rte_eth_dev_start(port_id);
        if (retval < 0)
        {
            Log_error("Error during starting device (port %u) info: %s",
                      port_id, strerror(-retval));
            return retval;
        }

        for (int i = 0; i < num_threads_; i++)
        {

            LOG_DEBUG("Create rx thread %d info on port %d and queue %d",
                      i, port_id, i);
            thread_ctx_arr[i]->init(this, i, port_id, i, RPCConfig::get_config()->burst_size);
            thread_ctx_arr[i]->mem_pool = rx_mbuf_pool[i];
            char sm_ring_name[128];
            sprintf(sm_ring_name, "sRPC_SMRING_CTX_%d", i);
            // SM ring doesn't need size;
            thread_ctx_arr[i]->sm_ring = rte_ring_create(sm_ring_name,
                                                         (conf->rte_ring_size) / 32,
                                                         rte_socket_id(),
                                                         RING_F_SC_DEQ | RING_F_MP_HTS_ENQ);
            sm_rings[i] = thread_ctx_arr[i]->sm_ring;
            verify(sm_rings[i] != nullptr);
        }

        install_flow_rule(port_id);
        return 0;
    }

    int DpdkTransport::port_close(uint16_t port_id)
    {

        rte_eth_dev_stop(port_id);
        // rte_pmd_qdma_dev_close(port_id);
        return 0;
    }

    int DpdkTransport::port_reset(uint16_t port_id)
    {

        int retval = port_close(port_id);
        if (retval < 0)
        {
            Log_error("Error: Failed to close device for port: %d", port_id);
            return retval;
        }

        retval = rte_eth_dev_reset(port_id);
        if (retval < 0)
        {
            Log_error("Error: Failed to reset device for port: %d", port_id);
            return -1;
        }

        retval = port_init(port_id);
        if (retval < 0)
        {
            Log_error("Error: Failed to initialize device for port %d", port_id);
            return -1;
        }

        return 0;
    }

    void DpdkTransport::shutdown()
    {

        rte_eal_mp_wait_lcore();
        Log_info("All DPDK threads stopped");
        for (int port_id = 0; port_id < port_num_; port_id++)
        {

            rte_eth_dev_stop(port_id);
            rte_eth_dev_close(port_id);
        }
        rte_hash_free(conn_table);
        struct rte_flow_error err;
        rte_flow_flush(0, &err);

        int ret = rte_eal_cleanup();
        if (ret == 0)
            Log_info("DPDK Context finished");
        else
            Log_error("DPDK CLEANUP FAILED !!");
    }

    void DpdkTransport::trigger_shutdown()
    {

        for (int i = 0; i < num_threads_; i++)
        {
            thread_ctx_arr[i]->shutdown = true;
        }
        force_quit = true;
    }

    /* void DpdkTransport::register_resp_callback(Workload* app) { */
    /*     response_handler = [app](uint8_t* data, int data_len, int id) -> int { */
    /*         return app->process_workload(data, data_len, id); */
    /*     }; */
    /* } */

    void DpdkTransport::reg_us_handler(i32 id, std::function<void(Request<TransportMarshal>*, ServerConnection*)> func){
        handlers[id] = new USHWrapper(id, func);
    }

    void d_thread_ctx::init(DpdkTransport *th, int th_id, int p_id,
                            int q_id, int burst_size)
    {
        t_layer = th;
        thread_id = th_id;
        port_id = p_id;
        queue_id = q_id;
        max_size = burst_size;
        tx_bufs = new struct rte_mbuf *[burst_size];
        rx_bufs = new struct rte_mbuf *[burst_size];
        for (int i = 0; i < burst_size; i++)
        {
            rx_bufs[i] = (rte_mbuf *)rte_malloc("deque_objs", sizeof(struct rte_mbuf), 0);
        }
    }

    int d_thread_ctx::buf_alloc(struct rte_mempool *mbuf_pool)
    {
        int retval = rte_pktmbuf_alloc_bulk(mbuf_pool, tx_bufs, max_size);
        return retval;
    }
    int DpdkTransport::isolate(uint8_t phy_port)
    {
        struct rte_flow_error *error = (struct rte_flow_error *)malloc(sizeof(struct rte_flow_error));
        int ret = rte_flow_isolate(phy_port, 1, error);
        if (ret < 0)
            Log_error("Failed to enable flow isolation for port %d\n, message: %s", phy_port, error->message);
        else
            Log_info("Flow isolation enabled for port %d\n", phy_port);
        return ret;
    }

    void DpdkTransport::install_flow_rule(size_t phy_port)
    {

        struct rte_flow_attr attr;
        struct rte_flow_item pattern[MAX_PATTERN_NUM];
        struct rte_flow_action action[MAX_ACTION_NUM];
        struct rte_flow *flow = NULL;
        struct rte_flow_action_queue queue = {.index = 0};
        struct rte_flow_item_ipv4 ip_spec;
        struct rte_flow_item_ipv4 ip_mask;
        struct rte_flow_item_eth eth_spec;
        struct rte_flow_item_eth eth_mask;
        struct rte_flow_item_udp udp_spec;
        struct rte_flow_item_udp udp_mask;

        struct rte_flow_error error;
        int res;

        memset(pattern, 0, sizeof(pattern));
        memset(action, 0, sizeof(action));

        /*
         * set the rule attribute.
         * in this case only ingress packets will be checked.
         */
        memset(&attr, 0, sizeof(struct rte_flow_attr));
        attr.priority = 1;
        attr.ingress = 1;

        /*
         * create the action sequence.
         * one action only,  move packet to queue
         */
        action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
        action[0].conf = &queue;
        action[1].type = RTE_FLOW_ACTION_TYPE_END;

        /*
         * set the first level of the pattern (ETH).
         * since in this example we just want to get the
         * ipv4 we set this level to allow all.
         */
        pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
        memset(&eth_spec, 0, sizeof(struct rte_flow_item_eth));
        memset(&eth_mask, 0, sizeof(struct rte_flow_item_eth));
        eth_spec.type = RTE_BE16(RTE_ETHER_TYPE_IPV4);
        eth_mask.type = RTE_BE16(0xffff);
        pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
        pattern[0].spec = &eth_spec;
        pattern[0].mask = &eth_mask;

        /*
         * setting the second level of the pattern (IP).
         * in this example this is the level we care about
         * so we set it according to the parameters.
         */
        memset(&ip_spec, 0, sizeof(struct rte_flow_item_ipv4));
        memset(&ip_mask, 0, sizeof(struct rte_flow_item_ipv4));
        ip_spec.hdr.dst_addr = src_addr_[config_->host_name_].ip;

        ip_mask.hdr.dst_addr = RTE_BE32(0xffffffff);
        // ip_spec.hdr.src_addr = 0;
        // ip_mask.hdr.src_addr = RTE_BE32(0);

        Log_info("IP Address to be queued %s", ipv4_to_string(ip_spec.hdr.dst_addr).c_str());

        // ip_mask.hdr.dst_addr =

        pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
        pattern[1].spec = &ip_spec;
        pattern[1].mask = &ip_mask;

        memset(&udp_mask, 0, sizeof(struct rte_flow_item_udp));
        memset(&udp_spec, 0, sizeof(struct rte_flow_item_udp));
        udp_spec.hdr.dst_port = RTE_BE16(8501);
        udp_mask.hdr.dst_port = RTE_BE16(0xffff);
        /* TODO: Change this to support leader change */
        udp_spec.hdr.src_port = 0;
        udp_mask.hdr.src_port = RTE_BE16(0);
        udp_mask.hdr.dgram_len = RTE_BE16(0);
        pattern[2].type = RTE_FLOW_ITEM_TYPE_UDP;
        pattern[2].spec = &udp_spec;
        pattern[2].mask = &udp_mask;
        /* the final level must be always type end */
        pattern[2].type = RTE_FLOW_ITEM_TYPE_END;
        res = rte_flow_validate(phy_port, &attr, pattern, action, &error);

        if (!res)
        {
            flow = rte_flow_create(phy_port, &attr, pattern, action, &error);
            Log_info("Flow Rule Added for IP Address : %s", ipv4_to_string(src_addr_[config_->host_name_].ip).c_str());
            // int ret = rte_flow_isolate(phy_port, 1,&error);

            //  if (!ret)
            //     Log_error("Failed to enable flow isolation for port %d\n, message: %s", phy_port,error.message);
            //  else
            //     Log_info("Flow isolation enabled for port %d\n", phy_port);
        }
        else
        {
            Log_error("Failed to create flow rule: %s\n", error.message);
        }
    }


}