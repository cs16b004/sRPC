#pragma once
#include "server.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include<linux/memfd.h>
#include "dpdk_transport/config.hpp"

using namespace std;

namespace rrr {


UDPConnection::UDPConnection(UDPServer* server, int socket)
        : ServerConnection((Server*) server,server->transport_->out_connections[socket]->in_fd_),connId(socket) {
    // increase number of open connections

    server_->sconns_ctr_.next(1);
}

UDPConnection::~UDPConnection() {
    // decrease number of open connections
    server_->sconns_ctr_.next(-1);
}

int UDPConnection::run_async(const std::function<void()>& f) {
    return server_->threadpool_->run_async(f);
}

void UDPConnection::begin_reply(Request* req, i32 error_code /* =... */) {
    out_l_.lock();
    v32 v_error_code = error_code;
    v64 v_reply_xid = req->xid;

    bmark_ = this->out_.set_bookmark(sizeof(i32)); // will write reply size later

    *this << v_reply_xid;
    *this << v_error_code;
}

void UDPConnection::end_reply() {
    // set reply size in packet
    if (bmark_ != nullptr) {
        i32 reply_size = out_.get_and_reset_write_cnt();
        out_.write_bookmark(bmark_, &reply_size);
        delete bmark_;
        bmark_ = nullptr;
    }

    // always enable write events since the code above gauranteed there
    // will be some data to send
    server_->pollmgr_->update_mode(this, Pollable::READ | Pollable::WRITE);
     Marshal *new_reply = new Marshal() ;
     new_reply->read_from_marshal(out_,out_.content_size());

    Log_debug("Reply content size : %d, out_ size: %d",new_reply->content_size(),out_.content_size());
    ((UDPServer*)server_)->transport_->out_connections[connId]->out_messages.push(new_reply);

    out_l_.unlock();

}

void UDPConnection::handle_read() {
   
    if (status_ == CLOSED) {
       
        return;
    }

    int bytes_read = in_.read_from_fd(socket_);//::read(socket_, buf , 1);
    if (bytes_read == 0) {
      
        return;
    }
    Log_info("%d Bytes Read from fd %d",bytes_read,socket_);
    list<Request*> complete_requests;
        for(;;){
       // in_.print();
        i32 packet_size;
        int n_peek = in_.peek(&packet_size, sizeof(i32));
         Log_debug("Packet Size %d",packet_size);
            Log_debug("n_peek = %d, content_size = %d",n_peek,in_.content_size());
           
        if (n_peek == sizeof(i32) && in_.content_size() >= packet_size + sizeof(i32)) {
            // consume the packet size
            verify(in_.read(&packet_size, sizeof(i32)) == sizeof(i32));

            Request* req = new Request;
            verify(req->m.read_from_marshal(in_, packet_size) == (size_t) packet_size);

            v64 v_xid;
            req->m >> v_xid;
            req->xid = v_xid.get();
            complete_requests.push_back(req);

        } else {
             Log_debug("packet not complete or there's no more packet to process");
            break;
        }
        }
    
      //  Log_info("Request read");
#ifdef RPC_STATISTICS
    stat_server_batching(complete_requests.size());
#endif // RPC_STATISTICS

    for (auto& req: complete_requests) {

        if (req->m.content_size() < sizeof(i32)) {
            // rpc id not provided
            begin_reply(req, EINVAL);
            end_reply();
            delete req;
            continue;
        }
        //req->print();
        i32 rpc_id;
        req->m >> rpc_id;

#ifdef RPC_STATISTICS
        stat_server_rpc_counting(rpc_id);
#endif // RPC_STATISTICS

        auto it = server_->handlers_.find(rpc_id);
        if (it != server_->handlers_.end()) {
            // the handler should delete req, and release server_connection refcopy.
           // Log_debug("RPC Triggered");
            it->second(req, (UDPConnection *) this->ref_copy());
            
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
            begin_reply(req, ENOENT);
            end_reply();
            delete req;
        }
    }
}

void UDPConnection::handle_write() {
    // if (status_ == CLOSED) {
    //     return;
    // }

    // out_l_.lock();
    // out_.write_to_fd(socket_);
    // if (out_.empty()) {
         server_->pollmgr_->update_mode(this, Pollable::READ);
    // }
    // out_l_.unlock();
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

        Log_debug("rrr::UDPConnection: closed on fd=%d", socket_);

        status_ = CLOSED;
       
    }

    // this call might actually DELETE this object, so we put it at the end of function
    if (should_release) {
        this->release();
    }
}

int UDPConnection::poll_mode() {
    int mode = Pollable::READ;
    out_l_.lock();
    if (!out_.empty()) {
        mode |= Pollable::WRITE;
    }
    out_l_.unlock();
    return mode;
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
        transport_ = new DpdkTransport();

    }
    /**
     * 
     * This will be replaced by a thread making out connection
    */
    //Test UDP Connection

    
    
  
}

UDPServer::~UDPServer() {
    if (status_ == RUNNING) {
        status_ = STOPPING;
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

    // make sure all open connections are closed
    int alive_connection_count = -1;
    for (;;) {
        int new_alive_connection_count = sconns_ctr_.peek_next();
        if (new_alive_connection_count <= 0) {
            break;
        }
        if (alive_connection_count == -1 || new_alive_connection_count < alive_connection_count) {
            Log_debug("waiting for %d alive connections to shutdown", new_alive_connection_count);
        }
        alive_connection_count = new_alive_connection_count;
        // sleep 0.05 sec because this is the timeout for PollMgr's epoll()
        usleep(50 * 1000);
    }
    verify(sconns_ctr_.peek_next() == 0);

    threadpool_->release();
    pollmgr_->release();

    //Log_debug("rrr::UDPServer: destroyed");
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
    Log_debug("Starting Server Loop");

    while(svr->status_ == RUNNING ){
        if( !svr->transport_->sm_queue.empty()){
            svr->transport_->sm_queue_l.lock();
            
            Marshal* sm_req = svr->transport_->sm_queue.front();
            svr->transport_->sm_queue.pop();
            
            svr->transport_->sm_queue_l.unlock();
            uint8_t req_type;
            verify(sm_req->read(&req_type, sizeof(uint8_t)) == sizeof(uint8_t));
            if(req_type == rrr::CON){
                std::string src_addr;
                *(sm_req)>>src_addr;
                Log_debug("SM REQ to connect %s",src_addr.c_str());
                uint32_t connId = svr->transport_->accept(src_addr.c_str());
                svr->sconns_l_.lock();   
                verify(set_nonblocking(svr->transport_->out_connections[connId]->in_fd_, true) == 0);
                UDPConnection* sconn = new UDPConnection(svr, connId);
                svr->sconns_.insert(sconn);
                svr->pollmgr_->add(sconn);
                svr->sconns_l_.unlock();
            }
        }
    }
    Log_debug("Server loop end");
    server_sock_ = -1;
    status_ = STOPPED;

}

void UDPServer::start(const char* addr) {
    start();
}

void UDPServer::start() {
    status_ = RUNNING;
    Config* config = Config::get_config(); 
    this->transport_->init(config);

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
