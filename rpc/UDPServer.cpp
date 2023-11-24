#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/mman.h>

#include "rpc/server.hpp"
#include "config.hpp"
#include "rpc/dpdk_transport/transport_marshal.hpp"

using namespace std;

namespace rrr {


UDPConnection::UDPConnection(UDPServer* server, uint64_t socket)
        : ServerConnection((Server*) server,0),connId(socket) {
            conn = server->transport_->connections[socket];
            in_ring = conn->in_bufring;
            out_ring = conn->out_bufring;
            us_handlers_.insert(server->us_handlers_.begin(), server_->us_handlers_.end());
    // increase number of open connections
    server_->sconns_ctr_.next(1);
    for(int i=0;i<32;i++){
        pkt_array[i] = (rte_mbuf*)rte_malloc("req_deque_objs", sizeof(struct rte_mbuf), 0);
    }
}

UDPConnection::~UDPConnection() {
    // decrease number of open connections
    server_->sconns_ctr_.next(-1);
}

int UDPConnection::run_async(const std::function<void()>& f) {
    return server_->threadpool_->run_async(f);
}

void UDPConnection::begin_reply(Request<rrr::TransportMarshal>* req, i32 error_code /* =... */) {
    current_reply.allot_buffer(conn->get_new_pkt());

     i32 v_error_code = error_code;
    i64 v_reply_xid = req->xid;
   // rte_pktmbuf_free(req->m.get_mbuf());
    current_reply.set_book_mark(sizeof(i32)); // will write reply size later

    current_reply << v_reply_xid;
    current_reply << v_error_code;
}

void UDPConnection::end_reply() {
    i32 reply_size = current_reply.content_size();
        current_reply.write_book_mark(&reply_size, sizeof(i32));
        current_reply.format_header();
       int retry=0;
     while(
     rte_ring_enqueue(out_ring,current_reply.get_mbuf())< 0){
        retry++;
        if(retry > 100*1000){
            Log_warn("Stuck in enquueing rpc_request");
            retry=0;
        }
     }
}

void UDPConnection::handle_read() {
   
    if (status_ == CLOSED) {
       
        return;
    }
    unsigned int available;
    unsigned int nb_pkts = rte_ring_sc_dequeue_burst(in_ring, (void**)pkt_array, 32,&available);

    Request<TransportMarshal>* request_array[32];
    
    for(int i=0;i<nb_pkts;i++){
        
        request_array[i] = new Request<TransportMarshal>();
        request_array[i]->m.allot_buffer(pkt_array[i]);
        i32 req_size=0;
        i64 xid;
        request_array[i]->m >> req_size >> request_array[i]->xid;
        

    }
    
    for (int i=0;i<nb_pkts;i++) {

        i32 rpc_id;
        request_array[i]->m >> rpc_id;

    
        auto it = server_->us_handlers_.find(rpc_id);
        if (likely(it != us_handlers_.end())) {
            // the handler should delete req, and release server_connection refcopy.
           // LOG_DEBUG("RPC Triggered");
            
            it->second(request_array[i], (UDPConnection *) this->ref_copy());
            
        } else {
            rpc_id_missing_l_s.lock();
            bool surpress_warning = false;
            if (rpc_id_missing_s.find(rpc_id) == rpc_id_missing_s.end()) {
                rpc_id_missing_s.insert(rpc_id);
            } else {
                surpress_warning = true;
            }
            rpc_id_missing_l_s.unlock();
            if (!surpress_warning) {
                Log_error("rrr::UDPConnection: no handler for rpc_id=0x%08x", rpc_id);
            }
            begin_reply(request_array[i], ENOENT);
            end_reply();
        }
    }
    rte_pktmbuf_free_bulk(pkt_array, nb_pkts);
}

void UDPConnection::handle_write() {

         server_->pollmgr_->update_mode(this, Pollable::READ);
    
}

void UDPConnection::handle_error() {
    this->close();
}

void UDPConnection::close() {
    bool should_release = false;

    if (status_ == CONNECTED) {
        server_->sconns_l_.lock();
        unordered_set<ServerConnection*>::iterator it = server_->sconns_.find((ServerConnection*)this);
        if (it == server_->sconns_.end()) {
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
    if (should_release) {
      //  this->release();
    }
}

int UDPConnection::poll_mode() {

    return Pollable::READ| Pollable::WRITE;
}

UDPServer::UDPServer(PollMgr* pollmgr /* =... */, ThreadPool* thrpool /* =? */, DpdkTransport* transport)
        : Server(pollmgr,thrpool),server_sock_(-1), status_(NEW), transport_(transport) {

    // get rid of eclipse warning
    memset(&loop_th_, 0, sizeof(loop_th_));

    if (pollmgr == nullptr) {
        pollmgr_ = new PollMgr;
    } else {
        pollmgr_ = (PollMgr *) pollmgr->ref_copy();
    }

    if (thrpool == nullptr) {
        threadpool_ = new ThreadPool;
    } else {
        threadpool_ = (ThreadPool *) thrpool->ref_copy();
    }
    if(transport_ == nullptr){
        transport_ = DpdkTransport::get_transport();

    }
    /**
     * 
     * This will be replaced by a thread making out connection
    */
    //Test UDP Connection

    
    
  
}
void UDPServer::stop(){
     if (status_ == RUNNING) {
        status_ = STOPPED;
        // wait till accepting thread done
        Pthread_join(loop_th_, nullptr);

        verify(server_sock_ == -1 && status_ == STOPPED);
    }

    sconns_l_.lock();
    vector<ServerConnection*> sconns(sconns_.begin(), sconns_.end());
    // NOTE: do NOT clear sconns_ here, because when running the following
    // it->close(), the UDPConnection object will check the sconns_ to
    // ensure it still resides in sconns_
    sconns_l_.unlock();

    for (auto& it: sconns) {
        ((UDPConnection*)it)->close();
    }
    pollmgr_->release();
    // make sure all open connections are closed
   
}
UDPServer::~UDPServer() {
    //LOG_DEBUG("rrr::UDPServer: destroyed");
    stop();
}

void* UDPServer::start_server_loop(void* arg) {
    Config* conf = Config::get_config();

    rrr::start_server_loop_args_type* start_server_loop_args = (start_server_loop_args_type*) arg;
    UDPServer* svr = (UDPServer*)(start_server_loop_args->server);
    svr->server_loop(arg);
    
     freeaddrinfo(start_server_loop_args->gai_result);
    delete start_server_loop_args;

    pthread_exit(nullptr);
    return nullptr;
}

void UDPServer::server_loop(void* arg) {
    rrr::start_server_loop_args_type* start_server_loop_args = (start_server_loop_args_type*) arg;
    UDPServer* svr = (UDPServer*)(start_server_loop_args->server);
    Log_info("Starting Server Loop");

    while(svr->status_ == RUNNING ){
        if( !svr->transport_->sm_queue.empty()){
            svr->transport_->sm_queue_l.lock();
            
            Marshal* sm_req = svr->transport_->sm_queue.front();
            svr->transport_->sm_queue.pop();
            
            svr->transport_->sm_queue_l.unlock();
            uint8_t req_type;
            verify(sm_req->read(&req_type, sizeof(uint8_t)) == sizeof(uint8_t));
            LOG_DEBUG("Request Type %02x",req_type);
            if(req_type == CON){
                std::string src_addr;
                *(sm_req)>>src_addr;
                Log_info("SM REQ to connect %s",src_addr.c_str());
                uint64_t connId = svr->transport_->accept(src_addr.c_str());
                   
                if (connId > 0){
                    svr->sconns_l_.lock();
                    
                    UDPConnection* sconn = new UDPConnection(svr, connId);
                    svr->sconns_.insert((UDPConnection*) sconn->ref_copy());
                    svr->pollmgr_->add((UDPConnection*)sconn->ref_copy());
                    svr->sconns_l_.unlock();
                }
            }
        }
    }
    Log_info("Server loop end");
    server_sock_ = -1;
    status_ = STOPPED;

}

void UDPServer::start(const char* addr) {
    start();
}

void UDPServer::start() {
    status_ = RUNNING;
    Config* config = Config::get_config(); 
    //this->transport_->init(config);
    verify(transport_!=nullptr);
    while(!transport_->initiated){
        usleep(2);
    }

    start_server_loop_args_type* start_server_loop_args = new start_server_loop_args_type();
    start_server_loop_args->server = (Server*)this;
    start_server_loop_args->gai_result = nullptr;
    start_server_loop_args->svr_addr = nullptr;
    Pthread_create(&loop_th_, nullptr, UDPServer::start_server_loop, start_server_loop_args);
    
}


} // namespace rrr
