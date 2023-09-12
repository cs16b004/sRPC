
#pragma once
#include <cstdint>
#include "transport.hpp"
#include "utils.hpp"
#define _GNU_SOURCE
#include <utmpx.h>

   

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
    const uint8_t padd[64] = {  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
                                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, };
    DpdkTransport* DpdkTransport::transport_l = nullptr;

//DpdkTransport* DpdkTransport::transport = nullptr;
std::string DpdkTransport::getMacFromIp(std::string ip){
      for(rrr::Config::NetworkInfo it: config_->get_net_info()){
        if (it.ip == ip){
           // Log_debug("Found the Mac for IP: %s, MAC: %s",ip.c_str(),it.mac.c_str());
            return (it.mac);
            
        }
    }
    return "02:de:ad:be:ef:60";
}

uint32_t DpdkTransport::accept(const char* addr_str){
    

    while(!initiated){
        Log_debug("Wating for intialization");
        sleep(1);
        ;
    }

    std::string addr(addr_str);
    size_t idx = addr.find(":");
    if (idx == std::string::npos) {
        Log_error("rrr::Transport: bad accept address: %s", addr);
        return 0;
    }
    std::string server_ip = addr.substr(0, idx);
    uint16_t port = atoi(addr.substr(idx + 1).c_str());
     if (addr_lookup_table.find(addr)!= addr_lookup_table.end()){
        return 0;
   }
     
    // UDPConnection *conn = new UDPConnection(*s_addr);

    TransportConnection* oconn = new TransportConnection();
    int pipefd[2];
    verify(pipe(pipefd)==0);
    oconn->src_addr = src_addr_[config_->host_name_];
    oconn->out_addr = NetAddress(getMacFromIp(server_ip).c_str(),server_ip.c_str(),port);
    oconn->in_fd_  = pipefd[0];
    oconn->wfd = pipefd[1];
    oconn->udp_port = src_addr_[config_->host_name_].port;
        addr = addr+ "::" + std::to_string(oconn->udp_port);
    uint32_t conn_id;
    // choose a connection based on round robin principle;
    conn_th_lock.lock();
    
    
   
    next_thread_+=1;
    
 
    uint16_t chosen_tx_thread = (next_thread_%(config_->num_tx_threads_));
   

    thread_tx_info[chosen_tx_thread]->conn_lock.lock();
    

    conn_counter++;
    
    //verify(thread_rx_info[chosen_rx_thread].conn_counter == thread_tx_info[chosen_tx_thread].conn_counter);

    conn_id = conn_counter;
     Log_info("Chosen threads for new conn: %d, tx-thread %d, next_thread_ %d",conn_id, chosen_tx_thread,next_thread_);
   
    thread_tx_info[chosen_tx_thread]->out_connections[conn_id] = oconn;
    
   
    thread_tx_info[chosen_tx_thread]->addr_lookup_table[addr] = conn_id;

    out_connections[conn_id] = oconn;
    addr_lookup_table[addr] = conn_id;

    thread_tx_info[chosen_tx_thread]->conn_lock.unlock();
    conn_th_lock.unlock();
   // this->connections_[conn_id] = conn;
   uint8_t con_ack_req[30] = {rrr::CON_ACK, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
    
   
    send(con_ack_req, 30*sizeof(uint8_t), conn_id, 
            thread_tx_info[chosen_tx_thread],rrr::SM);
     Log_info("Added [%s] %s, fd r: %d, w: %d",
                            (addr).c_str()
                            ,oconn->out_addr.to_string().c_str()
                            ,oconn->in_fd_,oconn->wfd);
    sleep(1);
    return conn_id;
}
uint32_t DpdkTransport::connect(const char* addr_str){
    while(!initiated){
        Log_debug("Waiting for initialization");
        sleep(1);
        ;
    }
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
    Log_debug("Connecting to a server : %s",addr_str);
   // Log_debug("Mac: %s",getMacFromIp(server_ip).c_str());
   oconn->src_addr = src_addr_[config_->host_name_];
    oconn->out_addr =     NetAddress(getMacFromIp(server_ip).c_str(),server_ip.c_str(),port);
   
    oconn->in_fd_  = pipefd[0];
    oconn->wfd = pipefd[1];
    
    
    uint32_t conn_id;
    conn_th_lock.lock();
    next_thread_++;
    uint64_t chosen_tx_thread = next_thread_%tx_threads_;
    
    thread_tx_info[chosen_tx_thread]->conn_lock.lock();
   
    oconn->udp_port = get_open_port();
    addr = addr + "::" + std::to_string(oconn->udp_port);
    
    conn_counter++;
    
    conn_id = conn_counter;
     Log_info("Chosen threads for new conn: %d is tx-thread %d, counter %d",conn_id, chosen_tx_thread,next_thread_);
    
    thread_tx_info[chosen_tx_thread]->out_connections[conn_id] = oconn;
    thread_tx_info[chosen_tx_thread]->addr_lookup_table[addr] = conn_id;

    out_connections[conn_id] = oconn;
    addr_lookup_table[addr] = conn_id;

    
    thread_tx_info[chosen_tx_thread]->conn_lock.unlock();
    conn_th_lock.unlock();
   // this->connections_[conn_id] = conn;
    uint8_t con_req[30] = {rrr::CON, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0};
    
    send(con_req, 30*sizeof(uint8_t), conn_id, 
            thread_tx_info[chosen_tx_thread],rrr::SM);
    while(!oconn->connected_)
        usleep(50*1000);
    Log_info("Connected to %s, fd r: %d, w: %d",addr.c_str(),oconn->in_fd_,oconn->wfd);
   
    return conn_id;
}
void DpdkTransport::initialize_tx_mbufs(void* arg){ 
      auto tx_info = reinterpret_cast<dpdk_thread_info*>(arg);
    auto dpdk_th = tx_info->dpdk_th;
    unsigned lcore_id = rte_lcore_id();
    std::vector<Marshal*> requests;
    
    
    if (tx_info->count == 0) {
        // Log_info("Mbuf Pool Initialized at index %d, name: %s",tx_info->thread_id%tx_threads_,tx_mbuf_pool[tx_info->thread_id%tx_threads_]->name);
         int ret = tx_info->buf_alloc(tx_mbuf_pool[tx_info->thread_id]);
         if (ret < 0)
             rte_panic("couldn't allocate mbufs");
        
    }
}
int DpdkTransport::dpdk_tx_loop(void* arg){
    auto tx_info = reinterpret_cast<dpdk_thread_info*>(arg);
    auto dpdk_th = tx_info->dpdk_th;
    Log_info("Entering TX thread %d at lcore %d",tx_info->thread_id,rte_lcore_id());
    dpdk_th->initialize_tx_mbufs(arg);
    while(!dpdk_th->force_quit){
        //usleep(1000);
        unsigned lcore_id = rte_lcore_id();
    std::vector<Marshal*> requests;
    tx_info->conn_lock.lock();
    for(auto const& entry:tx_info->out_connections){   
        verify(entry.second != nullptr);
        
        entry.second->outl.lock();
            if(!entry.second->out_messages.empty()){
                 TransportMarshal* req = entry.second->out_messages.front();
                    
                 dpdk_th->send(req->payload,req->n_bytes,entry.first,tx_info,rrr::RR);
                 
                entry.second->out_messages.pop();
            }
        entry.second->outl.unlock();
        
    }
    tx_info->conn_lock.unlock();
        //dpdk_th->tx_loop_one(reinterpret_cast<dpdk_thread_info*>(arg));
    }
    return 0;
}
void DpdkTransport::tx_loop_one(dpdk_thread_info *arg){
    
    unsigned lcore_id = rte_lcore_id();
    std::vector<Marshal*> requests;
    dpdk_thread_info* tx_info = arg;
    tx_info->conn_lock.lock();
    for(auto const& entry:tx_info->out_connections){
       
        verify(entry.second != nullptr);
        entry.second->outl.lock();
            if(!entry.second->out_messages.empty()){
                 TransportMarshal* req = entry.second->out_messages.front();
                    
                 send(req->payload,req->n_bytes,entry.first,tx_info,0x09);
                 
                entry.second->out_messages.pop();
            }
        entry.second->outl.unlock();
        
    }
    tx_info->conn_lock.unlock();
    
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
        uint16_t dest_port = ntohs(udp_hdr->dst_port);
        uint16_t pkt_size = ntohs(udp_hdr->dgram_len) -  sizeof(rte_udp_hdr);
        //Log_debug("Packet matched for connection id : %d, size %d!!",src_port, pkt_size);
        std::string src_addr = ipv4_to_string(src_ip) + ":" + std::to_string(src_port);
        std::string lookup_addr = src_addr + ("::" + std::to_string(dest_port));
        uint8_t* data_ptr = pkt_ptr + data_offset;
        //   for (int i=0; i < pkt_size; ++i) {
        //         if (! (i % 16) && i)
        //                 printf("\n");

        //         printf("0x%02x ", data_ptr[i]);
        // }
       // printf("\n\n");
        uint8_t pkt_type;
        #ifdef RPC_MICRO_STATISTICS
        //read pkt id last 8 bytes are reserved for pkt id
        if(config_->host_name_ == "catskill"){
            uint64_t pkt_id;
            rte_memcpy((uint8_t*) &pkt_id, pkt_ptr + rx_info->buf[i]->data_len - sizeof(uint64_t), sizeof(uint64_t));
   
        
            struct timespec ts;
            timespec_get(&ts, TIME_UTC);
            r_ts_lock.lock();
            pkt_rx_ts[pkt_id] = ts;
            r_ts_lock.unlock();
            Log_debug("Received packet with id %lld. total size %d, id offset: %d, data offset %d",
            pkt_id, rx_info->buf[i]->data_len, rx_info->buf[i]->data_len - sizeof(uint64_t), data_offset+1);
        }    
        #endif
        
        Log_debug("Processing incoming packet from %s",src_addr.c_str());
        mempcpy(&pkt_type,data_ptr,sizeof(uint8_t));
        data_ptr += sizeof(uint8_t); 
        if(pkt_type == rrr::RR ){
         
            if(addr_lookup_table.find(lookup_addr)!= addr_lookup_table.end()){
                #ifdef RPC_MICRO_STATISTICS
                int n;
                if(config_->host_name_ == "catskill"){
                    n = write(out_connections[addr_lookup_table[lookup_addr]]->wfd,data_ptr,pkt_size+8); // sizeof(pkt id)
                }else{
                    n = write(out_connections[addr_lookup_table[lookup_addr]]->wfd,data_ptr,pkt_size);
                }
                #else 
                int n = write(out_connections[addr_lookup_table[lookup_addr]]->wfd,data_ptr,pkt_size);
                #endif
                
                 if (n>0){
                Log_debug("%d bytes written to fd %d, read end %d",
                            n,out_connections[addr_lookup_table[lookup_addr]]->wfd,
                            out_connections[addr_lookup_table[lookup_addr]]->in_fd_);
                }
                if(n < 0 ){
                perror("Message: ");
            }
            }else{
                Log_debug("Packet Dropped as connection not found");
               
            }
        //Log_info("Byres Written %d",n);
            
        }else if (pkt_type == rrr::SM){
            Log_debug("Session Management Packet received pkt type 0x%2x",pkt_type);
            Marshal * sm_req = new Marshal();
            uint8_t req_type;
            
            mempcpy(&req_type,data_ptr,sizeof(uint8_t));
            Log_debug("Req Type 0x%2x, src_addr : %s",req_type, src_addr.c_str());
            if(req_type == rrr::CON_ACK){
                if(out_connections.find(addr_lookup_table[lookup_addr]) != 
                        out_connections.end()){
                    out_connections[addr_lookup_table[lookup_addr]]->connected_ = true;
                    Log_debug("Connection to %s accepted, by thread %d",src_addr.c_str(),rx_info->thread_id);
                        }
                        else{
                            Log_error("Connection not found connid: %d , thread_id %d, addr %s",rx_info->addr_lookup_table[lookup_addr],rx_info->thread_id, lookup_addr.c_str());
                        }
            }else{
                *(sm_req)<<req_type;
                *(sm_req)<<src_addr;
                rx_info->dpdk_th->sm_queue_l.lock();
                rx_info->dpdk_th->sm_queue.push(sm_req);
                rx_info->dpdk_th->sm_queue_l.unlock();
            }
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
            Log_debug("Src addr: %s",src_addr.c_str());
        }
        int prefetch_idx = i + DPDK_PREFETCH_NUM;
        if (prefetch_idx < rx_info->count)
            rte_prefetch0(rte_pktmbuf_mtod(rx_info->buf[prefetch_idx], uint8_t*));
    }

    for (int i = 0; i < rx_info->count; i++)
        rte_pktmbuf_free(rx_info->buf[i]);
}
uint16_t DpdkTransport::get_open_port(){
    
    pc_l.lock();
    uint16_t temp =  u_port_counter;
    u_port_counter+=1;
    pc_l.unlock();
    return temp;
}
void DpdkTransport::send(uint8_t* payload, unsigned length,
                      uint16_t conn_id, dpdk_thread_info* tx_info, uint8_t pkt_type) {
    sendl.lock();
    if (tx_info->count == 0) {
        int ret = tx_info->buf_alloc(tx_mbuf_pool[tx_info->thread_id%tx_threads_]);
        if (ret < 0)
            rte_panic("couldn't allocate mbufs");
    }
    //verify(tx_info != nullptr);
   // Log_info("Tx coun for tx buf: %d",tx_info->count);
    verify(tx_info->buf[tx_info->count] != nullptr);

    uint8_t* pkt_buf = rte_pktmbuf_mtod(tx_info->buf[tx_info->count], uint8_t*);

    int hdr_len = tx_info->make_pkt_header(pkt_buf, length, conn_id);
    int data_offset = hdr_len;
    /** Copy Packet Type*/ 
    memcpy(pkt_buf + data_offset,&pkt_type,sizeof(uint8_t));
    data_offset+=sizeof(uint8_t); // data offset after copying pkt type
     // copy rpc_data
    memcpy(pkt_buf + data_offset, payload, length); //copy rpc data
    data_offset+=length;


     #ifdef RPC_MICRO_STATISTICS // add a pkt id to each packet sent , to see where it spends the most time.
     if(config_->host_name_ == "greenport"){
        pkt_counter++;
        Log_debug("Setting ID offset: %d,", data_offset);
        rte_memcpy(pkt_buf + data_offset, (uint8_t*)&pkt_counter, sizeof(uint64_t));
        data_offset+= sizeof(uint64_t);
    }
        
    #endif

    int data_size = data_offset; 
    if(data_size < 64){
        memcpy(pkt_buf + data_size, padd, 64-data_size);
       
    }
    data_size = std::max(data_size,64);

    tx_info->buf[tx_info->count]->ol_flags =  RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_UDP_CKSUM;
    tx_info->buf[tx_info->count]->nb_segs = 1;
    tx_info->buf[tx_info->count]->pkt_len = data_size;
     tx_info->buf[tx_info->count]->l2_len = sizeof(struct rte_ether_hdr);
     tx_info->buf[tx_info->count]->l3_len = sizeof(struct rte_ipv4_hdr);
     tx_info->buf[tx_info->count]->l4_len = sizeof(struct rte_udp_hdr);
    tx_info->buf[tx_info->count]->data_len = data_size;

    Log_debug("Sending Packet from connID %d, thread %d",conn_id,tx_info->thread_id);
    Log_debug("send packet to server %s with size of %d, pkt type 0x%2x",tx_info->out_connections[conn_id]->out_addr.to_string().c_str(), data_size,pkt_type);

    tx_info->count++;
    if (unlikely(tx_info->count == tx_info->max_size)) {
        int ret = rte_eth_tx_burst(tx_info->port_id, tx_info->queue_id,
                                   tx_info->buf, tx_info->count);
        if (unlikely(ret < 0))
            Log_error("Tx couldn't send\n");

        if (unlikely(ret != tx_info->count)){
            Log_error("Couldn't send all packets");
            
        }

        tx_info->stat.pkt_count += tx_info->count;
        tx_info->count = 0;
       
    }
    else
        tx_info->stat.pkt_error += tx_info->count;
    sendl.unlock();
}

int dpdk_thread_info::make_pkt_header(uint8_t *pkt, int payload_len, uint32_t conn_id) {
    NetAddress& src_addr = out_connections[conn_id]->src_addr;
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
    gen_udp_header(udp_hdr, out_connections[conn_id]->udp_port, dest_addr.port , payload_len);

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
        tx_threads_ = config->num_tx_threads_;
        rx_threads_ = config->num_rx_threads_;
        next_thread_=0;
        Log_info("Thread counter %d, num_rx_threads %d, tx_threads %d", next_thread_, rx_threads_, tx_threads_);
        std::bitset<128> affinity_mask;
         for(int i=config->core_affinity_mask_[0];i <= config->core_affinity_mask_[1];i++)
            affinity_mask.set(i);
        gettimeofday(&start_clock, NULL);
        main_thread = std::thread([this, argv_str](){
            this->init_dpdk_main_thread(argv_str);
        });

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        int core_id;
        for(core_id=0; core_id< affinity_mask.size(); core_id++){
            if (affinity_mask.test(core_id)){
                //Log_debug("Setting cpu affinity to cpu: %d for thread id %s-%d",core_id,stringify(type_).c_str(),thread_id_);
                CPU_SET(core_id, &cpuset);
            }
        }

        int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
        assert((core_id <= num_cores));

        int err = pthread_setaffinity_np(main_thread.native_handle(), sizeof(cpu_set_t), &cpuset);
        if (err < 0) {
            Log_error("Couldn't set affinity of thread DPDK_INIT_THREAD to core %d", core_id);
            return;
        }
    }
     init_lock.unlock();
 
}
    
void DpdkTransport::create_transport(Config* config){
    if (transport_l == nullptr){
        transport_l = new DpdkTransport;
    }
    transport_l->init(config);
}
DpdkTransport* DpdkTransport::get_transport(){
    verify(transport_l != nullptr);
    return transport_l;
}
void DpdkTransport::init_dpdk_main_thread(const char* argv_str) {
    bool in_numa_node=false;
    Config* conf = Config::get_config();
    while(1){
        if(sched_getcpu() >= (conf->cpu_info_.numa)*(conf->cpu_info_.core_per_numa)
                || sched_getcpu() <= (conf->cpu_info_.numa +1)*(conf->cpu_info_.core_per_numa) ){
            break;
        }else{
            Log_info("Waiting for scheduled on right node");
            sleep(1);
        }
    }
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
   
    tx_queue_ = tx_threads_ ;
    rx_queue_ = rx_threads_ ;

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
                                                         (uint16_t)1000*RTE_MBUF_DEFAULT_BUF_SIZE, 
                                                         rte_socket_id());
        if (rx_mbuf_pool[pool_idx] == NULL)
            rte_exit(EXIT_FAILURE, "Cannot create rx mbuf pool %d\n", pool_idx);
    }

    /* Will initialize buffers in port_init function */
    thread_rx_info = new dpdk_thread_info*[rx_threads_];
    thread_tx_info = new dpdk_thread_info*[tx_threads_];
    for(int i=0;i<rx_threads_;i++){
        thread_rx_info[i] = new dpdk_thread_info();
    }
    for(int i=0;i<tx_threads_;i++){
        thread_tx_info[i] = new dpdk_thread_info();
    }

     uint16_t portid;
     RTE_ETH_FOREACH_DEV(portid) {

        if (port_init(portid) != 0)
            rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n",
                     portid);
    }

    Log_info("DPDK tx threads %d, rx threads %d", tx_threads_, rx_threads_);



    uint16_t total_lcores = rte_lcore_count();
 
    Log_info("Total Cores available: %d",total_lcores);
    uint16_t rx_lcore_lim = rx_threads_;
    uint16_t tx_lcore_lim =  rx_threads_ + tx_threads_;

    uint8_t numa_id =  config_->get_cpu_info().numa;
    // Add core per numa so that threads are scheduled on rigt lcores
    uint16_t lcore;
    rx_lcore_lim += numa_id * Config::get_config()->cpu_info_.core_per_numa;
    tx_lcore_lim += numa_id * Config::get_config()->cpu_info_.core_per_numa;
    Log_info("rx_core limit: %d tx_core limit: %d, my core_id %d ",rx_lcore_lim+1,tx_lcore_lim+1, sched_getcpu());
    for (lcore = numa_id * Config::get_config()->cpu_info_.core_per_numa + 1; lcore < rx_lcore_lim +1 ; lcore++) {
            
            int retval = rte_eal_remote_launch(dpdk_rx_loop, thread_rx_info[lcore%rx_threads_], lcore );
            if (retval < 0)
                rte_exit(EXIT_FAILURE, "Couldn't launch core %d\n", lcore % total_lcores);
       
        
    }

    
    for (lcore = rx_lcore_lim+1; lcore < tx_lcore_lim+1; lcore++) {
            
            int retval = rte_eal_remote_launch(dpdk_tx_loop, thread_tx_info[lcore%tx_threads_], lcore );
            if (retval < 0)
                rte_exit(EXIT_FAILURE, "Couldn't launch core %d\n", lcore % total_lcores);
        
    }
    initiated = true;
   
}

void DpdkTransport::addr_config(std::string host_name,
                       std::vector<Config::NetworkInfo> net_info) {
    Log_info("Setting up network info....");
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
        Log_info("Adding a host with name %s : info :\n %s",net.name.c_str(),net.to_string().c_str());
        verify(it == addr->end());
        
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

     memcpy((void*)(&rxconf) , (void*)&(dev_info.default_rxconf),sizeof(struct rte_eth_rxconf));

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
      
        retval = rte_eth_rx_queue_setup(port_id, q, nb_rxd,
                                        rte_eth_dev_socket_id(port_id),
                                        &rxconf, rx_mbuf_pool[q]);
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
      
        Log_debug("Create rx thread %d info on port %d and queue %d",
                    i, port_id, i);
        thread_rx_info[i]->init(this, i, port_id, i, DPDK_RX_BURST_SIZE);
    }

    for (int i = 0; i < tx_queue_; i++) {
        Log_debug("Create tx thread %d info on port %d and queue %d",
                    i, port_id, i);
        thread_tx_info[i]->init(this, i, port_id, i, DPDK_TX_BURST_SIZE);
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
        

        rte_eth_dev_stop(port_id);
        rte_eth_dev_close(port_id);
        
    }

    rte_eal_cleanup();

    packet_stats total_rx_stat, total_tx_stat;
    for (int i = 0; i < tx_threads_; i++)
        total_tx_stat.merge(thread_tx_info[i]->stat);
    for (int i = 0; i < rx_threads_; i++) {
        Log_info("Log Rx tid: %d, port id: %d, qid: %d", 
                 thread_rx_info[i]->thread_id, thread_rx_info[i]->port_id,
                 thread_rx_info[i]->queue_id);
        thread_rx_info[i]->stat.show_statistics();
        total_rx_stat.merge(thread_rx_info[i]->stat);
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
            ss<<"\n[ IP: "<<ipv4_to_string((ip))<<"\n  "<<"mac: "<<mac_to_string(mac)<<"\n ";
            ss<<"port: "<<std::to_string(port)<<"\n]";
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
