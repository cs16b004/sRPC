#include <string>
#include <sstream>


#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/tcp.h>

#include "server.hpp"

using namespace std;

namespace rrr {




std::unordered_set<i32> ServerConnection::rpc_id_missing_s;
SpinLock ServerConnection::rpc_id_missing_l_s;


TCPConnection::TCPConnection(TCPServer* server, int socket)
        : ServerConnection((Server*) server,socket) {
    // increase number of open connections
    server_->sconns_ctr_.next(1);
}

TCPConnection::~TCPConnection() {
    // decrease number of open connections
    server_->sconns_ctr_.next(-1);
}

int TCPConnection::run_async(const std::function<void()>& f) {
    return server_->threadpool_->run_async(f);
}

void TCPConnection::begin_reply(Request<rrr::Marshal>* req, i32 error_code /* =... */) {
    out_l_.lock();
    v32 v_error_code = error_code;
    v64 v_reply_xid = req->xid;

    bmark_ = this->out_.set_bookmark(sizeof(i32)); // will write reply size later

    *this << v_reply_xid;
    *this << v_error_code;
}

void TCPConnection::end_reply() {
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

void TCPConnection::handle_read() {
    if (status_ == CLOSED) {
        return;
    }

    int bytes_read = in_.read_from_fd(socket_);
    if (bytes_read == 0) {
        return;
    }
   
    list<Request<rrr::Marshal>*> complete_requests;

    for (;;) {
        i32 packet_size;
        int n_peek = in_.peek(&packet_size, sizeof(i32));
        if (n_peek == sizeof(i32) && in_.content_size() >= packet_size + sizeof(i32)) {
            // consume the packet size
            verify(in_.read(&packet_size, sizeof(i32)) == sizeof(i32));

            Request<rrr::Marshal>* req = new Request<rrr::Marshal>;
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


        auto it = server_->handlers_.find(rpc_id);
        if (it != server_->handlers_.end()) {
            // the handler should delete req, and release server_connection refcopy.
            it->second(req, (TCPConnection *) this->ref_copy());
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
                Log_error("rrr::TCPConnection: no handler for rpc_id=0x%08x", rpc_id);
            }
            begin_reply(req, ENOENT);
            end_reply();
            delete req;
        }
    }
}

void TCPConnection::handle_write() {
    if (status_ == CLOSED) {
        return;
    }

    out_l_.lock();
    out_.write_to_fd(socket_);
    if (out_.empty()) {
        server_->pollmgr_->update_mode(this, Pollable::READ);
    }
    out_l_.unlock();
}

void TCPConnection::handle_error() {
    this->close();
}

void TCPConnection::close() {
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

        LOG_DEBUG("rrr::TCPConnection: closed on fd=%d", socket_);

        status_ = CLOSED;
        ::close(socket_);
    }

    // this call might actually DELETE this object, so we put it at the end of function
    if (should_release) {
        this->release();
    }
}

int TCPConnection::poll_mode() {
    int mode = Pollable::READ;
    out_l_.lock();
    if (!out_.empty()) {
        mode |= Pollable::WRITE;
    }
    out_l_.unlock();
    return mode;
}

TCPServer::TCPServer(PollMgr* pollmgr /* =... */, ThreadPool* thrpool /* =? */)
        : Server(pollmgr,thrpool),server_sock_(-1), status_(NEW) {

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
}

TCPServer::~TCPServer() {
   stop();
}

void TCPServer::stop(){
    if (status_ == RUNNING) {
        status_ = STOPPING;
        // wait till accepting thread done
        Pthread_join(loop_th_, nullptr);

        verify(server_sock_ == -1 && status_ == STOPPED);
    }

    sconns_l_.lock();
    vector<ServerConnection*> sconns(sconns_.begin(), sconns_.end());
    // NOTE: do NOT clear sconns_ here, because when running the following
    // it->close(), the TCPConnection object will check the sconns_ to
    // ensure it still resides in sconns_
    sconns_l_.unlock();

    for (auto& it: sconns) {
        ((TCPConnection*)it)->close();
    }

    // make sure all open connections are closed
    int alive_connection_count = -1;
    for (;;) {
        int new_alive_connection_count = sconns_ctr_.peek_next();
        if (new_alive_connection_count <= 0) {
            break;
        }
        if (alive_connection_count == -1 || new_alive_connection_count < alive_connection_count) {
            LOG_DEBUG("waiting for %d alive connections to shutdown", new_alive_connection_count);
        }
        alive_connection_count = new_alive_connection_count;
        // sleep 0.05 sec because this is the timeout for PollMgr's epoll()
        usleep(50 * 1000);
    }
    verify(sconns_ctr_.peek_next() == 0);
    #ifdef RPC_STATISTICS
       // pollmgr_->remove(rJob);
    #endif
    threadpool_->release();
    pollmgr_->release();
    status_ = STOPPED;
    //LOG_DEBUG("rrr::TCPServer: destroyed");
}

void* TCPServer::start_server_loop(void* arg) {
    start_server_loop_args_type* start_server_loop_args = (start_server_loop_args_type*) arg;
    TCPServer* svr = (TCPServer*)(start_server_loop_args->server);
    svr->server_loop(start_server_loop_args->svr_addr);

    freeaddrinfo(start_server_loop_args->gai_result);
    delete start_server_loop_args;

    pthread_exit(nullptr);
    return nullptr;
}

void TCPServer::server_loop(struct addrinfo* svr_addr) {
    fd_set fds;
    while (status_ == RUNNING) {
        FD_ZERO(&fds);
        FD_SET(server_sock_, &fds);

        // use select to avoid waiting on accept when closing TCPServer
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50 * 1000; // 0.05 sec
        int fdmax = server_sock_;

        int n_ready = select(fdmax + 1, &fds, nullptr, nullptr, &tv);
        if (n_ready == 0) {
            continue;
        }
        if (status_ != RUNNING) {
            break;
        }

        int clnt_socket = accept(server_sock_, svr_addr->ai_addr, &svr_addr->ai_addrlen);
        if (clnt_socket >= 0 && status_ == RUNNING) {
            LOG_DEBUG("rrr::TCPServer: got new client, fd=%d", clnt_socket);
            verify(set_nonblocking(clnt_socket, true) == 0);

            sconns_l_.lock();
            TCPConnection* sconn = new TCPConnection(this, clnt_socket);
            sconns_.insert(sconn);
            pollmgr_->add(sconn);
            sconns_l_.unlock();
        }
    }

    close(server_sock_);
    server_sock_ = -1;
    status_ = STOPPED;
}

int TCPServer::start(const char* bind_addr) {
    string addr(bind_addr);
    size_t idx = addr.find(":");
    if (idx == string::npos) {
        Log_error("rrr::TCPServer: bad bind address: %s", bind_addr);
        return EINVAL;
    }
    string host = addr.substr(0, idx);
    string port = addr.substr(idx + 1);

    struct addrinfo hints, *result, *rp;
    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_family = AF_INET; // ipv4
    hints.ai_socktype = SOCK_STREAM; // tcp
    hints.ai_flags = AI_PASSIVE; // TCPServer side

    int r = getaddrinfo((host == "0.0.0.0") ? nullptr : host.c_str(), port.c_str(), &hints, &result);
    if (r != 0) {
        Log_error("rrr::TCPServer: getaddrinfo(): %s", gai_strerror(r));
        return EINVAL;
    }

    for (rp = result; rp != nullptr; rp = rp->ai_next) {
        server_sock_ = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (server_sock_ == -1) {
            continue;
        }

        const int yes = 1;
        verify(setsockopt(server_sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == 0);
        verify(setsockopt(server_sock_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == 0);

        if (::bind(server_sock_, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(server_sock_);
        server_sock_ = -1;
    }

    if (rp == nullptr) {
        // failed to bind
        Log_error("rrr::TCPServer: bind(): %s", strerror(errno));
        freeaddrinfo(result);
        return EINVAL;
    }

    // about backlog: http://www.linuxjournal.com/files/linuxjournal.com/linuxjournal/articles/023/2333/2333s2.html
    const int backlog = SOMAXCONN;
    verify(listen(server_sock_, backlog) == 0);
    verify(set_nonblocking(server_sock_, true) == 0);

    status_ = RUNNING;
    Log_info("rrr::TCPServer: started on %s", bind_addr);

    start_server_loop_args_type* start_server_loop_args = new start_server_loop_args_type();
    start_server_loop_args->server = (Server*)this;
    start_server_loop_args->gai_result = result;
    start_server_loop_args->svr_addr = rp;
    Pthread_create(&loop_th_, nullptr, TCPServer::start_server_loop, start_server_loop_args);

    return 0;
}

int Server::reg(i32 rpc_id, const std::function<void(Request<rrr::Marshal>*, ServerConnection*)>& func) {
    // disallow duplicate rpc_id
    if (handlers_.find(rpc_id) != handlers_.end()) {
        return EEXIST;
    }
    LOG_DEBUG("Adding %d, to  udp server", rpc_id);
    handlers_[rpc_id] = func;
    

    return 0;
}

int Server::reg(i32 rpc_id, const std::function<void(Request<rrr::TransportMarshal>*, ServerConnection*)>& func) {
    // disallow duplicate rpc_id
    if (us_handlers_.find(rpc_id) != us_handlers_.end()) {
        return EEXIST;
    }

    us_handlers_[rpc_id] = func;
    LOG_DEBUG("Adding %d, to  udp server", rpc_id);

    return 0;
}

void Server::unreg(i32 rpc_id) {
    handlers_.erase(rpc_id);
    us_handlers_.erase(rpc_id);
}

} // namespace rrr
