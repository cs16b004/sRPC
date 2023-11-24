
#include <cstdint>
#include "rpc/dpdk_transport/transport.hpp"
#include <rte_ring.h>
#include<rte_ring_core.h>
#include "rpc/utils.hpp"
#include<rte_timer.h>
#include "rpc/dpdk_transport/transport_marshal.hpp"
#include<ctime>

namespace rrr{

static uint64_t raw_time(void) {
    struct timespec tstart={0,0};
    clock_gettime(CLOCK_MONOTONIC, &tstart);
    uint64_t t = (uint64_t)(tstart.tv_sec*1.0e9 + tstart.tv_nsec);
    return t;

}
int DpdkTransport::dpdk_tx_loop(void* arg){
    dpdk_thread_info* info = reinterpret_cast<dpdk_thread_info*>(arg);
    DpdkTransport* dpdk_th = info->t_layer;

    Config* conf = Config::get_config();
    
 
    // Initialize sm_ring (multiproducer) Single consumer
    // put it in transport_layer global datastructure of rings
    // initalize data_structures locaaly which will be used in future;

    rte_mempool* mem_pool = info->mem_pool;
    
    unsigned int available = 0;
    unsigned int nb_sm_reqs_ =0;
    unsigned int nb_pkts=0;
    uint16_t ret=0;
    uint16_t retry_count=0;

    uint16_t queue_id = info->queue_id;
    uint16_t port_id = info->port_id;
    uint16_t buf_len = conf->buffer_len;
    uint16_t burst_size = conf->burst_size;
    rte_mbuf** my_buffers = info->deque_bufs;

    uint64_t burst_count=0;
    uint16_t &conn_limit = info->conn_counter;
    rte_ring** out_ring = info->out_ring;
    rte_ring** avail_ring = info->avail_ring;
    int i,j;
    uint64_t times=0;
     Log_info("Entering TX thread %d at lcore %d",info->thread_id,rte_lcore_id());
   
    while(!info->shutdown){
      
        for(i=0; i< conn_limit; i++)
        {
           
            if ((out_ring[i] == nullptr)|| rte_ring_count(out_ring[i]) < 5)
                continue;
            

            times++;
            nb_pkts =  rte_ring_sc_dequeue_burst(out_ring[i], (void**) my_buffers, burst_size, &available);

            if(nb_pkts <= 0)
                continue;

            ret = rte_eth_tx_burst(port_id, queue_id, my_buffers, nb_pkts);
           // LOG_DEBUG("NB pkts %d, sent %d, conn_id %lld", nb_pkts, ret, conn_entry.first);
            if (unlikely(ret < 0)) rte_panic("Can't send burst\n");
            while (ret != nb_pkts) {
                ret += rte_eth_tx_burst(port_id, queue_id, &my_buffers[ret], nb_pkts - ret);
                retry_count++;
                if (unlikely(retry_count == 100)) {
                    Log_warn("stuck in rte_eth_tx_burst in port %u queue %u", port_id, queue_id);
                    retry_count = 0;
                }
            }
            retry_count=0;
           //rte_pktmbuf_free_bulk(my_buffers,nb_pkts);
            
            while(rte_ring_sp_enqueue_bulk(avail_ring[i], (void**) my_buffers, nb_pkts, &available) == 0){
                    retry_count++;
                if (unlikely(retry_count == 100)) {
                    Log_warn("stuck in rte_avail_buffers enqueue for conn: %lld", i);
                    retry_count = 0;
                }
            }
            burst_count+=nb_pkts;
            
        }
    }
    Log_info("Exiting TX thread %d, Average Burst Count %llu",info->thread_id, (unsigned)(burst_count/times));
    return 0;
}

int DpdkTransport::dpdk_rx_loop(void* arg) {
    dpdk_thread_info* info = reinterpret_cast<dpdk_thread_info*>(arg);
    DpdkTransport* dpdk_th = info->t_layer;

    Config* conf = Config::get_config();
    
 
    // Initialize sm_ring (multiproducer) Single consumer
    // put it in transport_layer global datastructure of rings
    // initalize data_structures locaaly which will be used in future;
   
    unsigned int available = 0;
    
    unsigned int nb_pkts=0;
    int ret=0;
    uint16_t retry_count=0;

    uint16_t queue_id = info->queue_id;
    uint16_t port_id = info->port_id;
    uint16_t buf_len = conf->buffer_len;
    uint16_t burst_size = conf->burst_size;
    rte_mbuf** rx_buffers = info->deque_bufs;

    uint64_t burst_count=0;

    std::unordered_map<uint64_t,rte_ring*>& in_bufring = dpdk_th->in_ring;
    uint8_t* pkt_ptr;
    struct rte_ipv4_hdr* ip_hdr;
    uint32_t src_ip ;
    struct rte_udp_hdr* udp_hdr ;

    uint8_t pkt_type;
    uint8_t* data_ptr;
 
    Log_info("Enter RX thread %d on lcore: %d",
             info->thread_id, rte_lcore_id());
    int retry = 0;
    uint64_t conn_id;
    uint64_t start;
    uint64_t end;
    uint64_t count=0;
    uint64_t times=0;
    uint64_t sum=0;
    rte_ring* r2=nullptr;
    while(!info->shutdown) {
        // #ifdef RPC_STATISTICS
        //     if((times % 30000) == 1)
        //         start = raw_time();
            
        // #endif
        
        uint16_t nb_rx = rte_eth_rx_burst(port_id, queue_id, rx_buffers, burst_size);

        if (unlikely(nb_rx == 0))
            continue;
        for(int i=0;i<nb_rx;i++){
            rte_prefetch2(rx_buffers[i]);
        }
        for (int i = 0; i < nb_rx; i++) {
            pkt_ptr = rte_pktmbuf_mtod(rx_buffers[i], uint8_t*);
            ip_hdr = reinterpret_cast<struct rte_ipv4_hdr*>(pkt_ptr + ip_hdr_offset);
            src_ip = ip_hdr->src_addr;
            udp_hdr = reinterpret_cast<struct rte_udp_hdr*> (pkt_ptr + udp_hdr_offset);

            conn_id = 0;
            conn_id = src_ip;
            conn_id = conn_id<<16;
            // server port in BE 
            conn_id = conn_id | (uint64_t)(udp_hdr->src_port);
            //local host port in BE
            conn_id = conn_id<<16;
            conn_id  = conn_id | (uint64_t)(udp_hdr->dst_port);
    
            data_ptr = pkt_ptr + data_offset;
            rte_memcpy(&pkt_type,data_ptr,sizeof(uint8_t));
            data_ptr += sizeof(uint8_t);
            
            if(likely(pkt_type == RR) )
            {
                while(retry < 2000){
                    if(rte_ring_sp_enqueue(in_bufring[conn_id], rx_buffers[i]) >= 0){
                        break;
                    }
                    retry++;
                }
                
                retry=0;
            
            }
            else if (unlikely(pkt_type == SM))
            {
                LOG_DEBUG("Session Management Packet received pkt type 0x%2x",pkt_type);
                Marshal * sm_req = new Marshal();
                uint8_t req_type;
            
                rte_memcpy(&req_type,data_ptr,sizeof(uint8_t));
                data_ptr+=sizeof(uint8_t);
                if(req_type == CON_ACK){
                    if(dpdk_th->connections.find(conn_id) != dpdk_th->connections.end()){
                        dpdk_th->connections[conn_id]->connected_ = true;
                    
                    }
                    else{
                            LOG_DEBUG("Connection not found connid: %lu , thread_id rx-%d",conn_id,info->thread_id);
                    }
                }else{
                     uint16_t pkt_size = ntohs(udp_hdr->dgram_len) -  sizeof(rte_udp_hdr);
                    LOG_DEBUG("Connection request from %s", ipv4_to_string(src_ip).c_str());
                    std::string src_addr = ipv4_to_string(src_ip) + ":" + std::to_string(ntohs(udp_hdr->src_port));
                    *(sm_req)<<req_type;
                    *(sm_req)<<src_addr;
                    dpdk_th->sm_queue_l.lock();
                    dpdk_th->sm_queue.push(sm_req);
                    dpdk_th->sm_queue_l.unlock();
                }
               rte_pktmbuf_free(rx_buffers[i]);
            
            }
            else
            {
                LOG_DEBUG("Packet Type Not found");
                rte_pktmbuf_free(rx_buffers[i]);
            }
        }
        // #ifdef RPC_STATISTICS
        //     if((times % 30000) == 1){
        //         end = raw_time();
        //         sum += (end - start);

        //         count++;
        //         Log_info("Avg Time spent (ns)  = %llu, sum = %llu", ((sum/count) ), end - start );
        //     }
        //     times++;
        // #endif
    }
    Log_info("Exiting RX thread %d ", info->thread_id);
    return 0;
}


void dpdk_thread_info::init(DpdkTransport* th, int th_id, int p_id, 
                                        int q_id, int burst_size) {
    t_layer = th;
    thread_id = th_id;
    port_id = p_id;
    queue_id = q_id;
    max_size = burst_size;
    buf = new struct rte_mbuf*[burst_size];
    deque_bufs = new struct rte_mbuf*[burst_size];
    for(int i=0;i<burst_size;i++){
        deque_bufs[i] = (rte_mbuf*)rte_malloc("deque_objs", sizeof(struct rte_mbuf), 0);
    }
}


int dpdk_thread_info::buf_alloc(struct rte_mempool* mbuf_pool) {
    int retval = rte_pktmbuf_alloc_bulk(mbuf_pool, buf, max_size);
    return retval;
}


} // namespace rrr
