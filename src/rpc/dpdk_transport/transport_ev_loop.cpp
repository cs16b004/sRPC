#include <cstdint>
#include "transport.hpp"
#include <rte_ring.h>
#include <rte_ring_core.h>
#include "utils.hpp"
#include "../polling.hpp"
#include "../client.hpp"
#include "../server.hpp"

namespace rrr
{
    uint64_t cid[100] = {0};
    static uint8_t con_req[64] = {CON, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

    void DpdkTransport::reg_us_server(UDPServer *sv)
    {
        us_server = sv;
        //  for(std::pair<i32,std::function<void(Request<TransportMarshal>*, ServerConnection*)>>  rpc_handler: us_server->us_handlers_ ){

        //     handlers[rpc_handler.first] = new USHWrapper(rpc_handler.first, rpc_handler.second);
        //     LOG_DEBUG("Registering : rpc against %d", rpc_handler.first);
        //  }
    }
    void DpdkTransport::accept(uint64_t conn_id)
    {
        // RPCConfig *conf = rrr::RPCConfig::get_config();

        if (us_server == nullptr)
            return;
         conn_th_lock.lock();
        TransportConnection *oconn = new TransportConnection(num_threads_);

        uint64_t port = 0xFFFF;
        port = port << 16;
        uint64_t server_ip = 0xFFFFFFFF;
        server_ip = server_ip << 32;
        // std::cout<<"bitmask: "<<server_ip<<std::endl;
        server_ip = (server_ip & conn_id);
        server_ip = server_ip >> 32;

        port = port & conn_id;
        port = port >> 16;

        // port = ntohs(port);
        // server_ip = ntohl(server_ip);

        oconn->src_addr = src_addr_[config_->host_name_];
        //  oconn->src_addr.port = (u_port_counter.next());
        LOG_DEBUG("Accepting: %s", ConnToString(conn_id).c_str());
        // LOG_DEBUG("ConnId: ")
        oconn->out_addr = NetAddress(getMacFromIp(server_ip), server_ip, ntohs(port));
     
        oconn->udp_port = s_port_counter.next();

        oconn->conn_id = conn_id;

        oconn->conn_id = oconn->conn_id & (0xFFFFFFFFFFFFULL << 16);
        oconn->conn_id = oconn->conn_id | rte_cpu_to_be_16(oconn->udp_port);
        
        // choose a connection based on round robin principle;
       

        uint16_t chosen_thread = (next_thread_.next() % (config_->num_threads_));
        oconn->chosen_thread = chosen_thread;
        
        if (accepted.find(conn_id) != accepted.end())
        {
            LOG_DEBUG("Already Accepted %s !!", ConnToString(conn_id).c_str());
           
            send_ack(out_connections.find(accepted.find(conn_id)->second)->second);
            delete oconn;
            conn_th_lock.unlock();
            return;
        }
        oconn->assign_bufring();
        oconn->pkt_mempool = tx_mbuf_pool[chosen_thread];
        oconn->all_pools = tx_mbuf_pool;
        // oconn->buf_alloc(tx_mbuf_pool[chosen_thread], config_->buffer_len);
        // oconn->assign_availring();
        // oconn->make_headers_and_produce();
        // oconn->pkt_mempool =

        UDPConnection *n_conn[num_threads_ + 1];
        for (int i = 0; i <= num_threads_; i++)
        {
            n_conn[i] = new UDPConnection(us_server, oconn->conn_id);
        }
        
        out_connections[oconn->conn_id]  = oconn;
       
        us_server->sconns_l_.lock();
        // Copies of Server Connection for parallely running the rpc;
        for (int i = 0; i <= num_threads_; i++)
            n_conn[i]->conn = oconn;
        // First copy goes to poll mgr and upper layers;
        us_server->sconns_.insert(n_conn[0]);
        us_server->pollmgr_->add(n_conn[0]);
        // Other copies are added to connections sonn list used by event loop to run the request in parallel
        for (int i = 1; i <= num_threads_; i++)
            oconn->sconn[i - 1] = n_conn[i];
        us_server->sconns_l_.unlock();
        send_ack(oconn);
        
        int send_all = 0;
        int retry=0;
        while (send_all < num_threads_)
        {
            while ((rte_ring_enqueue(sm_rings[send_all], (void *)oconn)) < 0)
            {
                retry++;
                if(retry > 1000*1000){
                    Log_warn("Stuck while accpeting connection %s, thread %d blocked", ConnToString(oconn->conn_id).c_str(), rte_lcore_id());
                    retry=0;
                }
            }

            send_all++;
        }
        oconn->burst_size=32;
        while(accepted.insert({conn_id, oconn->conn_id}).second ==false )
        ;
       LOG_DEBUG("Accepted: %s, chosen thread %d", ConnToString(oconn->conn_id).c_str(),oconn->chosen_thread);
        conn_counter.next();
        conn_th_lock.unlock();

    }
    void DpdkTransport::send_ack(TransportConnection *oconn)
    {

        oconn->burst_size = 1;
        uint8_t con_ack[64] = {CON_ACK, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                               0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                               0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                               0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                               0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                               0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                               0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                               0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

        // this->connections_[conn_id] = conn;

     
        *((uint16_t *)(con_ack + 1)) = oconn->udp_port;
        TransportMarshal accept_marshal = TransportMarshal(oconn->get_new_pkt());
        rte_udp_hdr *sm_hdr = rte_pktmbuf_mtod_offset(accept_marshal.get_mbuf(), rte_udp_hdr *, udp_hdr_offset);
        sm_hdr->src_port = htons(oconn->src_addr.port);

        accept_marshal.set_pkt_type_sm();
        accept_marshal.write(con_ack, 64);

        accept_marshal.format_header();
        rte_mbuf* pkt = accept_marshal.get_mbuf();
        LOG_DEBUG("Ack: %s, c_th %d,  pkt %s", ConnToString(oconn->conn_id).c_str(),oconn->chosen_thread, accept_marshal.print_request().c_str());
        while (rte_eth_tx_burst(0, 0, &pkt, 1 ) < 1)
            ;
       // LOG_DEBUG("Enqued Accept Ack in %d,: %s, size %d", oconn->chosen_thread, oconn->out_bufring[oconn->chosen_thread]->name, rte_ring_count(oconn->out_bufring[oconn->chosen_thread]));
        
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
        TransportConnection *oconn = new TransportConnection(num_threads_);

        LOG_DEBUG("Connecting to a server %s , %s::%d", addr.c_str(), ipv4_to_string(server_ip).c_str(), port);
        // LOG_DEBUG("Mac: %s",getMacFromIp(server_ip).c_str());
        oconn->src_addr = src_addr_[config_->host_name_];
        oconn->out_addr = NetAddress(getMacFromIp(server_ip), server_ip, port);

        uint64_t conn_id = 0;
        conn_th_lock.lock();

        uint16_t chosen_thread = (next_thread_.next() % (config_->num_threads_));
        oconn->chosen_thread = chosen_thread;

        oconn->udp_port = u_port_counter.next();
        oconn->src_addr.port = oconn->udp_port;

        conn_counter.next();
        conn_id = conn_id | oconn->out_addr.ip; // server ip in BE
        conn_id = conn_id << 16;
        conn_id = conn_id | rte_cpu_to_be_16(port); // server port in BE
        conn_id = conn_id << 16;
        conn_id = conn_id | rte_cpu_to_be_16(oconn->udp_port); // local host port in BE
        Log_info("Chosen thread for new conn: %llu is  %d", conn_id, chosen_thread);
        oconn->conn_id = conn_id;
        oconn->assign_bufring();
        oconn->pkt_mempool = tx_mbuf_pool[chosen_thread];
        oconn->all_pools = tx_mbuf_pool;
        out_connections[conn_id] = oconn;
       

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
        Log_info("Sending Connection request %s", con_marshal.print_request().c_str());
        while ( rte_eth_tx_burst(0,0, &pkt,1) < 1)
        {
            wait++;
            if (wait > 100 * 1000)
            {
                Log_warn("Unable to send connection request packet: %llu", oconn->conn_id);
                wait = 0;
            }
        }
        wait = 0;
        while (!oconn->connected_)
        {
            usleep(
        500*1000);
            wait++;
            if (wait > 2)
            {
                Log_warn("Waiting for connection Request Ack %s", ConnToString(conn_id).c_str());

                wait = 0;
                goto send_con_req;
            }
        }
        auto nh = out_connections.extract(conn_id);
        conn_id = conn_id & ((0xFFFFFFFF0000FFFFULL));
        conn_id = conn_id | ((uint64_t)htons(oconn->out_addr.port) << 16);
        oconn->conn_id = conn_id;
        nh.key() = conn_id;
        out_connections.insert(std::move(nh));

        oconn->burst_size = conf->burst_size;
        ;
         int send_all = 0;
        while (send_all < num_threads_)
        {
            while ((rte_ring_mp_enqueue(sm_rings[send_all], (void *)oconn)) < 0)
            {
                ;
            }
            send_all++;
        }
        Log_info("Connected to %s", ConnToString(oconn->conn_id).c_str());
        usleep(5000);
        conn_th_lock.unlock();
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
                LOG_DEBUG("Found the Mac for IP: %s, MAC: %s", ipv4_to_string(ip).c_str(), mac_to_string(it.mac).c_str());
                return (it.mac);
            }
        }
        Log_error("Error: No mac found for the IP %s", ipv4_to_string(ip).c_str());
        return nullptr;
    }

    int DpdkTransport::ev_loop(void *arg)
    {
        d_thread_ctx *ctx = reinterpret_cast<d_thread_ctx *>(arg);
        RPCConfig *conf = RPCConfig::get_config();
        DpdkTransport *t_layer = ctx->t_layer;

        // wait for us space to be made available by the application

        Log_info("Launching even loop %d on lcore: %d",
                 ctx->thread_id, rte_lcore_id());

        while (!ctx->shutdown)
        {

             
            cid[ctx->thread_id]++;
            ctx->nb_rx = rte_eth_rx_burst(ctx->port_id, ctx->queue_id,
                                          ctx->rx_bufs, conf->burst_size);
#ifdef RPC_STATISTICS
            ctx->rx_pkts += ctx->nb_rx;
#endif
            t_layer->process_requests(ctx);
            t_layer->process_sm_req(ctx);
            // transmit
            t_layer->do_transmit(ctx);
          
            t_layer->do_poll_job(ctx);
        }
        Log_info("Exiting EV thread %d, num pkts sent: %lu, num pkts received: %lu dropped_pakcets: %lu",
                 ctx->thread_id, ctx->sent_pkts, ctx->rx_pkts, ctx->dropped_packets);
        return 0;
    }

    void DpdkTransport::process_sm_req(d_thread_ctx *ctx)
    {

        int nb_sm_reqs_ = 0;
        unsigned int available;

        if (unlikely(rte_ring_empty(ctx->sm_ring) == 0))
        {
            nb_sm_reqs_ = rte_ring_sc_dequeue_burst(ctx->sm_ring, (void **)ctx->conn_arr, 8, &available);
            for (int i = 0; i < nb_sm_reqs_; i++)
            {
                ctx->out_connections[ctx->conn_arr[i]->conn_id] = ctx->conn_arr[i]; // Put the connection in local conn_table
                while(ctx->t_conns.insert(ctx->conn_arr[i]).second == false);
                LOG_DEBUG("Added Connection %lu to thread %d, out_ring: %s", ctx->conn_arr[i]->conn_id, ctx->thread_id, ctx->conn_arr[i]->out_bufring[ctx->thread_id]->name);
            }
            //if (nb_sm_reqs_ > 0 && ctx->thread_id == 8) 
            // for (TransportConnection* current_conn : ctx->t_conns)
            // {
                
            //     // rte_ring_count(current_conn->out_bufring[ctx->thread_id]))
            //         LOG_DEBUG("thread %d, %p, ts %lu conn %s RING %s elems %d", ctx->thread_id, ctx, cid, ConnToString(current_conn->conn_id).c_str(), current_conn->out_bufring[ctx->thread_id]->name, rte_ring_count(current_conn->out_bufring[ctx->thread_id]));
            // }
            // if(ctx->thread_id == 8 )

            // for (int i = 0; i < nb_sm_reqs_; i++)
            // {
            //     ctx->conn_arr[conn_counter.next()] = conn_arr[i]; // Put the connection in local conn_table
            //     ctx->max_conn = conn_counter.peek_next();
            //     LOG_DEBUG("Added Connection %lu to thread %d", conn_arr[i]->conn_id, ctx->thread_id);
            // }
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
        rte_be32_t host_ip = ctx->t_layer->host_ip;
        rte_udp_hdr *udp_hdr;
        rte_mbuf **rx_buffers = ctx->rx_bufs;

        rte_mbuf *rpc_pkt = nullptr;

        i32 req_size = 0;
        i64 req_xid;
        i32 req_rpc_id;

        uint8_t pkt_type;
        uint16_t net_type;

        if (unlikely(ctx->nb_rx == 0))
            return;
        // LOG_DEBUG("Packets received %d", ctx->nb_rx);
        ctx->nb_tx = 0;
        Request<TransportMarshal> *req_m = new Request<TransportMarshal>();
        for (int i = 0; i < ctx->nb_rx; i++)
        {
            rte_prefetch0(rx_buffers[i]);

            pkt_ptr = rte_pktmbuf_mtod(rx_buffers[i], uint8_t *);
            ip_hdr = ((rte_ipv4_hdr *)(pkt_ptr + ip_hdr_offset));
            src_ip = ip_hdr->src_addr;
            udp_hdr = ((rte_udp_hdr *)(pkt_ptr + udp_hdr_offset));
            // if(unlikely(ip_hdr->dst_addr != host_ip)){
            //     rte_pktmbuf_free(rx_buffers[i]);
            //     continue;
            // }

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

            // net_type = *((uint16_t*)data_ptr);
            // LOG_DEBUG("PKT TYPE 0x%x", net_type);
            // if(unlikely(net_type != (uint16_t)0xfeed) ){
            //     rte_pktmbuf_free(rx_buffers[i]);
            //     continue;
            // }
            // data_ptr+= sizeof(uint16_t);

            pkt_type = *data_ptr;
            // mempcpy
            // pkt_type = *((uint8_t*)data_ptr);

            data_ptr += sizeof(uint8_t);
           // LOG_DEBUG("Pkt type %02x from %s::%d", pkt_type, ipv4_to_string(src_ip).c_str(), ntohs(udp_hdr->src_port));
            if (likely(pkt_type >= RR)) // Run here or submit to bg thread
            {
                std::unordered_map<uint64_t, TransportConnection*>::iterator ft = ctx->out_connections.find(conn_id);
                if (ft == ctx->out_connections.end())
                {
                    rte_pktmbuf_free(rx_buffers[i]);
                    ctx->dropped_packets++;
                    LOG_DEBUG("Dropping pakcet for Conn Id : %s", ConnToString(conn_id).c_str());
                    continue;
                }
                else
                {
                    if (pkt_type == RR)
                    {

                        req_m->m.allot_buffer_x(rx_buffers[i]);

                        req_m->m >> req_size >> req_m->xid;
                        req_m->m >> req_rpc_id;

                        auto it = us_server->us_handlers_.find(req_rpc_id);
                        if(it == us_server->us_handlers_.end())
                            LOG_DEBUG("Dropping packet be because handler for  0x%x not found", req_rpc_id);
                        it->second(req_m, (UDPConnection *)(ft->second->sconn[ctx->thread_id]));
                        swap_udp_addresses(rx_buffers[i]);
                        ctx->tx_bufs[ctx->nb_tx] = rx_buffers[i];
                        ctx->nb_tx++;
                    }
                    else if ((pkt_type == RR_BG))
                    {
                        // background
                        // rte_pktmbuf_free(rx_buffers[i]);
                        int ret = 0;
                        while (rte_ring_sp_enqueue(ft->second->in_bufring[ctx->thread_id], (void *)rx_buffers[i]) < 0)
                        {
                            ret++;
                            if (ret > 1000 * 1000)
                            {
                                LOG_DEBUG("Cannot enqueue moving to next packet");
                                rte_pktmbuf_free(rx_buffers[i]);
                                ctx->dropped_packets++;
                                break;
                            }
                        }
                    }
                }
            }
            else if (unlikely(pkt_type == SM))
            {
                LOG_DEBUG("Session Management Packet received pkt type 0x%2x", pkt_type);
                uint8_t req_type;

                rte_memcpy(&req_type, data_ptr, sizeof(uint8_t));
                data_ptr += sizeof(uint8_t);
                LOG_DEBUG("Req Type 0x%2x, conn : %s",req_type, ConnToString(conn_id).c_str());
                switch (req_type)
                {
                case CON_ACK:
                {
                    std::unordered_map<uint64_t, TransportConnection*>::iterator it = out_connections.find(conn_id);
                    if ( it != out_connections.end())
                    {
                        it->second->out_addr.port = *((uint16_t *)data_ptr);
                        it->second->connected_ = true;
                    }
                    else
                    {
                        LOG_DEBUG("Connection not found conn: %s , thread_id ev-%d", ConnToString(conn_id).c_str(), ctx->thread_id);
                    }
                    break;
                }
                case CON:
                {
                   // LOG_DEBUG("Connection request from %s accepting at %d", ipv4_to_string(src_ip).c_str(), ctx->thread_id);
                    accept(conn_id);
                    break;
                }
                default:
                    break;
                }
            }
            else
            {
                LOG_DEBUG("Packet Type Not found");
                ctx->dropped_packets++;
            }
        }
        delete req_m;
    }
    void DpdkTransport::do_transmit(d_thread_ctx *ctx)
    {

        // send single thread request;
        uint16_t prev_packets = 0;
        uint16_t sent = 0;

        while (ctx->nb_tx > 0)
        {
            sent = rte_eth_tx_burst(ctx->port_id, ctx->queue_id, &(ctx->tx_bufs[prev_packets]), ctx->nb_tx);
            prev_packets += sent;
            // #ifdef LOG_LEVEL_AS_DEBUG
            //             for(int j=0;j<ctx->nb_tx;j++){
            //                 uint8_t* pkt_ptr = rte_pktmbuf_mtod(ctx->tx_bufs[j], uint8_t *);
            //                 struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(ctx->tx_bufs[0], struct rte_ether_hdr *);
            //                 rte_ipv4_hdr* ip_hdr = ((rte_ipv4_hdr *)(pkt_ptr + ip_hdr_offset));

            //                 rte_udp_hdr* udp_hdr = ((rte_udp_hdr *)(pkt_ptr + udp_hdr_offset));
            //                 LOG_DEBUG("Sent %d packets to: %s::%d, size: %d",  prev_packets,
            //                                 ipv4_to_string(ip_hdr->dst_addr).c_str(), ntohs(udp_hdr->dst_port), ntohs(udp_hdr->dgram_len));
            //             }

            // #endif
            // if(ctx->nb_tx < sent)
            //     Log_warn("!! nb_tx %d, ctx %d prev packcets %d", ctx->nb_tx, ctx->thread_id, prev_packets);
            ctx->nb_tx -= sent;
        }
// rte_pktmbuf_free_bulk(ctx->tx_bufs, prev_packets);
//  slow path send background requests
#ifdef RPC_STATISTICS
        ctx->sent_pkts += prev_packets;
#endif

        unsigned int available;
        int nb_pkts = 0;
        int ret = 0;
        int retry_count = 0;
       
       
        rte_ring *current_ring;
        std::unordered_set<TransportConnection*>::iterator it;
        for (it = ctx->t_conns.begin(); it != ctx->t_conns.end(); ++it)
        {
            nb_pkts = rte_ring_sc_dequeue_burst((*it)->out_bufring[ctx->thread_id], (void **)ctx->tx_bufs, 32, &available);

            if (nb_pkts <= 0)
                return;

            ret = rte_eth_tx_burst(ctx->port_id, ctx->queue_id, ctx->tx_bufs, nb_pkts);

            if (unlikely(ret < 0))
                rte_panic("Can't send packets from connection %lu\n", (*it)->conn_id);
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
#ifdef RPC_STATISTICS
            ctx->sent_pkts += nb_pkts;
#endif
        }
    }

    void DpdkTransport::do_poll_job(d_thread_ctx *ctx)
    {

        uint16_t nb_polls;
        unsigned int available;

        nb_polls = rte_ring_sc_dequeue_burst(ctx->poll_req_q, (void **)ctx->poll_reqs, 8, &available);
        for (int i = 0; i < nb_polls; i++)
        {
            LOG_DEBUG("Client added poll job %p", ctx->poll_reqs[i]);
            while(ctx->poll_jobs.insert((Pollable *)(ctx->poll_reqs[i])).second == false);
        }
       
       std::unordered_set<Pollable*>::iterator it = ctx->poll_jobs.begin();
        for(;it != ctx->poll_jobs.end(); ++it){
            (*it)->handle_read();
        }
    }

}