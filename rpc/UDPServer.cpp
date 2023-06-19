#pragma once
#include "server.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "dpdk_transport/config.hpp"

using namespace std;

namespace rrr {


#ifdef RPC_STATISTICS

static const int g_stat_server_batching_size = 1000;
static int g_stat_server_batching[g_stat_server_batching_size];
static int g_stat_server_batching_idx;
static uint64_t g_stat_server_batching_report_time = 0;
static const uint64_t g_stat_server_batching_report_interval = 1000 * 1000 * 1000;

static void stat_server_batching(size_t batch) {
    g_stat_server_batching_idx = (g_stat_server_batching_idx + 1) % g_stat_server_batching_size;
    g_stat_server_batching[g_stat_server_batching_idx] = batch;
    uint64_t now = base::rdtsc();
    if (now - g_stat_server_batching_report_time > g_stat_server_batching_report_interval) {
        // do report
        int min = numeric_limits<int>::max();
        int max = 0;
        int sum_count = 0;
        int sum = 0;
        for (int i = 0; i < g_stat_server_batching_size; i++) {
            if (g_stat_server_batching[i] == 0) {
                continue;
            }
            if (g_stat_server_batching[i] > max) {
                max = g_stat_server_batching[i];
            }
            if (g_stat_server_batching[i] < min) {
                min = g_stat_server_batching[i];
            }
            sum += g_stat_server_batching[i];
            sum_count++;
            g_stat_server_batching[i] = 0;
        }
        double avg = double(sum) / sum_count;
        Log::info("* UDPServer BATCHING: min=%d avg=%.1lf max=%d", min, avg, max);
        g_stat_server_batching_report_time = now;
    }
}

// rpc_id -> <count, cumulative>
static unordered_map<i32, pair<Counter, Counter>> g_stat_rpc_counter;
static uint64_t g_stat_server_rpc_counting_report_time = 0;
static const uint64_t g_stat_server_rpc_counting_report_interval = 1000 * 1000 * 1000;

static void stat_server_rpc_counting(i32 rpc_id) {
    g_stat_rpc_counter[rpc_id].first.next();

    uint64_t now = base::rdtsc();
    if (now - g_stat_server_rpc_counting_report_time > g_stat_server_rpc_counting_report_interval) {
        // do report
        for (auto& it: g_stat_rpc_counter) {
            i32 counted_rpc_id = it.first;
            i64 count = it.second.first.peek_next();
            it.second.first.reset();
            it.second.second.next(count);
            i64 cumulative = it.second.second.peek_next();
            Log::info("* RPC COUNT: id=%#08x count=%ld cumulative=%ld", counted_rpc_id, count, cumulative);
        }
        g_stat_server_rpc_counting_report_time = now;
    }
}

#endif // RPC_STATISTICS


std::unordered_set<i32> ServerConnection::rpc_id_missing_s;
SpinLock ServerConnection::rpc_id_missing_l_s;


UDPConnection::UDPConnection(UDPServer* server, int socket)
        : ServerConnection((Server*) server,socket) {
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

    out_l_.unlock();
}

void UDPConnection::handle_read() {
    if (status_ == CLOSED) {
        return;
    }

    int bytes_read = in_.read_from_fd(socket_);
    if (bytes_read == 0) {
        return;
    }

    list<Request*> complete_requests;

    for (;;) {
        i32 packet_size;
        int n_peek = in_.peek(&packet_size, sizeof(i32));
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
            // packet not complete or there's no more packet to process
            break;
        }
    }

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
        ::close(socket_);
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

struct start_server_loop_args_type {
    UDPServer* server;
    struct addrinfo* gai_result;
    struct addrinfo* svr_addr;
};

void* UDPServer::start_server_loop(void* arg) {
    
}

void UDPServer::server_loop(struct addrinfo* svr_addr) {
    
}

void UDPServer::start(Config* config) {

    int clnt_socket;
    
     UDPConnection* sconn = new UDPConnection(this, clnt_socket);
            sconns_.insert(sconn);
            pollmgr_->add(sconn);new UDPConnection(this, clnt_socket);

    this->transport_->init(config);


}

int Server::reg(i32 rpc_id, const std::function<void(Request*, ServerConnection*)>& func) {
    // disallow duplicate rpc_id
    if (handlers_.find(rpc_id) != handlers_.end()) {
        return EEXIST;
    }

    handlers_[rpc_id] = func;

    return 0;
}

void Server::unreg(i32 rpc_id) {
    handlers_.erase(rpc_id);
}

} // namespace rrr
