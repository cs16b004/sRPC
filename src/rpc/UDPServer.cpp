#include "server.hpp"

#include <stdio.h>
#include <stdlib.h>

#include "dpdk_transport/config.hpp"
#include "dpdk_transport/transport_marshal.hpp"

using namespace std;

#ifdef RPC_STATISTICS
struct ring_stat
{
    uint64_t num_sample;
    uint64_t free_c;
    uint64_t used_c;

} *rs;

void add_sample(ring_stat *rs, uint64_t num_used, uint64_t num_free)
{
    rs->num_sample++;

    rs->free_c += num_free;
    rs->used_c += num_used;
}
static uint64_t raw_time(void)
{
    struct timespec tstart = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &tstart);
    uint64_t t = (uint64_t)(tstart.tv_sec * 1.0e9 + tstart.tv_nsec);
    return t;
}
#endif

namespace rrr
{

    UDPConnection::UDPConnection(UDPServer *server, uint64_t socket)
        : ServerConnection((Server *)server, 0), connId(socket)
    {
        conn = server->transport_->out_connections[socket];
        nr_inrings = server->transport_->num_threads_;
        // increase number of open connections
        us_handlers_.insert(server_->us_handlers_.begin(), server_->us_handlers_.end());
        server_->sconns_ctr_.next(1);
        for (int i = 0; i < 64; i++)
        {
            pkt_array[i] = (rte_mbuf *)rte_malloc("req_deque_objs", sizeof(struct rte_mbuf), 0);
        }
        // cb = server->us_handlers_.find(0x10000003)->second;

#ifdef RPC_STATISTICS

#endif
    }

    // UDPConnection::UDPConnection(const UDPConnection& other)
    //         : ServerConnection(other.server_, 0), connId(other.connId) {

    //             conn =  ((UDPServer *)other.server_)->transport_->out_connections[connId];
    //     // increase number of open connections
    //     us_handlers_.insert(server_->us_handlers_.begin(), server_->us_handlers_.end());
    //     server_->sconns_ctr_.next(1);
    //     for(int i=0;i<64;i++){
    //         pkt_array[i] = (rte_mbuf*)rte_malloc("req_deque_objs", sizeof(struct rte_mbuf), 0);
    //     }
    //    // cb = server->us_handlers_.find(0x10000003)->second;

    //     #ifdef RPC_STATISTICS
    //     rs = new ring_stat();
    //     rs->used_c=0;
    //     rs->free_c=0;
    //     rs->num_sample=0;
    //     #endif

    // }

    UDPConnection::~UDPConnection()
    {
        // decrease number of open connections
        server_->sconns_ctr_.next(-1);
    }

    int UDPConnection::run_async(const std::function<void()> &f)
    {
        return server_->threadpool_->run_async(f);
    }

    void UDPConnection::begin_reply(Request<rrr::TransportMarshal> *req, i32 error_code /* =... */)
    {
        // LOG_DEBUG("Reply: %s", req->m.print_request().c_str());
        if (likely(req->m.is_type_st()))
        {
            // LOG_DEBUG("Single thread req");
            current_reply.allot_buffer_x(req->m.get_mbuf());
        }
        else
        {

            current_reply.allot_buffer(conn->get_new_pkt());
            current_reply.set_pkt_type_bg();
        }
        i32 v_error_code = error_code;
        i64 v_reply_xid = req->xid;
        // rte_pktmbuf_free(req->m.get_mbuf());
#ifdef LATENCY
        req->m >> timestamps[reply_idx % 32];
#endif
        current_reply.set_book_mark(sizeof(i32)); // will write reply size later

        current_reply << v_reply_xid;
        current_reply << v_error_code;
        xid = req->xid; 
    }

    void UDPConnection::end_reply()
    {
        i32 reply_size = current_reply.content_size();

        current_reply.write_book_mark(&reply_size, sizeof(i32));
#ifdef LATENCY
        current_reply << timestamps[reply_idx % 32];
#endif
        current_reply.format_header();
        int retry = 0;
        if (current_reply.is_type_st())
        {
            current_reply.set_pkt_type_bg();

            return;
        }
        // reply_arr[reply_idx%32] = current_reply.get_mbuf();
        reply_idx++;
        while (
            rte_ring_mp_enqueue(conn->out_bufring[retry%nr_inrings], current_reply.get_mbuf()) < 0)
        {
            retry++;
            if (retry > 100)
            {
                Log_warn("Stuck in enquueing rpc_request");
                retry = 0;
            }
        }
        LOG_DEBUG("Enqueued Packet");
    }

    void UDPConnection::handle_read()
    {

        // if (status_ == CLOSED) {

        //     return;
        // }
        unsigned int available;

        for (int j = 0; j < nr_inrings; j++)
        {
            nb_pkts = rte_ring_sc_dequeue_burst(conn->in_bufring[j], (void **)pkt_array, 32, &available);
            if (nb_pkts == 0)
                return;
            for (int i = 0; i < nb_pkts; i++)
            {

                request_array[i] = new Request<TransportMarshal>();
                request_array[i]->m.allot_buffer_x(pkt_array[i]);
                i32 req_size = 0;
                i64 xid;
                request_array[i]->m >> req_size >> request_array[i]->xid;
            }
            for (int i = 0; i < nb_pkts; i++)
            {

                i32 rpc_id;
                request_array[i]->m >> rpc_id;
                request_array[i]->m.set_pkt_type_bg();

                // cb(request_array[i], (UDPConnection *) this->ref_copy());
                auto it = us_handlers_.find(rpc_id);
                if (likely(it != us_handlers_.end()))
                {

                    it->second(request_array[i], (UDPConnection *)this);
                }
                else
                {
                    Log_error("rrr::UDPConnection: no handler for rpc_id=0x%08x", rpc_id);
                    rpc_id_missing_l_s.lock();
                    bool surpress_warning = false;
                    if (rpc_id_missing_s.find(rpc_id) == rpc_id_missing_s.end())
                    {
                        rpc_id_missing_s.insert(rpc_id);
                    }
                    else
                    {
                        surpress_warning = true;
                    }
                    rpc_id_missing_l_s.unlock();
                    if (!surpress_warning)
                    {
                        Log_error("rrr::UDPConnection: no handler for rpc_id=0x%08x", rpc_id);
                    }
                    begin_reply(request_array[i], ENOENT);
                    end_reply();
                }
            }
            rte_pktmbuf_free_bulk(pkt_array, nb_pkts);
        }
    }

    void UDPConnection::handle_write()
    {
        // if (status_ == CLOSED) {
        //     return;
        // }
        // int retry =0;
        // unsigned int available;
        // if(reply_idx == 0)
        //     return;
        //  while(
        //  rte_ring_sp_enqueue_bulk(conn->out_bufring,(void* const*)&reply_arr,reply_idx,&available)< 0){
        //     retry++;
        //     if(retry > 100*1000){
        //         Log_warn("Stuck in enqueing rpc_request");
        //         retry=0;
        //     }
        //  }
        //  reply_idx=0;
        // // out_l_.lock();
        // out_.write_to_fd(socket_);
        // if (out_.empty()) {
        // server_->pollmgr_->update_mode(this, Pollable::READ);
        // }
        // out_l_.unlock();
    }

    void UDPConnection::handle_error()
    {
        this->close();
    }

    void UDPConnection::close()
    {
        bool should_release = false;

        if (status_ == CONNECTED)
        {
            server_->sconns_l_.lock();
            unordered_set<ServerConnection *>::iterator it = server_->sconns_.find((ServerConnection *)this);
            if (it == server_->sconns_.end())
            {
                // another thread has already calling close()
                server_->sconns_l_.unlock();
                return;
            }
            server_->sconns_.erase(it);

            // because we released this connection from server_->sconns_
            should_release = true;

            server_->pollmgr_->remove(this);
            server_->sconns_l_.unlock();

            LOG_DEBUG("rrr::UDPConnection: closed on fd=%d", socket_);

            status_ = CLOSED;
        }

        // this call might actually DELETE this object, so we put it at the end of function
        if (should_release)
        {
            //  this->release();
        }
    }

    int UDPConnection::poll_mode()
    {

        return Pollable::READ | Pollable::WRITE;
    }

    UDPServer::UDPServer(PollMgr *pollmgr /* =... */, ThreadPool *thrpool /* =? */, DpdkTransport *transport)
        : Server(pollmgr, thrpool), server_sock_(-1), status_(NEW), transport_(transport)
    {

        // get rid of eclipse warning
        memset(&loop_th_, 0, sizeof(loop_th_));

        if (pollmgr == nullptr)
        {
            pollmgr_ = new PollMgr;
        }
        else
        {
            pollmgr_ = (PollMgr *)pollmgr->ref_copy();
        }

        if (thrpool == nullptr)
        {
            threadpool_ = new ThreadPool;
        }
        else
        {
            threadpool_ = (ThreadPool *)thrpool->ref_copy();
        }
        if (transport_ == nullptr)
        {
            transport_ = DpdkTransport::get_transport();
        }
        /**
         *
         * This will be replaced by a thread making out connection
         */
        // Test UDP Connection
    }
    void UDPServer::stop()
    {

        stop_loop();

        sconns_l_.lock();
        vector<ServerConnection *> sconns(sconns_.begin(), sconns_.end());
        // NOTE: do NOT clear sconns_ here, because when running the following
        // it->close(), the UDPConnection object will check the sconns_ to
        // ensure it still resides in sconns_
        sconns_l_.unlock();

        for (auto &it : sconns)
        {
            ((UDPConnection *)it)->close();
        }

        LOG_DEBUG("UDP Server Stopped");
        pollmgr_->release();
        LOG_DEBUG("PollMgr Released");

        // make sure all open connections are closed
    }
    void UDPServer::stop_loop()
    {

        if (this->status_ == RUNNING)
        {
            this->status_ = STOPPING;
            Log_info(" wait till accepting thread done");
            Pthread_join(loop_th_, nullptr);

            verify(server_sock_ == -1 && status_ == STOPPED);
        }
    }
    UDPServer::~UDPServer()
    {
        // LOG_DEBUG("rrr::UDPServer: destroyed");
        // stop();
    }

    void *UDPServer::start_server_loop(void *arg)
    {
        RPCConfig *conf = RPCConfig::get_config();

        rrr::start_server_loop_args_type *start_server_loop_args = (start_server_loop_args_type *)arg;
        UDPServer *svr = (UDPServer *)(start_server_loop_args->server);
        svr->server_loop(arg);

        freeaddrinfo(start_server_loop_args->gai_result);
        delete start_server_loop_args;
        return nullptr;
    }

    void UDPServer::server_loop(void *arg)
    {
        rrr::start_server_loop_args_type *start_server_loop_args = (start_server_loop_args_type *)arg;
        UDPServer *svr = (UDPServer *)(start_server_loop_args->server);
        Log_info("Starting Server Loop");
        vector<TransportConnection *> new_conns;

        while (!svc_registered)
        {
            ;

            usleep(10000);
        }

        transport_->reg_us_server(this);

        Log_info("Registered User Space server at transport layer");
        while (svr->status_ == RUNNING)
        {
            ;
        }
        Log_info("Server loop end");
        server_sock_ = -1;
        status_ = STOPPED;
    }

    void UDPServer::start(const char *addr)
    {
        start();
    }

    void UDPServer::start()
    {
        status_ = RUNNING;
        RPCConfig *config = RPCConfig::get_config();
        // this->transport_->init(config);
        verify(transport_ != nullptr);
        while (!transport_->initiated)
        {
            usleep(2);
        }
        // Add functios to tranport

        start_server_loop_args_type *start_server_loop_args = new start_server_loop_args_type();
        start_server_loop_args->server = (Server *)this;
        start_server_loop_args->gai_result = nullptr;
        start_server_loop_args->svr_addr = nullptr;
        Pthread_create(&loop_th_, nullptr, UDPServer::start_server_loop, start_server_loop_args);
    }

} // namespace rrr
