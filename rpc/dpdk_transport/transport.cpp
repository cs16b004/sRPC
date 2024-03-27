#include <cstdint>
#include "transport.hpp"
#include <rte_ring.h>
#include <rte_ring_core.h>
#include "utils.hpp"

#define _GNU_SOURCE
#include <utmpx.h>

#define DPDK_RX_DESC_SIZE 1024
#define DPDK_TX_DESC_SIZE 1024

#define DPDK_NUM_MBUFS 8192
#define DPDK_MBUF_CACHE_SIZE 250
#define DPDK_RX_BURST_SIZE 64
#define DPDK_TX_BURST_SIZE 1
#define MAX_PATTERN_NUM 3
#define MAX_ACTION_NUM 2
#define DPDK_RX_WRITEBACK_THRESH 64

namespace rrr
{
    struct Request;
    const uint8_t padd[64] = {
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
    };

    // Singleton tranport_layer
    DpdkTransport *DpdkTransport::transport_l = nullptr;

    // Used in in send method to create a packet
    std::string DpdkTransport::getMacFromIp(std::string ip)
    {
        for (rrr::Config::NetworkInfo it : config_->get_net_info())
        {
            if (it.ip == ip)
            {
                // Log_debug("Found the Mac for IP: %s, MAC: %s",ip.c_str(),it.mac.c_str());
                return (it.mac);
            }
        }
        return "02:de:ad:be:ef:60";
    }

    inline uint16_t process_sm_requests(rte_ring *sm_ring, rte_mempool *mempool)
    {

        return 0;
    }
    void DpdkTransport::print_packet(rte_mbuf *pkt)
    {
        // Extract Ethernet header
        struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
        uint8_t *pkt_ptr = rte_pktmbuf_mtod(pkt, uint8_t *);
        struct rte_ether_addr temp = eth_hdr->src_addr;
        eth_hdr->src_addr = eth_hdr->dst_addr;
        eth_hdr->dst_addr = temp;

        struct rte_ipv4_hdr *ip_hdr = reinterpret_cast<rte_ipv4_hdr *>(pkt_ptr + IPV4_OFFSET);
        struct rte_udp_hdr *udp_hdr = reinterpret_cast<rte_udp_hdr *>(pkt_ptr + UDP_OFFSET);

        Log_debug("src: IP %s, size: %d", ipv4_to_string(ip_hdr->src_addr).c_str(), ntohs(udp_hdr->dgram_len));

        char *req = new char[1024];
        uint8_t *pkt_data = rte_pktmbuf_mtod(pkt, uint8_t *);
        int j = 0;
        int maxs = ntohs(udp_hdr->dgram_len) < 200 ? ntohs(udp_hdr->dgram_len) : 200;
        for (int i = (sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr));
             i < maxs; i++)
        {

            sprintf(req + j, "%02x ", pkt_data[i]);
            j += 3;
            if (j % 25 == 0)
            {
                req[j] = '\n';
                j++;
            }
        }
        req[j] = 0;
        Log_info("Packet data: %s", req);
    }
    uint64_t DpdkTransport::accept(const char *addr_str)
    {
        Config *conf = rrr::Config::get_config();
        while (!initiated)
        {
            Log_debug("Wating for intialization");
            sleep(1);
            ;
        }

        std::string addr(addr_str);
        size_t idx = addr.find(":");
        if (idx == std::string::npos)
        {
            Log_error("rrr::Transport: bad accept address: %s", addr);
            return 0;
        }
        std::string server_ip = addr.substr(0, idx);
        uint16_t port = atoi(addr.substr(idx + 1).c_str());

        // UDPConnection *conn = new UDPConnection(*s_addr);
        // Log_debug("Accept request %s",addr_str);
        TransportConnection *oconn = new TransportConnection();
        int pipefd[2];
        verify(pipe(pipefd) == 0);
        oconn->src_addr = src_addr_[config_->host_name_];
        oconn->out_addr = NetAddress(getMacFromIp(server_ip).c_str(), server_ip.c_str(), port);
        oconn->in_fd_ = pipefd[0];
        oconn->wfd = pipefd[1];
        oconn->udp_port = src_addr_[config_->host_name_].port;
        addr = addr + "::" + std::to_string(oconn->udp_port);
        uint64_t conn_id = 0;
        // choose a connection based on round robin principle;
        conn_th_lock.lock();

        next_thread_ += 1;
        uint16_t chosen_tx_thread = (next_thread_ % (config_->num_tx_threads_));

        conn_counter++;

        // verify(thread_rx_info[chosen_rx_thread].conn_counter == thread_tx_info[chosen_tx_thread].conn_counter);

        conn_id = conn_id | oconn->out_addr.ip;
        conn_id = conn_id << 16;
        // server port in BE
        conn_id = conn_id | rte_cpu_to_be_16(port);
        // local host port in BE
        conn_id = conn_id << 16;
        conn_id = conn_id | rte_cpu_to_be_16(oconn->udp_port);
        if (out_connections.find(conn_id) != out_connections.end())
        {
            conn_th_lock.unlock();
            thread_tx_info[chosen_tx_thread]->conn_lock.unlock();
            return 0;
        }
        Log_info("Chosen threads for new conn: %d, tx-thread %d, next_thread_ %d", conn_id, chosen_tx_thread, next_thread_);

        oconn->conn_id = conn_id;
        out_connections[conn_id] = oconn;

        char buf_ring_name[128];
        sprintf(buf_ring_name, "sRPC_BUFRING_CONN_%d", conn_id);
        // bigger size queue;
        oconn->out_bufring = rte_ring_create(buf_ring_name,
                                             conf->rte_ring_size,
                                             rte_socket_id(),
                                             RING_F_SC_DEQ | RING_F_MP_HTS_ENQ);

        oconn->buf_alloc(tx_mbuf_pool[chosen_tx_thread], conf->buffer_len);
        oconn->make_headers();
        rte_ring_sp_enqueue(tx_sm_rings[chosen_tx_thread], (void *)oconn);

        conn_th_lock.unlock();
        // this->connections_[conn_id] = conn;

        int wait = 0;

        uint8_t *pkt_ptr = rte_pktmbuf_mtod(oconn->out_msg_buffers[0], uint8_t *);
        memcpy(pkt_ptr + data_offset, con_ack, 64);
        oconn->out_msg_buffers[0]->pkt_len = 64 + data_offset;
        oconn->out_msg_buffers[0]->data_len = 64 + data_offset;
        rte_ring_sp_enqueue(oconn->out_bufring, (void *)oconn->out_msg_buffers[0]);
        wait = 0;

        sleep(1);
        oconn->connected_ = true;
        return conn_id;
    }

    uint64_t DpdkTransport::connect(const char *addr_str)
    {
        Config *conf = rrr::Config::get_config();
        while (!initiated)
        {
            Log_debug("Waiting for initialization");
            sleep(1);
            ;
        }
        std::string addr(addr_str);
        size_t idx = addr.find(":");
        if (idx == std::string::npos)
        {
            Log_error("rrr::Transport: bad connect address: %s", addr);
            return EINVAL;
        }
        std::string server_ip = addr.substr(0, idx);
        uint16_t port = atoi(addr.substr(idx + 1).c_str());

        // UDPConnection *conn = new UDPConnection(*s_addr);
        TransportConnection *oconn = new TransportConnection();
        int pipefd[2];
        verify(pipe(pipefd) == 0);
        Log_debug("Connecting to a server : %s", addr_str);
        // Log_debug("Mac: %s",getMacFromIp(server_ip).c_str());
        oconn->src_addr = src_addr_[config_->host_name_];
        oconn->out_addr = NetAddress(getMacFromIp(server_ip).c_str(), server_ip.c_str(), port);

        oconn->in_fd_ = pipefd[0];
        oconn->wfd = pipefd[1];

        uint64_t conn_id = 0;
        conn_th_lock.lock();

        next_thread_++;
        uint64_t chosen_tx_thread = next_thread_ % tx_threads_;
        oconn->udp_port = get_open_port();
        oconn->src_addr.port = oconn->udp_port;
        addr = addr + "::" + std::to_string(oconn->udp_port);

        conn_counter++;
        conn_id = conn_id | oconn->out_addr.ip; // server ip in BE
        conn_id = conn_id << 16;
        conn_id = conn_id | rte_cpu_to_be_16(port); // server port in BE
        conn_id = conn_id << 16;
        conn_id = conn_id | rte_cpu_to_be_16(oconn->udp_port); // local host port in BE
        oconn->conn_id = conn_id;
        Log_info("Chosen threads for new conn: %d is tx-thread %d, counter %d", conn_id, chosen_tx_thread, next_thread_);
        out_connections[conn_id] = oconn;
        char buf_ring_name[128];
        sprintf(buf_ring_name, "sRPC_BUFRING_CONN_%d", conn_id);
        // bigger size queue;
        oconn->out_bufring = rte_ring_create(buf_ring_name,
                                             conf->rte_ring_size,
                                             rte_socket_id(),
                                             RING_F_SC_DEQ | RING_F_MP_HTS_ENQ);

        oconn->buf_alloc(tx_mbuf_pool[chosen_tx_thread], conf->buffer_len);
        oconn->make_headers();
        verify(rte_ring_enqueue(tx_sm_rings[chosen_tx_thread], (void *)oconn) == 0);

        conn_th_lock.unlock();
        // this->connections_[conn_id] = conn;

        // First packet is SM packet
        uint8_t *pkt_ptr = rte_pktmbuf_mtod(oconn->out_msg_buffers[0], uint8_t *);
        memcpy(pkt_ptr + data_offset, con_req, 64);
        oconn->out_msg_buffers[0]->pkt_len = 64 + data_offset;
        oconn->out_msg_buffers[0]->data_len = 64 + data_offset;
        rte_ring_enqueue(oconn->out_bufring, (void *)oconn->out_msg_buffers[0]);
        int wait = 0;
        while (!oconn->connected_)
        {
            usleep(50 * 1000);
            wait++;
            if (wait > 20)
            {
                Log_warn("Stuck in Buffer Queue of rte_tx_thread_%d", chosen_tx_thread);
                wait = 0;
            }
        }
        Log_info("Connected to %s, fd r: %d, w: %d", addr.c_str(), oconn->in_fd_, oconn->wfd);

        return conn_id;
    }

    int DpdkTransport::dpdk_tx_loop(void *arg)
    {
        dpdk_thread_info *info = reinterpret_cast<dpdk_thread_info *>(arg);
        DpdkTransport *dpdk_th = info->t_layer;

        Config *conf = Config::get_config();

        // Initialize sm_ring (multiproducer) Single consumer
        // put it in transport_layer global datastructure of rings
        // initalize data_structures locaaly which will be used in future;
        rte_ring *sm_queue_ = info->sm_ring;
        rte_mempool *mem_pool = info->mem_pool;

        TransportConnection **conn_arr = new TransportConnection *[8];

        unsigned int available = 0;
        unsigned int nb_sm_reqs_ = 0;
        unsigned int nb_pkts = 0;
        uint16_t ret = 0;
        uint16_t retry_count = 0;

        uint16_t queue_id = info->queue_id;
        uint16_t port_id = info->port_id;
        uint16_t buf_len = conf->buffer_len;
        uint16_t burst_size = conf->burst_size;
        rte_mbuf **my_buffers = info->buf;
        TransportConnection *current_conn;
        int i, j;
        uint64_t times = 0;
        Log_info("Entering TX thread %d at lcore %d", info->thread_id, rte_lcore_id());

        while (!info->shutdown)
        {
            times++;
            // likely the rign is empty
            if (unlikely(rte_ring_empty(sm_queue_) == 0))
            {
                nb_sm_reqs_ = rte_ring_sc_dequeue_burst(sm_queue_, (void **)conn_arr, 8, &available);
                for (i = 0; i < nb_sm_reqs_; i++)
                {
                    info->out_connections[conn_arr[i]->conn_id] = conn_arr[i]; // Put the connection in local conn_table
                    Log_debug("Added Connection %lld to tx_thread %d", conn_arr[i]->conn_id, info->thread_id);
                }
            }
            /*
                Dequeue each connection msg_buffers and transmit messages;

            */

            for (auto conn_entry : info->out_connections)
            {
                current_conn = conn_entry.second;
                if (current_conn->out_bufring == nullptr)
                    continue;
                nb_pkts = rte_ring_sc_dequeue_burst(current_conn->out_bufring, (void **)my_buffers, burst_size, &available);
                if (nb_pkts <= 0)
                    continue;
                ret = rte_eth_tx_burst(port_id, queue_id, my_buffers, nb_pkts);
                Log_debug("NB pkts %d, sent %d, conn_id %lld", nb_pkts, ret, conn_entry.first);
                if (unlikely(ret < 0))
                    rte_panic("Can't send burst\n");
                while (ret != nb_pkts)
                {
                    ret += rte_eth_tx_burst(port_id, queue_id, &my_buffers[ret], nb_pkts - ret);
                    retry_count++;
                    if (unlikely(retry_count == 1000000))
                    {
                        Log_warn("stuck in rte_eth_tx_burst in port %u queue %u", port_id, queue_id);
                        retry_count = 0;
                    }
                }
            }
        }
        Log_info("Exiting TX thread %d", info->thread_id);
        return 0;
    }
    void DpdkTransport::tx_loop_one(dpdk_thread_info *arg)
    {

        unsigned lcore_id = rte_lcore_id();
        std::vector<Marshal *> requests;
        dpdk_thread_info *tx_info = arg;
        tx_info->conn_lock.lock();
        for (auto const &entry : tx_info->out_connections)
        {

            verify(entry.second != nullptr);
            entry.second->outl.lock();
            if (!entry.second->out_messages.empty())
            {
                TransportMarshal *req = entry.second->out_messages.front();

                entry.second->out_messages.pop();
            }
            entry.second->outl.unlock();
        }
        tx_info->conn_lock.unlock();

        return;
    }
    int DpdkTransport::dpdk_rx_loop(void *arg)
    {
        auto rx_info = reinterpret_cast<dpdk_thread_info *>(arg);
        auto dpdk_th = rx_info->t_layer;
        unsigned lcore_id = rte_lcore_id();

        uint16_t port_id = rx_info->port_id;

        Log_info("Enter receive thread %d on core %d on port_id %d on queue %d",
                 rx_info->thread_id, lcore_id, port_id, rx_info->queue_id);

        while (!dpdk_th->force_quit)
        {
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

    void DpdkTransport::process_incoming_packets(dpdk_thread_info *rx_info)
    {
        /* Prefetch packets */
        for (int i = 0; (i < 3) && (i < rx_info->count); i++)
            rte_prefetch0(rte_pktmbuf_mtod(rx_info->buf[i], uint8_t *));

        for (int i = 0; i < rx_info->count; i++)
        {
            uint8_t *pkt_ptr = rte_pktmbuf_mtod((struct rte_mbuf *)rx_info->buf[i], uint8_t *);

            struct rte_ipv4_hdr *ip_hdr = reinterpret_cast<struct rte_ipv4_hdr *>(pkt_ptr + ip_hdr_offset);
            uint32_t src_ip = ip_hdr->src_addr;

            struct rte_udp_hdr *udp_hdr = reinterpret_cast<struct rte_udp_hdr *>(pkt_ptr + udp_hdr_offset);

            // uint16_t src_port = ntohs(udp_hdr->src_port);
            // uint16_t dest_port = ntohs(udp_hdr->dst_port);
            uint16_t pkt_size = ntohs(udp_hdr->dgram_len) - sizeof(rte_udp_hdr);
            // Log_debug("Packet matched for connection id : %d, size %d!!",src_port, pkt_size);
            uint64_t conn_id = 0;
            conn_id = src_ip;
            conn_id = conn_id << 16;
            // server port in BE
            conn_id = conn_id | (uint64_t)(udp_hdr->src_port);
            // local host port in BE
            conn_id = conn_id << 16;
            conn_id = conn_id | (uint64_t)(udp_hdr->dst_port);
#ifdef LOG_LEVEL_AS_DEBUG

            uint64_t conn_c = conn_id;
            uint16_t local_port = conn_c & (0xffff);
            conn_c = conn_c >> 16;
            uint16_t server_port = conn_c & (0xffff);
            conn_c = conn_c >> 16;
            uint32_t ser_ip = conn_c & (0xffffffff);

            Log_debug("Conn Id dissassemble : IP: %s, port: %d, local port: %d", ipv4_to_string(ser_ip).c_str(), ntohs(server_port), ntohs(local_port));
#endif
            uint8_t *data_ptr = pkt_ptr + data_offset;

            uint8_t pkt_type;
#ifdef RPC_MICRO_STATISTICS
            if (config_->host_name_ == "catskill")
            {
                uint64_t pkt_id;
                rte_memcpy((uint8_t *)&pkt_id, pkt_ptr + rx_info->buf[i]->data_len - sizeof(uint64_t), sizeof(uint64_t));

                struct timespec ts;
                timespec_get(&ts, TIME_UTC);
                r_ts_lock.lock();
                pkt_rx_ts[pkt_id] = ts;
                r_ts_lock.unlock();
                Log_debug("Received packet with id %lld. total size %d, id offset: %d, data offset %d",
                          pkt_id, rx_info->buf[i]->data_len, rx_info->buf[i]->data_len - sizeof(uint64_t), data_offset + 1);
            }
#endif

            mempcpy(&pkt_type, data_ptr, sizeof(uint8_t));
            data_ptr += sizeof(uint8_t);
            if (pkt_type == RR)
            {

                if (out_connections.find(conn_id) != out_connections.end())
                {
                    print_packet(rx_info->buf[i]);
#ifdef RPC_MICRO_STATISTICS
                    int n;
                    if (config_->host_name_ == "catskill")
                    {
                        n = write(out_connections[conn_id]->wfd, data_ptr, pkt_size + 8); // sizeof(pkt id)
                    }
                    else
                    {
                        n = write(out_connections[conn_id]->wfd, data_ptr, pkt_size);
                    }
#else
                    int n = write(out_connections[conn_id]->wfd, data_ptr, pkt_size-1);
#endif

                    if (n > 0)
                    {
                        Log_debug("%d bytes written to fd %d, read end %d",
                                  n, out_connections[conn_id]->wfd,
                                  out_connections[conn_id]->in_fd_);
                    }
                    if (n < 0)
                    {
                        perror("Message: ");
                    }
                }
                else
                {
                    Log_debug("Packet Dropped as connection not found");
                }
                // Log_info("Byres Written %d",n);
            }
            else if (pkt_type == SM)
            {
                Log_debug("Session Management Packet received pkt type 0x%2x", pkt_type);
                Marshal *sm_req = new Marshal();
                uint8_t req_type;

                mempcpy(&req_type, data_ptr, sizeof(uint8_t));
                // Log_debug("Req Type 0x%2x, src_addr : %s",req_type, src_addr.c_str());
                if (req_type == CON_ACK)
                {
                    if (out_connections.find(conn_id) !=
                        out_connections.end())
                    {
                        out_connections[conn_id]->connected_ = true;
                    }
                    else
                    {
                        Log_error("Connection not found connid: %d , thread_id %d", conn_id, rx_info->thread_id);
                    }
                }
                else
                {
                    //  Log_debug("Connection request from %s", ipv4_to_string(src_ip).c_str());
                    std::string src_addr = ipv4_to_string(src_ip) + ":" + std::to_string(ntohs(udp_hdr->src_port));
                    *(sm_req) << req_type;
                    *(sm_req) << src_addr;
                    rx_info->t_layer->sm_queue_l.lock();
                    rx_info->t_layer->sm_queue.push(sm_req);
                    rx_info->t_layer->sm_queue_l.unlock();
                }
            }
            else
            {
                Log_debug("Packet Type Not found");
            }
            int prefetch_idx = i + 3;
            if (prefetch_idx < rx_info->count)
                rte_prefetch0(rte_pktmbuf_mtod(rx_info->buf[prefetch_idx], uint8_t *));
        }

        for (int i = 0; i < rx_info->count; i++)
            rte_pktmbuf_free(rx_info->buf[i]);
    }
    uint16_t DpdkTransport::get_open_port()
    {

        pc_l.lock();
        uint16_t temp = u_port_counter;
        u_port_counter += 1;
        pc_l.unlock();
        return temp;
    }
    void DpdkTransport::init(Config *config)
    {
        // src_addr_ = new NetAddress();
        init_lock.lock();
        if (!initiated)
        {
            config_ = config;
            addr_config(config->host_name_, config->get_net_info());

            Config::CpuInfo cpu_info = config->get_cpu_info();
            const char *argv_str = config->get_dpdk_options();
            tx_threads_ = config->num_tx_threads_;
            rx_threads_ = config->num_rx_threads_;
            next_thread_ = 0;
            Log_info("Thread counter %d, num_rx_threads %d, tx_threads %d", next_thread_, rx_threads_, tx_threads_);
            std::bitset<128> affinity_mask;
            for (int i = config->core_affinity_mask_[0]; i <= config->core_affinity_mask_[1]; i++)
                affinity_mask.set(i);
            gettimeofday(&start_clock, NULL);
            main_thread = std::thread([this, argv_str]()
                                      { this->init_dpdk_main_thread(argv_str); });

            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            int core_id;
            for (core_id = 0; core_id < affinity_mask.size(); core_id++)
            {
                if (affinity_mask.test(core_id))
                {
                    // Log_debug("Setting cpu affinity to cpu: %d for thread id %s-%d",core_id,stringify(type_).c_str(),thread_id_);
                    CPU_SET(core_id, &cpuset);
                }
            }

            int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
            assert((core_id <= num_cores));

            int err = pthread_setaffinity_np(main_thread.native_handle(), sizeof(cpu_set_t), &cpuset);
            if (err < 0)
            {
                Log_error("Couldn't set affinity of thread DPDK_INIT_THREAD to core %d", core_id);
                return;
            }
        }
        init_lock.unlock();
    }

    void DpdkTransport::create_transport(Config *config)
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
        Config *conf = Config::get_config();
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

        rx_sm_rings = new struct rte_ring *[rx_threads_];
        tx_sm_rings = new struct rte_ring *[tx_threads_];

        tx_queue_ = tx_threads_;
        rx_queue_ = rx_threads_;

        tx_mbuf_pool = new struct rte_mempool *[tx_threads_];
        for (int pool_idx = 0; pool_idx < tx_threads_; pool_idx++)
        {
            char pool_name[1024];
            sprintf(pool_name, "sRPC_TX_MBUF_POOL_%d", pool_idx);
            tx_mbuf_pool[pool_idx] = rte_pktmbuf_pool_create(pool_name, 8 * DPDK_NUM_MBUFS - 1,
                                                             DPDK_MBUF_CACHE_SIZE, 0,
                                                             RTE_MBUF_DEFAULT_BUF_SIZE,
                                                             rte_socket_id());
            if (tx_mbuf_pool[pool_idx] == NULL)
                rte_exit(EXIT_FAILURE, "Cannot create tx mbuf pool %d\n", pool_idx);
        }

        rx_mbuf_pool = new struct rte_mempool *[rx_threads_];
        for (int pool_idx = 0; pool_idx < rx_threads_; pool_idx++)
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
        thread_rx_info = new dpdk_thread_info *[rx_threads_];
        thread_tx_info = new dpdk_thread_info *[tx_threads_];
        for (int i = 0; i < rx_threads_; i++)
        {
            thread_rx_info[i] = new dpdk_thread_info();
        }
        for (int i = 0; i < tx_threads_; i++)
        {
            thread_tx_info[i] = new dpdk_thread_info();
        }

        uint16_t portid;
        RTE_ETH_FOREACH_DEV(portid)
        {

            if (port_init(portid) != 0)
                rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n",
                         portid);
        }

        Log_info("DPDK tx threads %d, rx threads %d", tx_threads_, rx_threads_);

        uint16_t total_lcores = rte_lcore_count();

        Log_info("Total Cores available: %d", total_lcores);
        uint16_t rx_lcore_lim = rx_threads_;
        uint16_t tx_lcore_lim = rx_threads_ + tx_threads_;

        uint8_t numa_id = config_->get_cpu_info().numa;
        // Add core per numa so that threads are scheduled on rigt lcores
        uint16_t lcore;
        rx_lcore_lim += numa_id * Config::get_config()->cpu_info_.core_per_numa;
        tx_lcore_lim += numa_id * Config::get_config()->cpu_info_.core_per_numa;
        Log_info("rx_core limit: %d tx_core limit: %d, my core_id %d ", rx_lcore_lim + 1, tx_lcore_lim + 1, sched_getcpu());
        for (lcore = numa_id * Config::get_config()->cpu_info_.core_per_numa + 1; lcore < rx_lcore_lim + 1; lcore++)
        {

            int retval = rte_eal_remote_launch(dpdk_rx_loop, thread_rx_info[lcore % rx_threads_], lcore);
            if (retval < 0)
                rte_exit(EXIT_FAILURE, "Couldn't launch core %d\n", lcore % total_lcores);
        }

        for (lcore = rx_lcore_lim + 1; lcore < tx_lcore_lim + 1; lcore++)
        {

            int retval = rte_eal_remote_launch(dpdk_tx_loop, thread_tx_info[lcore % tx_threads_], lcore);
            if (retval < 0)
                rte_exit(EXIT_FAILURE, "Couldn't launch core %d\n", lcore % total_lcores);
        }
        initiated = true;
    }

    void DpdkTransport::addr_config(std::string host_name,
                                    std::vector<Config::NetworkInfo> net_info)
    {
        Log_info("Setting up network info....");
        for (auto &net : net_info)
        {
            std::map<std::string, NetAddress> *addr;
            if (host_name == net.name)
            {
                addr = &src_addr_;
                Log_info("Configuring local address %d, %s", net.id, net.ip.c_str());
            }
            else
                addr = &dest_addr_;

            /* if (net.type == host_type) */
            /*     addr = &src_addr_; */
            /* else */
            /*     addr = &dest_addr_; */

            auto it = addr->find(host_name);
            Log_info("Adding a host with name %s : info :\n %s", net.name.c_str(), net.to_string().c_str());
            verify(it == addr->end());

            addr->emplace(std::piecewise_construct,
                          std::forward_as_tuple(net.name),
                          std::forward_as_tuple(net.mac.c_str(),
                                                net.ip.c_str(),
                                                net.port));
        }
    }

    int DpdkTransport::port_init(uint16_t port_id)
    {
        Config *conf = rrr::Config::get_config();
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

        for (int i = 0; i < rx_queue_; i++)
        {

            Log_debug("Create rx thread %d info on port %d and queue %d",
                      i, port_id, i);
            thread_rx_info[i]->init(this, i, port_id, i, Config::get_config()->burst_size);
            thread_rx_info[i]->mem_pool = rx_mbuf_pool[i];
            char sm_ring_name[128];
            sprintf(sm_ring_name, "sRPC_SMRING_THREAD_RX_%d", i);
            // SM ring doesn't need size;
            thread_rx_info[i]->sm_ring = rte_ring_create(sm_ring_name,
                                                         (conf->rte_ring_size) / 32,
                                                         rte_socket_id(),
                                                         RING_F_SC_DEQ | RING_F_MP_HTS_ENQ);
            rx_sm_rings[i] = thread_rx_info[i]->sm_ring;
            verify(rx_sm_rings[i] != nullptr);
        }

        for (int i = 0; i < tx_queue_; i++)
        {
            Log_debug("Create tx thread %d info on port %d and queue %d",
                      i, port_id, i);
            thread_tx_info[i]->init(this, i, port_id, i, Config::get_config()->burst_size);
            thread_tx_info[i]->mem_pool = tx_mbuf_pool[i];
            char sm_ring_name[128];
            sprintf(sm_ring_name, "sRPC_SMRING_THREAD_TX_%d", i);
            // SM ring doesn't need size;
            thread_tx_info[i]->sm_ring = rte_ring_create(sm_ring_name, (conf->rte_ring_size) / 32, rte_socket_id(), RING_F_SC_DEQ | RING_F_MP_HTS_ENQ);
            tx_sm_rings[i] = thread_tx_info[i]->sm_ring;
            verify(tx_sm_rings[i] != nullptr);
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
        main_thread.join();
        rte_eal_mp_wait_lcore();

        //  qdma_port_info::print_opennic_regs(port_info_[0].user_bar_idx);
        for (int port_id = 0; port_id < port_num_; port_id++)
        {

            rte_eth_dev_stop(port_id);
            rte_eth_dev_close(port_id);
        }

        int ret = rte_eal_cleanup();
        if (ret == 0)
            Log_info("DPDK Context finished");
        else
            Log_error("DPDK CLEANUP FAILED !!");
    }

    void DpdkTransport::trigger_shutdown()
    {
        force_quit = true;
        for (int i = 0; i < rx_threads_; i++)
        {
            thread_rx_info[i]->shutdown = true;
        }
        for (int i = 0; i < tx_threads_; i++)
        {
            thread_tx_info[i]->shutdown = true;
        }
    }

    /* void DpdkTransport::register_resp_callback(Workload* app) { */
    /*     response_handler = [app](uint8_t* data, int data_len, int id) -> int { */
    /*         return app->process_workload(data, data_len, id); */
    /*     }; */
    /* } */

    void dpdk_thread_info::init(DpdkTransport *th, int th_id, int p_id,
                                int q_id, int burst_size)
    {
        t_layer = th;
        thread_id = th_id;
        port_id = p_id;
        queue_id = q_id;
        max_size = burst_size;
        buf = new struct rte_mbuf *[burst_size];
    }

    int dpdk_thread_info::buf_alloc(struct rte_mempool *mbuf_pool)
    {
        int retval = rte_pktmbuf_alloc_bulk(mbuf_pool, buf, max_size);
        return retval;
    }

    inline void DpdkTransport::do_dpdk_send(
        int port_num, int queue_id, void **bufs, uint64_t num_pkts)
    {
        uint64_t retry_count = 0;
        uint64_t ret = 0;
        struct rte_mbuf **buffers = (struct rte_mbuf **)bufs;
        Log_debug("do_dpdk_send port %d, queue_id %d", port_num, queue_id);
        ret = rte_eth_tx_burst(port_num, queue_id, buffers, num_pkts);
        if (unlikely(ret < 0))
            rte_panic("Can't send burst\n");
        while (ret != num_pkts)
        {
            ret += rte_eth_tx_burst(port_num, queue_id, &buffers[ret], num_pkts - ret);
            retry_count++;
            if (unlikely(retry_count == 1000000))
            {
                Log_info("stuck in rte_eth_tx_burst in port %u queue %u", port_num, queue_id);
                retry_count = 0;
            }
        }
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
