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

#define MAX_PATTERN_NUM 3
#define MAX_ACTION_NUM 2
#define DPDK_RX_WRITEBACK_THRESH 64

namespace rrr
{

    static uint8_t con_req[64] = {CON, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
    static uint8_t con_ack[64] = {CON_ACK, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

    void DpdkTransport::accept(uint64_t conn_id)
    {
        // RPCConfig *conf = rrr::RPCConfig::get_config();

        TransportConnection *oconn = new TransportConnection();
        uint64_t port = 0xFFFF;
        port = port<<16;
        uint64_t server_ip = 0xFFFFFFFF;
        server_ip = server_ip<<32;
       // std::cout<<"bitmask: "<<server_ip<<std::endl;
        server_ip = (server_ip & conn_id);
        server_ip = server_ip>>32;
        

        port = port & conn_id;
        port = port>>16;
       // port = ntohs(port);
       // server_ip = ntohl(server_ip);
        LOG_DEBUG("Accepting: %llu ip %s, port %d", server_ip, ipv4_to_string(server_ip).c_str(), ntohs(port));

        oconn->src_addr = src_addr_[config_->host_name_];
        oconn->out_addr = NetAddress(getMacFromIp(server_ip), server_ip, ntohs(port));
        oconn->udp_port = src_addr_[config_->host_name_].port;
        // choose a connection based on round robin principle;
        conn_th_lock.lock();

        uint16_t chosen_thread = (next_thread_.next()  % (config_->num_threads_));

        conn_counter.next();

        if (out_connections.find(conn_id) != out_connections.end()){
            oconn = out_connections[conn_id];
            goto send_ack;
        }
        // thread_ctx_arr[chosen_thread]->conn_lock.unlock();
        // return 0;

        Log_info("Chosen threads for new conn: %lu, thread %d", conn_id, chosen_thread);

        oconn->conn_id = conn_id;
        oconn->assign_bufring();
        oconn->pkt_mempool = tx_mbuf_pool[chosen_thread];
        oconn->buf_alloc(tx_mbuf_pool[chosen_thread], config_->buffer_len);
        oconn->assign_availring();
        oconn->make_headers_and_produce();
        out_connections[conn_id] = oconn;
        

        while (rte_ring_sp_enqueue(sm_rings[chosen_thread], (void *)oconn) < 0)
            ;

    send_ack:
        oconn->burst_size = 1;
        conn_th_lock.unlock();
        // this->connections_[conn_id] = conn;

        int wait = 0;

        TransportMarshal accept_marshal = TransportMarshal(oconn->get_new_pkt());
        accept_marshal.set_pkt_type_sm();
        accept_marshal.write(con_ack, 64);
        accept_marshal.format_header();
       // LOG_DEBUG("Sending Ack to: %s:%d ", ipv4_to_string(oconn->out_addr.ip).c_str(), oconn->out_addr.port);
        while (rte_ring_enqueue(oconn->out_bufring, (void *)accept_marshal.get_mbuf()) < 0)
            ;
        wait = 0;

        sleep(1);
        oconn->connected_ = true;
        oconn->burst_size = 32; // conf->client_batch_size_;
        return;
    }

    uint64_t DpdkTransport::connect(const char *addr_str)
    {
        RPCConfig *conf = rrr::RPCConfig::get_config();
        while (!initiated)
        {
            LOG_DEBUG("Waiting for initialization");
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
        uint32_t server_ip = ipv4_from_str(addr.substr(0, idx).c_str());
        
        uint16_t port = atoi(addr.substr(idx + 1).c_str());

        // UDPConnection *conn = new UDPConnection(*s_addr);
        TransportConnection *oconn = new TransportConnection();

        LOG_DEBUG("Connecting to a server %s , %s::%d", addr.c_str(), ipv4_to_string(server_ip).c_str(), port );
        // LOG_DEBUG("Mac: %s",getMacFromIp(server_ip).c_str());
        oconn->src_addr = src_addr_[config_->host_name_];
        oconn->out_addr = NetAddress(getMacFromIp(server_ip), server_ip, port);

        uint64_t conn_id = 0;
        conn_th_lock.lock();


        uint16_t chosen_thread = (next_thread_.next()  % (config_->num_threads_));

        oconn->udp_port = u_port_counter.next();
        oconn->src_addr.port = oconn->udp_port;
       // addr = addr + "::" + std::to_string(oconn->udp_port);

        conn_counter.next();
        conn_id = conn_id | oconn->out_addr.ip; // server ip in BE
        conn_id = conn_id << 16;
        conn_id = conn_id | rte_cpu_to_be_16(port); // server port in BE
        conn_id = conn_id << 16;
        conn_id = conn_id | rte_cpu_to_be_16(oconn->udp_port); // local host port in BE
        Log_info("Chosen thread for new conn: %llu is  %d", conn_id, chosen_thread);
        out_connections[conn_id] = oconn;
        
        // int ret;
        // ret = rte_hash_add_key_data(conn_table, &conn_id, oconn);
        // if(ret < 0){
        //   Log_error("Error in connecting to %s, entry cannot be created in conn table", addr_str);
        //   return 0;
        //}
        // ret = rte_hash_lookup(conn_table,&conn_id);
        oconn->conn_id = conn_id;
        oconn->assign_bufring();
        oconn->pkt_mempool = tx_mbuf_pool[chosen_thread];
        oconn->buf_alloc(tx_mbuf_pool[chosen_thread], conf->buffer_len);
        oconn->assign_availring();
        oconn->make_headers_and_produce();
        
        while ((rte_ring_enqueue(sm_rings[chosen_thread], (void *)oconn)) < 0)
        {
            ;
        }

        conn_th_lock.unlock();
        // this->connections_[conn_id] = conn;
        send_con_req:
        rte_mbuf *t = oconn->get_new_pkt();
        uint8_t *dst = rte_pktmbuf_mtod(t, uint8_t *);

        TransportMarshal con_marshal(oconn->get_new_pkt());
       
        con_marshal.set_pkt_type_sm();
        con_marshal.write(con_req, 64);
        con_marshal.format_header();
        rte_mbuf *pkt = con_marshal.get_mbuf();
    
        int wait = 0;
        LOG_DEBUG("Sending Connection request");
        while (rte_ring_sp_enqueue(oconn->out_bufring, (void *)con_marshal.get_mbuf()) < 0)
        {
            wait++;
            if (wait > 100 * 1000)
            {
                Log_warn("Unable to enque connection request packet: %llu", oconn->conn_id);
                wait = 0;
            }
        }
        wait = 0;
        while (!oconn->connected_)
        {
            usleep(500 * 1000);
            wait++;
            if (wait > 20)
            {
                Log_warn("Waiting for connection Request Ack %llu", conn_id);

                wait = 0;
                goto send_con_req;
            }
        }
        oconn->burst_size = conf->client_batch_size_;
        ;
        Log_info("Connected to %s", addr.c_str());

        return conn_id;
    }

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

    // Used in in send method to create a packet
    uint8_t *DpdkTransport::getMacFromIp(uint32_t ip)
    {
        for (rrr::RPCConfig::NetworkInfo it : config_->get_net_info())
        {
            if (it.ip == ip)
            {
                 LOG_DEBUG("Found the Mac for IP: %s, MAC: %s",ipv4_to_string(ip).c_str(), mac_to_string( it.mac).c_str());
                return (it.mac);
            }
        }
        Log_error("Error: No mac found for the IP %s", ipv4_to_string(ip).c_str());
        return nullptr;
    }

    inline uint16_t process_sm_requests(rte_ring *sm_ring, rte_mempool *mempool)
    {

        return 0;
    }
    std::string out_string = "|aaaaaa||aaaaaa||aaaaaa||aaaaaa||aaaaaa||aaaaaa||aaaaaa||aaaaaa|";
    uint64_t count = 0;

    void add_bench(std::string in, std::string *out)
    {
        // out->append(out_string);
        count++;
    }
    
    static void inline swap_udp_addresses(struct rte_mbuf *pkt)
    {
        // Extract Ethernet header
        struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
        uint8_t* pkt_ptr = rte_pktmbuf_mtod(pkt, uint8_t* );
        struct rte_ether_addr temp = eth_hdr->src_addr;
        eth_hdr->src_addr = eth_hdr->dst_addr;
        eth_hdr->dst_addr = temp;

        // Extract IP header
        struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(pkt_ptr + ip_hdr_offset);

        // Extract UDP header
        struct rte_udp_hdr *udp_hdr = (struct rte_udp_hdr *)(pkt_ptr + udp_hdr_offset );

        // Swap IP addresses
        uint32_t tmp_ip = ip_hdr->src_addr;
        ip_hdr->src_addr = ip_hdr->dst_addr;
        ip_hdr->dst_addr = tmp_ip;

        // Swap UDP port numbers
        uint16_t tmp_port = udp_hdr->src_port;
        udp_hdr->src_port = udp_hdr->dst_port;
        udp_hdr->dst_port = tmp_port;
        
        
    }

    void bench_rpc_handler(TransportMarshal *req, TransportConnection *conn)
    {

        // LOG_DEBUG("PRINT REQ %s", req->print_request().c_str());

        i32 v_error_code = 0;
        i64 v_reply_xid = 0;

        std::string in_0;
        (*req) >> in_0;

#ifdef LATENCY
        uint64_t ts;
        (*req) >> ts;
#endif
        std::string out_0;
        add_bench(in_0, &out_0);
        swap_udp_addresses(req->get_mbuf());
        TransportMarshal current_reply;
   
      

        current_reply.allot_buffer_x(req->get_mbuf());
        current_reply.set_book_mark(sizeof(i32)); // will write reply size later

        current_reply << v_reply_xid;
        current_reply << v_error_code;
        current_reply << out_string;
#ifdef LATENCY
        current_reply << ts;
#endif;
        i32 reply_size = current_reply.content_size();
        current_reply.write_book_mark(&reply_size, sizeof(i32));

        current_reply.format_header();
        current_reply.set_pkt_type_bg();
        
        // LOG_DEBUG( "%s", current_reply.print_request().c_str());
        int retry = 0;
    }

    int DpdkTransport::ev_loop(void *arg)
    {
        d_thread_ctx *ctx = reinterpret_cast<d_thread_ctx *>(arg);
        RPCConfig *conf = RPCConfig::get_config();
        DpdkTransport *t_layer = ctx->t_layer;
        Log_info("Launching even loop %d on lcore: %d",
                 ctx->thread_id, rte_lcore_id());

        while (!ctx->shutdown)
        {

            ctx->nb_rx = rte_eth_rx_burst(ctx->port_id, ctx->queue_id,
                                          ctx->rx_bufs, conf->burst_size);

            t_layer->process_requests(ctx);
            t_layer->process_sm_req(ctx);
            t_layer->do_transmit(ctx);
            // transmit
        }
        Log_info("Exiting RX thread %d ",
                 ctx->thread_id);
        return 0;
    }

    void DpdkTransport::process_sm_req(d_thread_ctx *ctx)
    {

        int nb_sm_reqs_ = 0;
        unsigned int available;
        TransportConnection **conn_arr = new TransportConnection *[8];
        if (unlikely(rte_ring_empty(ctx->sm_ring) == 0))
        {
            nb_sm_reqs_ = rte_ring_sc_dequeue_burst(ctx->sm_ring, (void **)conn_arr, 8, &available);
            for (int i = 0; i < nb_sm_reqs_; i++)
            {
                ctx->out_connections[conn_arr[i]->conn_id] = conn_arr[i]; // Put the connection in local conn_table
                LOG_DEBUG("Added Connection %lu to thread %d", conn_arr[i]->conn_id, ctx->thread_id);
            }
        }
    }

    void DpdkTransport::process_requests(d_thread_ctx *ctx)
    {

        uint8_t *pkt_ptr;
        uint8_t *data_ptr;
        uint16_t pkt_size;
        uint64_t conn_id;
        rte_ipv4_hdr *ip_hdr;
        uint32_t src_ip;
        rte_udp_hdr *udp_hdr;
        rte_mbuf **rx_buffers = ctx->rx_bufs;

        rte_mbuf *rpc_pkt = nullptr;
        TransportMarshal req_m;
        std::unordered_map<i32, std::function<void(rrr::TransportMarshal *, TransportConnection *)>> us_handlers_;
        us_handlers_[0x10000003] = bench_rpc_handler;
        i32 req_size = 0;
        i64 req_xid;
        i32 req_rpc_id;

        uint8_t pkt_type;

        if (unlikely(ctx->nb_rx == 0))
            return;
       // LOG_DEBUG("Packets received %d", ctx->nb_rx);
        ctx->nb_tx=0;
        for (int i = 0; i < ctx->nb_rx; i++)
        {
            rte_prefetch0(rx_buffers[i]);
            
            pkt_ptr = rte_pktmbuf_mtod(rx_buffers[i], uint8_t *);
            ip_hdr = ((rte_ipv4_hdr *)(pkt_ptr + ip_hdr_offset));
            src_ip = ip_hdr->src_addr;
            udp_hdr = ((rte_udp_hdr *)(pkt_ptr + udp_hdr_offset));

            pkt_size = ntohs(udp_hdr->dgram_len) - sizeof(rte_udp_hdr);
            conn_id = 0;
            conn_id = src_ip;
            conn_id = conn_id << 16;
            // server port in BE
            conn_id = conn_id | (uint64_t)(udp_hdr->src_port);
            // local host port in BE
            conn_id = conn_id << 16;
            conn_id = conn_id | (uint64_t)(udp_hdr->dst_port);

            data_ptr = pkt_ptr + data_offset;
            pkt_type = *data_ptr;
            // mempcpy
            // pkt_type = *((uint8_t*)data_ptr);

            data_ptr += sizeof(uint8_t);
           // LOG_DEBUG("Pkt type %02x from %s::%d",pkt_type, ipv4_to_string(src_ip).c_str(), ntohs(udp_hdr->src_port));
            if ( likely(pkt_type >= RR)) // Run here or submit to bg thread
            {
                if (pkt_type == RR)
                {

                    
                    req_m.allot_buffer_x(rx_buffers[i]);

                    req_m >> req_size >> req_xid;
                    req_m >> req_rpc_id;
                   
                    us_handlers_[req_rpc_id](&req_m, out_connections[conn_id]);
                    
                    ctx->tx_bufs[ctx->nb_tx] = rx_buffers[i];
                    ctx->nb_tx+=1;
                }
                else if(unlikely( pkt_type == RR_BG))
                {
                    // background
                    //LOG_DEBUG("Back Ground request");
                    rte_ring_enqueue(out_connections[conn_id]->in_bufring, (void *)rx_buffers[i]);
                }
            }
            else if (unlikely(pkt_type == SM))
            {
                LOG_DEBUG("Session Management Packet received pkt type 0x%2x", pkt_type);
                Marshal *sm_req = new Marshal();
                uint8_t req_type;

                mempcpy(&req_type, data_ptr, sizeof(uint8_t));
                data_ptr += sizeof(uint8_t);
                // LOG_DEBUG("Req Type 0x%2x, src_addr : %s",req_type, src_addr.c_str());
                switch (req_type)
                {
                case CON_ACK:
                {
                    if (out_connections.find(conn_id) != out_connections.end())
                    {
                        out_connections[conn_id]->connected_ = true;
                    }
                    else
                    {
                        LOG_DEBUG("Connection not found connid: %lu , thread_id rx-%d", conn_id, ctx->thread_id);
                    }
                    break;
                }
                case CON:
                {
                    LOG_DEBUG("Connection request from %s", ipv4_to_string(src_ip).c_str());
                    accept(conn_id);
                    break;
                }
                default: break;
                
                }
            }
            else
            {
                LOG_DEBUG("Packet Type Not found");
            }
        }
    }
    void DpdkTransport::do_transmit(d_thread_ctx *ctx)
    {

        // send single thread request;
        int prev_packets = 0;
        while (ctx->nb_tx > 0)
        {
            prev_packets+= rte_eth_tx_burst(ctx->port_id, ctx->queue_id, &(ctx->tx_bufs[prev_packets]), ctx->nb_tx);
            #ifdef LOG_LEVEL_AS_DEBUG
            for(int j=0;j<ctx->nb_tx;j++){
                uint8_t* pkt_ptr = rte_pktmbuf_mtod(ctx->tx_bufs[j], uint8_t *);
                struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(ctx->tx_bufs[0], struct rte_ether_hdr *);
                rte_ipv4_hdr* ip_hdr = ((rte_ipv4_hdr *)(pkt_ptr + ip_hdr_offset));
                
                rte_udp_hdr* udp_hdr = ((rte_udp_hdr *)(pkt_ptr + udp_hdr_offset));
                LOG_DEBUG("Sent %d packets to: %s::%d, size: %d",  prev_packets,
                                ipv4_to_string(ip_hdr->dst_addr).c_str(), ntohs(udp_hdr->dst_port), ntohs(udp_hdr->dgram_len));
            }

            #endif
            ctx->nb_tx -= prev_packets;
        }
        // slow path send background requests

        unsigned int available;
        int nb_pkts = 0;
        int ret = 0;
        int retry_count = 0;
        for (auto conn_entry : ctx->out_connections)
        {
            TransportConnection *current_conn = conn_entry.second;
            //LOG_DEBUG("Num of pkts to deque %d, burst_size %d", rte_ring_count(current_conn->out_bufring), current_conn->burst_size);
            if (current_conn->out_bufring == nullptr )//|| rte_ring_count(current_conn->out_bufring) < current_conn->burst_size)
                continue;

            nb_pkts = rte_ring_sc_dequeue_burst(current_conn->out_bufring, (void **)ctx->tx_bufs, 32, &available);

            if (nb_pkts <= 0)
                return;

            ret = rte_eth_tx_burst(ctx->port_id, ctx->queue_id, ctx->tx_bufs, nb_pkts);
           //  LOG_DEBUG("NB pkts %d, sent %d, conn_id %lld", nb_pkts, ret, conn_entry.first);
            if (unlikely(ret < 0))
                rte_panic("Can't send packets from connection %llu\n", current_conn->conn_id);
            while (ret != nb_pkts)
            {
                ret += rte_eth_tx_burst(ctx->port_id, ctx->queue_id, &(ctx->tx_bufs[ret]), nb_pkts - ret);
                retry_count++;
                if (unlikely(retry_count == 1000000))
                {
                    Log_warn("stuck in rte_eth_tx_burst in port %u queue %u", ctx->port_id, ctx->queue_id);
                    retry_count = 0;
                }
            }
            retry_count = 0;
           // rte_pktmbuf_free_bulk(my_buffers,nb_pkts);

            while (rte_ring_sp_enqueue_bulk(current_conn->available_bufring, (void **)ctx->tx_bufs, nb_pkts, &available) == 0)
            {
                retry_count++;
                if (unlikely(retry_count == 1000000))
                {
                    Log_warn("stuck in avail_buffers enqueue for conn: %lld, free space in ring %d, ring size: %d", current_conn->conn_id, available, rte_ring_count(current_conn->available_bufring));
                    retry_count = 0;
                }
            }
        }
    }

}