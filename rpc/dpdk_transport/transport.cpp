
#pragma once
#include <cstdint>
#include "transport.hpp"
#include "utils.hpp"


#define DPDK_RX_DESC_SIZE           1024
#define DPDK_TX_DESC_SIZE           1024

#define DPDK_NUM_MBUFS              8192
#define DPDK_MBUF_CACHE_SIZE        250
#define DPDK_RX_BURST_SIZE          64
#define DPDK_TX_BURST_SIZE          1
#define MAX_PATTERN_NUM		3
#define MAX_ACTION_NUM		2
#define DPDK_RX_WRITEBACK_THRESH    64
#define SRC_IP ((0<<24) + (0<<16) + (0<<8) + 0) /* src ip = 0.0.0.0 */

#define FULL_MASK 0xffffffff /* full mask */

#define EMPTY_MASK 0x0 /* empty mask */
#define DPDK_PREFETCH_NUM           2
namespace rrr{
    struct Request;


//DpdkTransport* DpdkTransport::transport = nullptr;
std::string DpdkTransport::getMacFromIp(std::string ip){
      for(rrr::Config::NetworkInfo it: config_->get_net_info()){
        if (it.ip == ip){
            Log_debug("Found the Mac for IP: %s, MAC: %s",ip.c_str(),it.mac.c_str());
            return (it.mac);
            
        }
    }
    return "02:de:ad:be:ef:60";
}

// DpdkTransport* DpdkTransport::getTransport(){
//     assert(transport != nullptr);
//     return transport;
// }

// int DpdkTransport::createTransport(){
//     if(transport != nullptr)
//             return -1;
//         else{
//             transport = new DpdkTransport();
//             return 0;
//         }
// }
uint32_t DpdkTransport::accept(const char* addr_str){
    std::string addr(addr_str);
    size_t idx = addr.find(":");
    if (idx == std::string::npos) {
        Log_error("rrr::Transport: bad connect address: %s", addr);
        return EINVAL;
    }
    std::string server_ip = addr.substr(0, idx);
    uint16_t port = atoi(addr.substr(idx + 1).c_str());

     
    // UDPConnection *conn = new UDPConnection(*s_addr);
    TransportConnection* oconn = new TransportConnection();
    int pipefd[2];
    verify(pipe(pipefd)==0);
    
    oconn->out_addr = NetAddress(getMacFromIp(server_ip).c_str(),server_ip.c_str(),port);
    oconn->in_fd_  = pipefd[0];
    oconn->wfd = pipefd[1];
    uint32_t conn_id;
    conn_lock.lock();
    conn_counter++;
    conn_id = conn_counter;
    out_connections[conn_id] = oconn;
    addr_lookup[addr] = conn_id;
    conn_lock.unlock();
   // this->connections_[conn_id] = conn;
   
    return conn_id;
}
uint32_t DpdkTransport::connect(const char* addr_str){

    std::string addr(addr_str);
    size_t idx = addr.find(":");
    if (idx == std::string::npos) {
        Log_error("rrr::Transport: bad connect address: %s", addr);
        return EINVAL;
    }
    std::string server_ip = addr.substr(0, idx);
    uint16_t port = atoi(addr.substr(idx + 1).c_str());

 
    // UDPConnection *conn = new UDPConnection(*s_addr);
    TransportConnection* oconn = new TransportConnection();
    int pipefd[2];
    verify(pipe(pipefd)==0);
    Log_debug("Connecting with a server");
    Log_debug("Mac: %s",getMacFromIp(server_ip).c_str());
    oconn->out_addr =     NetAddress(getMacFromIp(server_ip).c_str(),server_ip.c_str(),port);
    Log_debug("Network added");
    oconn->in_fd_  = pipefd[0];
    oconn->wfd = pipefd[1];
    uint32_t conn_id;
    conn_lock.lock();
    conn_counter++;
    conn_id = conn_counter;
    out_connections[conn_id] = oconn;
    addr_lookup[addr] = conn_id;
    conn_lock.unlock();
   // this->connections_[conn_id] = conn;
    uint8_t con_req = rrr::CON;
    while(!initiated){
        // Log_debug("Wating for intialization");
        // usleep(2);
        ;
    }
    Log_debug("Added %s",oconn->out_addr.to_string().c_str());
    send(&con_req,sizeof(uint8_t),conn_id,&thread_tx_info[rx_threads_%tx_threads_],rrr::SM);
    sleep(1);
    return conn_id;
}
void DpdkTransport::initialize_tx_mbufs(void* arg){
      auto rx_info = reinterpret_cast<dpdk_thread_info*>(arg);
    auto dpdk_th = rx_info->dpdk_th;
    unsigned lcore_id = rte_lcore_id();
    std::vector<Marshal*> requests;
    dpdk_thread_info* tx_info = &(thread_tx_info[rte_lcore_id()%tx_threads_]);
    
    if (tx_info->count == 0) {
         Log_debug("Mbuf Pool Initialized at index %d, name: %s",tx_info->thread_id%tx_threads_,tx_mbuf_pool[tx_info->thread_id%tx_threads_]->name);
         int ret = tx_info->buf_alloc(tx_mbuf_pool[tx_info->thread_id%tx_threads_]);
         if (ret < 0)
             rte_panic("couldn't allocate mbufs");
        
    }
}
int DpdkTransport::dpdk_tx_loop(void* arg){
    auto tx_info = reinterpret_cast<dpdk_thread_info*>(arg);
    auto dpdk_th = tx_info->dpdk_th;
    Log_info("Entering TX Loop at lcore %d",rte_lcore_id());
    dpdk_th->initialize_tx_mbufs(arg);
    while(!dpdk_th->force_quit){
        dpdk_th->tx_loop_one(reinterpret_cast<dpdk_thread_info*>(arg));
    }
    return 0;
}
void DpdkTransport::tx_loop_one(dpdk_thread_info *arg){
    
    unsigned lcore_id = rte_lcore_id();
    std::vector<Marshal*> requests;
    dpdk_thread_info* tx_info = &(thread_tx_info[rte_lcore_id()%tx_threads_]);
    
    for(std::pair<uint32_t,TransportConnection*> entry:tx_info->dpdk_th->out_connections){
        entry.second->outl.lock();
            if(!entry.second->out_messages.empty()){
                 Marshal* req = entry.second->out_messages.front();
                 size_t n_bytes = req->content_size();
                 uint8_t payload[n_bytes];
                 req->read(payload,n_bytes);
                 send(payload,n_bytes,entry.first,tx_info,rrr::RR);
                 
                entry.second->out_messages.pop();
            }
            entry.second->outl.unlock();
        
    }
    
    
    return;
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
        
        struct rte_ipv4_hdr* ip_hdr = reinterpret_cast<struct rte_ipv4_hdr*>(pkt_ptr + ip_hdr_offset);
        uint32_t src_ip = ip_hdr->src_addr;

        struct rte_udp_hdr* udp_hdr = reinterpret_cast<struct rte_udp_hdr*> (pkt_ptr + udp_hdr_offset);
        
        uint16_t src_port = ntohs(udp_hdr->src_port);
        
        uint16_t pkt_size = ntohs(udp_hdr->dgram_len) -  sizeof(rte_udp_hdr);
        //Log_debug("Packet matched for connection id : %d, size %d!!",src_port, pkt_size);
        std::string src_addr = ipv4_to_string(src_ip) + ":" + std::to_string(src_port);
        uint8_t* data_ptr = pkt_ptr + udp_hdr_offset + sizeof(rte_udp_hdr);
        //   for (int i=0; i < pkt_size; ++i) {
        //         if (! (i % 16) && i)
        //                 printf("\n");

        //         printf("0x%02x ", data_ptr[i]);
        // }
        // printf("\n\n");
        uint8_t pkt_type;
        mempcpy(&pkt_type,data_ptr,sizeof(uint8_t));
        data_ptr += sizeof(uint8_t); 
        if(pkt_type == rrr::RR ){
            if(rx_info->dpdk_th->addr_lookup.find(src_addr)!=addr_lookup.end()){
                int n = write(rx_info->dpdk_th->out_connections[addr_lookup[src_addr]]->wfd,data_ptr,pkt_size-sizeof(uint8_t));
                #ifdef TRANSPORT_STATS
                rx_info->stat.pkt_count++;
                gettimeofday(&current, NULL);
                int elapsed_sec = current.tv_sec - start_clock.tv_sec;

                if (elapsed_sec >= 100) {
                    gettimeofday(&start_clock,NULL);
                    rx_info->stat.show_statistics();
                }
                #endif
                if(n < 0 ){
                perror("Message: ");
            }
            }else{
                Log_debug("Packet Dropped as connection not found");
                #ifdef TRANSPORT_STATS
                rx_info->stat.pkt_error++;
                gettimeofday(&current, NULL);
                int elapsed_sec = current.tv_sec - start_clock.tv_sec;

                if (elapsed_sec >= 100) {
                    gettimeofday(&start_clock,NULL);
                    rx_info->stat.show_statistics();
                }
                #endif
            }
        //Log_info("Byres Written %d",n);
            
        }else if (pkt_type == rrr::SM){
            Log_debug("Session Management Packet received pkt type 0x%2x",pkt_type);
            Marshal * sm_req = new Marshal();
            uint8_t req_type;
            mempcpy(&req_type,data_ptr,sizeof(uint8_t));
            *(sm_req)<<req_type;
            *(sm_req)<<src_addr;
            rx_info->dpdk_th->sm_queue_l.lock();
            rx_info->dpdk_th->sm_queue.push(sm_req);
            rx_info->dpdk_th->sm_queue_l.unlock();
              #ifdef TRANSPORT_STATS
                rx_info->stat.pkt_count++;
                gettimeofday(&current, NULL);
                int elapsed_sec = current.tv_sec - start_clock.tv_sec;

                if (elapsed_sec >= 100) {
                    gettimeofday(&start_clock,NULL);
                    rx_info->stat.show_statistics();
                }
                #endif
               
            
        }else{
            Log_debug("Packet Type Not found");
        }
        int prefetch_idx = i + DPDK_PREFETCH_NUM;
        if (prefetch_idx < rx_info->count)
            rte_prefetch0(rte_pktmbuf_mtod(rx_info->buf[prefetch_idx], uint8_t*));
    }

    for (int i = 0; i < rx_info->count; i++)
        rte_pktmbuf_free(rx_info->buf[i]);
}

void DpdkTransport::send(uint8_t* payload, unsigned length,
                      uint16_t conn_id, dpdk_thread_info* tx_info, uint8_t pkt_type) {
    
    if (tx_info->count == 0) {
        int ret = tx_info->buf_alloc(tx_mbuf_pool[tx_info->thread_id%tx_threads_]);
        if (ret < 0)
            rte_panic("couldn't allocate mbufs");
    }
    
    uint8_t* pkt_buf = rte_pktmbuf_mtod(tx_info->buf[tx_info->count], uint8_t*);

    int hdr_len = make_pkt_header(pkt_buf, length, conn_id);
    /* We want to have uniform distribution accross all of rx threads */
    /* tx_info->udp_port_id = (tx_info->udp_port_id + 1) % rx_queue_; */
    /** Copy Packet Type*/ 
    memcpy(pkt_buf+hdr_len,&pkt_type,sizeof(uint8_t));
    memcpy(pkt_buf + hdr_len + sizeof(uint8_t), payload, length);

    int data_size = hdr_len + sizeof(uint8_t)  + length;
    tx_info->buf[tx_info->count]->ol_flags = (PKT_TX_IPV4| PKT_TX_IP_CKSUM | PKT_TX_UDP_CKSUM);
    tx_info->buf[tx_info->count]->nb_segs = 1;
    tx_info->buf[tx_info->count]->pkt_len = data_size;
     tx_info->buf[tx_info->count]->l2_len = sizeof(struct rte_ether_hdr);
     tx_info->buf[tx_info->count]->l3_len = sizeof(struct rte_ipv4_hdr);
     tx_info->buf[tx_info->count]->l4_len = sizeof(struct rte_udp_hdr);
    tx_info->buf[tx_info->count]->data_len = data_size;
    Log_debug("send packet to server %s with size of %d, pkt type 0x%2x",out_connections[conn_id]->out_addr.to_string().c_str(), data_size,pkt_type);

    tx_info->count++;
    if (unlikely(tx_info->count == tx_info->max_size)) {
        int ret = rte_eth_tx_burst(tx_info->port_id, tx_info->queue_id,
                                   tx_info->buf, tx_info->count);
        if (unlikely(ret < 0))
            Log_error("Tx couldn't send\n");

        if (unlikely(ret != tx_info->count))
            Log_error("Couldn't send all packets");

        tx_info->stat.pkt_count += tx_info->count;
        tx_info->count = 0;
    }
    else
        tx_info->stat.pkt_error += tx_info->count;
}

int DpdkTransport::make_pkt_header(uint8_t *pkt, int payload_len, uint32_t conn_id) {
    NetAddress& src_addr = src_addr_[config_->host_name_];
    if(out_connections.find(conn_id) == out_connections.end())
        return -1; 
    NetAddress& dest_addr = out_connections[conn_id]->out_addr; 

    unsigned pkt_offset = 0;
    rte_ether_hdr* eth_hdr = reinterpret_cast<rte_ether_hdr*>(pkt);
    gen_eth_header(eth_hdr, src_addr.mac, dest_addr.mac);

    pkt_offset += sizeof(rte_ether_hdr);
    rte_ipv4_hdr* ipv4_hdr = reinterpret_cast<rte_ipv4_hdr*>(pkt + pkt_offset);
    gen_ipv4_header(ipv4_hdr, src_addr.ip, (dest_addr.ip), payload_len);

    pkt_offset += sizeof(ipv4_hdr_t);
    rte_udp_hdr* udp_hdr = reinterpret_cast<rte_udp_hdr*>(pkt + pkt_offset);
    int client_port_addr = src_addr.port;
    gen_udp_header(udp_hdr, client_port_addr, dest_addr.port, payload_len);

    pkt_offset += sizeof(udp_hdr_t);
    return pkt_offset;
}

void DpdkTransport::init(Config* config) {
   // src_addr_ = new NetAddress();
    init_lock.lock();
    if(!initiated){
        config_= config;
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
        gettimeofday(&start_clock, NULL);
        main_thread = std::thread([this, argv_str](){
            this->init_dpdk_main_thread(argv_str);
        });
    }
    
    init_lock.unlock();

    
    //sleep(200);
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
        sprintf(pool_name, "RRR_TX_MBUF_POOL_%d", pool_idx);
        /* TODO: Fix it for machines with more than one NUMA node */
        tx_mbuf_pool[pool_idx] = rte_pktmbuf_pool_create(pool_name, DPDK_NUM_MBUFS,
                                                         DPDK_MBUF_CACHE_SIZE, 0, 
                                                         1000*RTE_MBUF_DEFAULT_BUF_SIZE, 
                                                         rte_socket_id());
        if (tx_mbuf_pool[pool_idx] == NULL)
            rte_exit(EXIT_FAILURE, "Cannot create tx mbuf pool %d\n", pool_idx);
    }

    rx_mbuf_pool = new struct rte_mempool*[rx_threads_];
    for (int pool_idx = 0; pool_idx < rx_threads_; pool_idx++) {
        char pool_name[1024];
        sprintf(pool_name, "RRR_RX_MBUF_POOL_%d", pool_idx);
        /* TODO: Fix it for machines with more than one NUMA node */
        rx_mbuf_pool[pool_idx] = rte_pktmbuf_pool_create(pool_name, DPDK_NUM_MBUFS,
                                                         DPDK_MBUF_CACHE_SIZE, 0, 
                                                         1000*RTE_MBUF_DEFAULT_BUF_SIZE, 
                                                         rte_socket_id());
        if (rx_mbuf_pool[pool_idx] == NULL)
            rte_exit(EXIT_FAILURE, "Cannot create rx mbuf pool %d\n", pool_idx);
    }

    /* Will initialize buffers in port_init function */
    thread_rx_info = new dpdk_thread_info[rx_threads_];
    thread_tx_info = new dpdk_thread_info[tx_threads_];

    

     uint16_t portid;
     RTE_ETH_FOREACH_DEV(portid) {

        if (port_init(portid) != 0)
            rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n",
                     portid);
    }

    Log_info("DPDK tx threads %d, rx threads %d", tx_threads_, rx_threads_);

    uint16_t lcore;
    for (lcore = 1; lcore < rx_threads_; lcore++) {
        Log_info("Launching RX Thread");
        int retval = rte_eal_remote_launch(dpdk_rx_loop, &thread_rx_info[lcore], lcore);
        if (retval < 0)
            rte_exit(EXIT_FAILURE, "Couldn't launch core %d\n", lcore);
    }
     for (lcore = rx_threads_; lcore < rx_threads_+tx_threads_; lcore++) {
        Log_info("Launching TX Thread");
        int retval = rte_eal_remote_launch(dpdk_tx_loop, &thread_tx_info[lcore%tx_threads_], lcore);
        if (retval < 0)
            rte_exit(EXIT_FAILURE, "Couldn't launch core %d\n", lcore);
    }
    lcore = 0;
    initiated = true;
    dpdk_rx_loop(&thread_rx_info[lcore]);
}

void DpdkTransport::addr_config(std::string host_name,
                       std::vector<Config::NetworkInfo> net_info) {
    Log_info("Setting up netowkr info....");
    for (auto& net : net_info) {
        std::map<std::string, NetAddress>* addr;
        if (host_name == net.name){
            addr = &src_addr_;
            Log_info("Configuring local address %d, %s",net.id,net.ip.c_str());
        }
        else
            addr = &dest_addr_;

        /* if (net.type == host_type) */
        /*     addr = &src_addr_; */
        /* else */
        /*     addr = &dest_addr_; */

        auto it = addr->find(host_name);
        
        verify(it == addr->end());
        Log_info("Adding a host with name %s : info :\n %s",net.name.c_str(),net.to_string().c_str());
        addr->emplace(std::piecewise_construct,
                      std::forward_as_tuple(net.name),
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
    isolate(port_id);

    memset(&port_conf, 0x0, sizeof(struct rte_eth_conf));
    memset(&txconf, 0x0, sizeof(struct rte_eth_txconf));
    memset(&rxconf, 0x0, sizeof(struct rte_eth_rxconf));
    port_conf = {
		.rxmode = {
			.split_hdr_size = 0,
		},
		.txmode = {
			.offloads =
				DEV_TX_OFFLOAD_VLAN_INSERT |
				DEV_TX_OFFLOAD_IPV4_CKSUM  |
				DEV_TX_OFFLOAD_UDP_CKSUM   |
				DEV_TX_OFFLOAD_TCP_CKSUM   |
				DEV_TX_OFFLOAD_SCTP_CKSUM  |
				DEV_TX_OFFLOAD_TCP_TSO,
		},
	};
    
    port_conf.txmode.offloads &= dev_info.tx_offload_capa;

    rxconf = dev_info.default_rxconf;
	rxconf.offloads = port_conf.rxmode.offloads;
    
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
    install_flow_rule(port_id);
    return 0;
}

int DpdkTransport::port_close(uint16_t port_id) {
   

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

std::string NetAddress::getAddr(){
    std::stringstream ss;
    ss<<ipv4_to_string(ip)<<":"<<std::to_string(port);
    return ss.str();
}

std::string NetAddress::to_string(){
            std::stringstream ss;
            ss<<"\n[ IP: "<<ipv4_to_string((ip))<<"\n  "<<"mac: "<<mac_to_string(mac)<<"\n ]";
            return ss.str();
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
    Log_info("Packets Received Total= %d", pkt_count);
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

inline void DpdkTransport::do_dpdk_send(
    int port_num, int queue_id, void** bufs, uint64_t num_pkts) {
    uint64_t retry_count = 0;
    uint64_t ret = 0;
    struct rte_mbuf** buffers = (struct rte_mbuf**)bufs;
    Log_debug("do_dpdk_send port %d, queue_id %d", port_num, queue_id);
    ret = rte_eth_tx_burst(port_num, queue_id, buffers, num_pkts);
    if (unlikely(ret < 0)) rte_panic("Can't send burst\n");
    while (ret != num_pkts) {
      ret += rte_eth_tx_burst(port_num, queue_id, &buffers[ret], num_pkts - ret);
      retry_count++;
      if (unlikely(retry_count == 1000000)) {
        Log_info("stuck in rte_eth_tx_burst in port %u queue %u", port_num, queue_id);
        retry_count = 0;
      }
    }
  }
int  DpdkTransport::isolate(uint8_t phy_port){ 
	 	struct rte_flow_error* error = (struct rte_flow_error*) malloc(sizeof(struct rte_flow_error));
		int ret = rte_flow_isolate(phy_port, 1,error);
		if (ret < 0) 
             Log_error("Failed to enable flow isolation for port %d\n, message: %s", phy_port,error->message);
        else
             Log_info("Flow isolation enabled for port %d\n", phy_port);
		return ret; 
}



void DpdkTransport::install_flow_rule(size_t phy_port){
    
  
   struct rte_flow_attr attr;
	struct rte_flow_item pattern[MAX_PATTERN_NUM];
	struct rte_flow_action action[MAX_ACTION_NUM];
	struct rte_flow *flow = NULL;
	struct rte_flow_action_queue queue = { .index = 0 };
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
    attr.priority =1 ;
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
    //ip_spec.hdr.src_addr = 0;
    //ip_mask.hdr.src_addr = RTE_BE32(0);

    Log_info("IP Address to be queued %s",ipv4_to_string(ip_spec.hdr.dst_addr).c_str());

	//ip_mask.hdr.dst_addr = 
	
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
    

	if (!res){
		flow = rte_flow_create(phy_port, &attr, pattern, action, &error);
        Log_info("Flow Rule Added for IP Address : %s",ipv4_to_string(src_addr_[config_->host_name_].ip).c_str());
        // int ret = rte_flow_isolate(phy_port, 1,&error);
   
        //  if (!ret) 
        //     Log_error("Failed to enable flow isolation for port %d\n, message: %s", phy_port,error.message);
        //  else
        //     Log_info("Flow isolation enabled for port %d\n", phy_port);
    }else{
        Log_error("Failed to create flow rule: %s\n", error.message);
    }

}
}
