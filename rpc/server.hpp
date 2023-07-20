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


struct start_server_loop_args_type {
    Server* server;
    struct addrinfo* gai_result;
    struct addrinfo* svr_addr;
};

class TCPServer;
class Server;
class UDPServer;
class ServerConnection;


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



// Abstract Class to be used by both UDP Server and TCP Server Connections
class ServerConnection: public Pollable{
    friend class Server;
protected:
    Marshal in_, out_;
    SpinLock out_l_,in_l_;

    Server* server_;
    int socket_;

    Marshal::bookmark* bmark_;

    enum {
        CONNECTED, CLOSED
    } status_;
    
    static std::unordered_set<i32> rpc_id_missing_s;
    static SpinLock rpc_id_missing_l_s;


 // Protected destructor as required by RefCounted.
   
public:
    // ~ServerConnection();
    ServerConnection(Server* server, int socket): server_(server),socket_(socket) {
        status_ = CONNECTED;
    };
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

    TCPConnection(TCPServer* server, int socket);

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
   


    // helper function, do some work in background
    int run_async(const std::function<void()>& f);

    int fd() {
        return socket_;
    }

    int poll_mode();
    void handle_write();
    void handle_read();
    void handle_error(); 
    void begin_reply(Request* req, i32 error_code=0);
    void end_reply();
};

class Server: public NoCopy{
    friend class TCPConnection;
    friend class ServerConnection;
    friend class UDPConnection;
    protected:
     std::unordered_map<i32, std::function<void(Request*, ServerConnection*)>> handlers_;
     std::unordered_set<ServerConnection*> sconns_{};
    PollMgr* pollmgr_;
    ThreadPool* threadpool_;
   
    Counter sconns_ctr_;

    SpinLock sconns_l_;
    


    pthread_t loop_th_;
    protected:
        ~Server() {};

public:

    Server(PollMgr* pollmgr = nullptr, ThreadPool* thrpool = nullptr):pollmgr_(pollmgr),threadpool_(thrpool){};
    
   

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

class TCPServer: public Server {

    friend class TCPConnection;
    friend class ServerConnection;

      enum {
        NEW, RUNNING, STOPPING, STOPPED
    } status_;
    int server_sock_;

    std::unordered_set<ServerConnection*> sconns_;

   

    pthread_t loop_th_;

    static void* start_server_loop(void* arg);
    void server_loop(struct addrinfo* svr_addr);

public:

    TCPServer(PollMgr* pollmgr = nullptr, ThreadPool* thrpool = nullptr);
     ~TCPServer();

    int start(const char* bind_addr);

    /**
     * The svc_func need to do this:
     *
     *  {
     *     // process request
     *     ..
     *
     *     // send reply
     *     server_connection->begin_reply();
     *     *server_connection << {reply_content};
     *     server_connection->end_reply();
     *
     *     // cleanup resource
     *     delete request;
     *     server_connection->release();
     *  }
     */
   
};
class DeferredReply: public NoCopy {
    rrr::Request* req_;
    rrr::ServerConnection* sconn_;
    std::function<void()> marshal_reply_;
    std::function<void()> cleanup_;

public:

    DeferredReply(rrr::Request* req, rrr::ServerConnection* sconn,
                  const std::function<void()>& marshal_reply, const std::function<void()>& cleanup)
        : req_(req), sconn_(sconn), marshal_reply_(marshal_reply), cleanup_(cleanup) {}

    ~DeferredReply() {
        cleanup_();
        delete req_;
        #ifdef DPDK
            (UDPConnection*)sconn_->release(); 
        #else
            ((TCPConnection*)sconn_)->release();
        #endif
        req_ = nullptr;
        sconn_ = nullptr;
    }

    int run_async(const std::function<void()>& f) {
        return sconn_->run_async(f);
    }

    void reply() {
        #ifdef DPDK
            ((UDPConnection*)sconn_)->begin_reply(req_);
            marshal_reply_();
            ((UDPConnection*)sconn_)->end_reply();
        #else
            ((TCPConnection*)sconn_)->begin_reply(req_);
            marshal_reply_();
            ((TCPConnection*)sconn_)->end_reply();
        #endif
        delete this;
    }

};
class UDPConnection: public ServerConnection {

    friend class UDPServer;
  
    friend class DpdkTransport;

    
    
    void close();
    uint32_t connId;

protected:
    NetAddress toAddress;
    
    // Protected destructor as required by RefCounted.

    ~UDPConnection();

public:

    UDPConnection(UDPServer* server, int socket);
    int run_async(const std::function<void()>& f);
    
    unsigned char buf[1000];
    int fd() {
        return socket_;
    }

    int poll_mode();
    void handle_write();
    void handle_read();
    void handle_error(); 
    void begin_reply(Request* req, i32 error_code=0);
    void end_reply();
};
class UDPServer : public Server{
    friend class UDPConnection;
    DpdkTransport* transport_;
    

    protected:
        int wfd;
        ~UDPServer();
        int server_sock_;


        std::unordered_set<ServerConnection*> sconns_;

        enum {
            NEW, RUNNING, STOPPING, STOPPED
        } status_;

        pthread_t loop_th_;
    public:
        void start(Config* config);
        void start();
        static void* start_server_loop(void* arg);
        void server_loop(struct addrinfo* svr_addr);

    public:

        UDPServer(PollMgr* pollmgr = nullptr, ThreadPool* thrpool = nullptr,DpdkTransport* transport=nullptr);
};

} // namespace rrr

