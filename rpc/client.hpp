#pragma once

#include <unordered_map>

#include "misc/marshal.hpp"
#include "polling.hpp"
#include "dpdk_transport/transport.hpp"
namespace rrr {

class Future;
class Client;
class TCPClient;
class UDPClient;
struct FutureAttr {
    FutureAttr(const std::function<void(Future*)>& cb = std::function<void(Future*)>()) : callback(cb) { }

    // callback should be fast, otherwise it hurts rpc performance
    std::function<void(Future*)> callback;
};

class Future: public RefCounted {
    friend class Client;
    friend class TCPClient;
    friend class UDPClient;
    i64 xid_;
    i32 error_code_;

    FutureAttr attr_;
    Marshal reply_;

    bool ready_;
    bool timed_out_;
    pthread_cond_t ready_cond_;
    pthread_mutex_t ready_m_;

    void notify_ready();

protected:

    // protected destructor as required by RefCounted.
    ~Future() {
        Pthread_mutex_destroy(&ready_m_);
        Pthread_cond_destroy(&ready_cond_);
    }

public:

    Future(i64 xid, const FutureAttr& attr = FutureAttr())
            : xid_(xid), error_code_(0), attr_(attr), ready_(false), timed_out_(false) {
        Pthread_mutex_init(&ready_m_, nullptr);
        Pthread_cond_init(&ready_cond_, nullptr);
    }

    bool ready() {
        Pthread_mutex_lock(&ready_m_);
        bool r = ready_;
        Pthread_mutex_unlock(&ready_m_);
        return r;
    }

    // wait till reply done
    void wait();

    void timed_wait(double sec);

    Marshal& get_reply() {
        wait();
        return reply_;
    }

    i32 get_error_code() {
        wait();
        return error_code_;
    }

    static inline void safe_release(Future* fu) {
        if (fu != nullptr) {
            fu->release();
        }
    }
};

class FutureGroup {
private:
    std::vector<Future*> futures_;

public:
    void add(Future* f) {
        if (f == nullptr) {
            Log_error("Invalid Future object passed to FutureGroup!");
            return;
        }
        futures_.push_back(f);
    }

    void wait_all() {
        for (auto& f : futures_) {
            f->wait();
        }
    }

    ~FutureGroup() {
        wait_all();
        for (auto& f : futures_) {
            f->release();
        }
    }
};

class Client: public Pollable {

protected:
    Marshal in_, out_;
    PollMgr* pollmgr_;
    enum {
        NEW, CONNECTED, CLOSED
    } status_;

    Counter xid_counter_;
    std::unordered_map<i64, Future*> pending_fu_;

    SpinLock pending_fu_l_;
    SpinLock out_l_;

    // reentrant, could be called multiple times before releasing
  

    void invalidate_pending_futures();

    // prevent direct usage, use close_and_release() instead
    using RefCounted::release;
public:
    Client(PollMgr* pollmgr): pollmgr_(pollmgr), status_(NEW) { }

    /**
     * Start a new request. Must be paired with end_request(), even if nullptr returned.
     *
     * The request packet format is: <size> <xid> <rpc_id> <arg1> <arg2> ... <argN>
     */
    virtual Future* begin_request(i32 rpc_id, const FutureAttr& attr = FutureAttr()) = 0;
    virtual void close() = 0;
    virtual void end_request() = 0;
    virtual int connect(const char* addr) = 0;
    virtual void close_and_release() = 0;
    virtual int fd()=0;
    

    template<class T>
    Client& operator <<(const T& v) {
        if (status_ == CONNECTED) {
            this->out_ << v;
        }
        return *this;
    }

    // NOTE: this function is used *internally* by Python extension
    Client& operator <<(Marshal& m) {
        if (status_ == CONNECTED) {
            this->out_.read_from_marshal(m, m.content_size());
        }
        return *this;
    }    
};
class UDPClient: public Client{
    protected:
        int sock_;
        int wfd;
        Marshal* out_ptr_;
        uint32_t conn_id;
        Marshal::bookmark* bmark_;
        DpdkTransport* transport_;
        using RefCounted::release;

    public:
        void handle_read(){
            verify(0);
        }
        void handle_write(){
            verify(0);
        }
        void handle_error(){
            verify(0);
        }
        void close(){
            verify(0);
        }
        int poll_mode();
        Future* begin_request(i32 rpc_id, const FutureAttr& attr = FutureAttr());
        UDPClient(PollMgr* pollmgr): Client(pollmgr), bmark_(nullptr) {
            int pipefd[2];
            verify(pipe(pipefd)==0);

            sock_ = pipefd[0];

            wfd = pipefd[1];
            out_ptr_ = &out_;
            verify(set_nonblocking(sock_, true) == 0);
            if(transport_ == nullptr)
                transport_ = new DpdkTransport();
        }
        void end_request();
        int connect(const char* addr);
        int fd(){
            return sock_;
        }
        void close_and_release() {
            close();
            release();
        }
    //    template<class T>
    // UDPClient& operator <<(const T& v) {
    //     if (status_ == CONNECTED) {
    //         (*out_ptr_)<< v;
    //     }
    //     return *this;
    // }

    // // NOTE: this function is used *internally* by Python extension
    // UDPClient& operator <<(Marshal& m) {
    //     if (status_ == CONNECTED) {
    //         (*out_ptr_).read_from_marshal(m, m.content_size());
    //     }
    //     return *this;
    // }

};
class TCPClient: public Client {
   
    int sock_;
   
    Marshal::bookmark* bmark_;

   
    // reentrant, could be called multiple times before releasing
    void close();

    
    

    // prevent direct usage, use close_and_release() instead
    using RefCounted::release;

protected:

    virtual ~TCPClient() {
        invalidate_pending_futures();
    }

public:
    void close_and_release() {
        close();
        release();
    }
    TCPClient(PollMgr* pollmgr): Client(pollmgr), bmark_(nullptr) { }

    /**
     * Start a new request. Must be paired with end_request(), even if nullptr returned.
     *
     * The request packet format is: <size> <xid> <rpc_id> <arg1> <arg2> ... <argN>
     */
    Future* begin_request(i32 rpc_id, const FutureAttr& attr = FutureAttr());

    void end_request();

    int fd() {
        return sock_;
    }

    int poll_mode();
    void handle_read();
    void handle_write();
    void handle_error();
    int connect(const char* addr);

};

class ClientPool: public NoCopy {
    rrr::Rand rand_;

    // refcopy
    rrr::PollMgr* pollmgr_;

    // guard cache_
    SpinLock l_;
    std::map<std::string, rrr::Client**> cache_;
    int parallel_connections_;

public:

    ClientPool(rrr::PollMgr* pollmgr = nullptr, int parallel_connections = 1);
    ~ClientPool();

    // return cached client connection
    // on error, return nullptr
    rrr::Client* get_client(const std::string& addr);

};

}
