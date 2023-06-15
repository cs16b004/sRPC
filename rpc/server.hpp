#pragma once

#include <unordered_map>
#include <unordered_set>

#include <pthread.h>
#include <netdb.h>

#include "misc/marshal.hpp"
#include "polling.hpp"
#include "dpdk_transport/transport.hpp"

// for getaddrinfo() used in TCPServer::start()
struct addrinfo;
#define MAX_BUFFER_SIZE 12000
namespace rrr {
class TCPServer;

/**
 * The raw packet sent from client will be like this:
 * <size> <xid> <rpc_id> <arg1> <arg2> ... <argN>
 * NOTE: size does not include the size itself (<xid>..<argN>).
 *
 * For the request object, the marshal only contains <arg1>..<argN>,
 * other fields are already consumed.
 */
struct Request {
    Marshal m;
    i64 xid;
};

class Service {
public:
    virtual ~Service() {}
    virtual int __reg_to__(Server*) = 0;
};


class DeferredReply: public NoCopy {
    rrr::Request* req_;
    rrr::TCPConnection* sconn_;
    std::function<void()> marshal_reply_;
    std::function<void()> cleanup_;

public:

    DeferredReply(rrr::Request* req, rrr::TCPConnection* sconn,
                  const std::function<void()>& marshal_reply, const std::function<void()>& cleanup)
        : req_(req), sconn_(sconn), marshal_reply_(marshal_reply), cleanup_(cleanup) {}

    ~DeferredReply() {
        cleanup_();
        delete req_;
        sconn_->release();
        req_ = nullptr;
        sconn_ = nullptr;
    }

    int run_async(const std::function<void()>& f) {
        return sconn_->run_async(f);
    }

    void reply() {
        sconn_->begin_reply(req_);
        marshal_reply_();
        sconn_->end_reply();
        delete this;
    }
};
// Abstract Class to be used by both UDP Server and TCP Server Connections
class ServerConnection: Pollable{
protected:
    Marshal in_, out_;
    SpinLock out_l_;

    Server* server_;
    int socket_;

    Marshal::bookmark* bmark_;

    enum {
        CONNECTED, CLOSED
    } status_;
    
    static std::unordered_set<i32> rpc_id_missing_s;
    static SpinLock rpc_id_missing_l_s;


 // Protected destructor as required by RefCounted.
    ~ServerConnection();
public:

    ServerConnection(Server* server, int socket);
    virtual void begin_reply(Request* req, i32 error_code = 0) = 0;
    virtual void end_reply() = 0;
    virtual int run_async(const std::function<void()>& f) = 0;
    template<class T>
    ServerConnection& operator <<(const T& v) {
        this->out_ << v;
        return *this;
    }
    ServerConnection& operator <<(Marshal& m) {
        this->out_.read_from_marshal(m, m.content_size());
        return *this;
    }
    int fd() {
        return socket_;
    }
};
class TCPConnection: public ServerConnection {

    friend class TCPServer;

   
    TCPServer* TCPServer_;
   
    /**
     * Only to be called by:
     * 1: ~TCPServer(), which is called when destroying TCPServer
     * 2: handle_error(), which is called by PollMgr
     */
    void close();

protected:

    // Protected destructor as required by RefCounted.
    ~TCPConnection();

public:

    TCPConnection(TCPServer* TCPServer, int socket);

    /**
     * Start a reply message. Must be paired with end_reply().
     *
     * Reply message format:
     * <size> <xid> <error_code> <ret1> <ret2> ... <retN>
     * NOTE: size does not include size itself (<xid>..<retN>).
     *
     * User only need to fill <ret1>..<retN>.
     *
     * Currently used errno:
     * 0: everything is fine
     * ENOENT: method not found
     * EINVAL: invalid packet (field missing)
     */
    void begin_reply(Request* req, i32 error_code = 0);

    void end_reply();

    // helper function, do some work in background
    int run_async(const std::function<void()>& f);

    template<class T>
    TCPConnection& operator <<(const T& v) {
        this->out_ << v;
        return *this;
    }

    TCPConnection& operator <<(Marshal& m) {
        this->out_.read_from_marshal(m, m.content_size());
        return *this;
    }

    int fd() {
        return socket_;
    }

    int poll_mode();
    void handle_write();
    void handle_read();
    void handle_error();
};

class Server: public NoCopy{
     std::unordered_map<i32, std::function<void(Request*, Connection*)>> handlers_;
    PollMgr* pollmgr_;
    ThreadPool* threadpool_;
    int TCPServer_sock_;

    Counter sconns_ctr_;

    SpinLock sconns_l_;
    std::unordered_set<Connection*> sconns_;

    enum {
        NEW, RUNNING, STOPPING, STOPPED
    } status_;

    pthread_t loop_th_;

    static void* start_server_loop(void* arg);
    void server_loop(struct addrinfo* svr_addr);

public:

    Server(PollMgr* pollmgr = nullptr, ThreadPool* thrpool = nullptr);
    virtual ~Server();

    int start(const char* bind_addr);

    int reg(Service* svc) {
        return svc->__reg_to__(this);
    }
     
    int reg(i32 rpc_id, const std::function<void(Request*, ServerConnection*)>& func);

    template<class S>
    int reg(i32 rpc_id, S* svc, void (S::*svc_func)(Request*, ServerConnection*)) {

        // disallow duplicate rpc_id
        if (handlers_.find(rpc_id) != handlers_.end()) {
            return EEXIST;
        }

        handlers_[rpc_id] = [svc, svc_func] (Request* req, ServerConnection* sconn) {
            (svc->*svc_func)(req, sconn);
        };

        return 0;
    }

    void unreg(i32 rpc_id);
};
class UDPServer: Server {

  };
class TCPServer: public NoCopy {

    friend class TCPConnection;

    std::unordered_map<i32, std::function<void(Request*, TCPConnection*)>> handlers_;
    PollMgr* pollmgr_;
    ThreadPool* threadpool_;
    int TCPServer_sock_;

    Counter sconns_ctr_;

    SpinLock sconns_l_;
    std::unordered_set<TCPConnection*> sconns_;

    enum {
        NEW, RUNNING, STOPPING, STOPPED
    } status_;

    pthread_t loop_th_;

    static void* start_server_loop(void* arg);
    void server_loop(struct addrinfo* svr_addr);

public:

    TCPServer(PollMgr* pollmgr = nullptr, ThreadPool* thrpool = nullptr);
    virtual ~TCPServer();

    int start(const char* bind_addr);

    int reg(Service* svc) {
        return svc->__reg_to__(this);
    }

    /**
     * The svc_func need to do this:
     *
     *  {
     *     // process request
     *     ..
     *
     *     // send reply
     *     TCPServer_connection->begin_reply();
     *     *TCPServer_connection << {reply_content};
     *     TCPServer_connection->end_reply();
     *
     *     // cleanup resource
     *     delete request;
     *     TCPServer_connection->release();
     *  }
     */
    int reg(i32 rpc_id, const std::function<void(Request*, TCPConnection*)>& func);

    template<class S>
    int reg(i32 rpc_id, S* svc, void (S::*svc_func)(Request*, TCPConnection*)) {

        // disallow duplicate rpc_id
        if (handlers_.find(rpc_id) != handlers_.end()) {
            return EEXIST;
        }

        handlers_[rpc_id] = [svc, svc_func] (Request* req, TCPConnection* sconn) {
            (svc->*svc_func)(req, sconn);
        };

        return 0;
    }

    void unreg(i32 rpc_id);
};

} // namespace rrr

