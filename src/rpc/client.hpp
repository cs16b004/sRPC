#pragma once

#include <unordered_map>

#include "misc/marshal.hpp"
#include "polling.hpp"
#include "misc/stat.hpp"
#include <pthread.h>
#include "dpdk_transport/transport.hpp"
#include <chrono>
#include <ctime> 
#include "dpdk_transport/transport_marshal.hpp"
#include "dpdk_transport/transport_connection.hpp"

namespace rrr {
class Future;
class Client;
class TCPClient;
//class UDPClient;
/**
 * @brief A future attribute object is added to rpc call's future. Once the reply comes from the network. Polling threads run the call back function.
 * In Janus this is used to notify by setting a Reactor::Event.
 * 
 */
struct FutureAttr {
    FutureAttr(const std::function<void(Future*)>& cb = std::function<void(Future*)>()) : callback(cb) { }

    // callback should be fast, otherwise it hurts rpc performance
    std::function<void(Future*)> callback;
};

/**
 * @brief A future object holds the the returned value from the RPC call, it also has logic to do blocking wait on a particular rpc call.
 * It keeps the reply call back in the form of Future Attr. 
 * 
 */
class Future: public RefCounted {
    /**
     * @brief All client classes are friend as they need access to reply_ marshal, and notify_ready() methods
     * 
     */
    friend class Client;
    friend class TCPClient;
    friend class UDPClient;
    /**
     * @brief Request id for a particular RPC call.
     * 
     */
    i64 xid_;
    /**
     * @brief Error code returned from server.
     * 
     */
    i32 error_code_;

    FutureAttr attr_;
    // #ifdef DPDK
    //     TransportMarshal reply_;
    
    /**
     * @brief Serialzed reply from the server.
     * NOTE: If DPDK is used then TransportMarshal could be much faster than marshal, Writing Marshal object is a memcopy syscall which is slow. 
     */
        Marshal reply_;
    /**
     * @brief If the reply is ready.
     * 
     */
    bool ready_;
    /**
     * @brief If an expiry is set on the reply then the reply will be dicarded even if received.
     * 
     */
    bool timed_out_;
    /**
     * @brief Used in standard way to conditionally wait on ready ad time out to signal the rpc calling thread.
     * 
     */
    pthread_cond_t ready_cond_;
    pthread_mutex_t ready_m_;
    /**
     * @brief Notify if the reply is received and the future is ready with returned values.
     * 
     */
    void notify_ready();

protected:

    // protected destructor as required by RefCounted.
    ~Future() {
        Pthread_mutex_destroy(&ready_m_);
        Pthread_cond_destroy(&ready_cond_);
    }

public:
    /**
     * @brief Construct a new Future object
     * 
     * @param xid request id associated with the rpc call.
     * @param attr callback function to call the reply is received.
     */
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
/**
 * @brief A utility object to wait on a set of rpc futures (rpc calls) instead of blockingly wait on them individually.
 * 
 */
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

/**
 * @brief Abstract client class used by Application and stub proxy classes to abstract away communication.
 * 
 */
class Client: public Pollable {

protected:
    /**
     * @brief Marshal object to hold serialized rpc requests and reply values. 
     * 
     */
    Marshal in_, out_;
    /**
     * @brief TransportMarshal to hold aa serialized requests.
     * 
     */
    TransportMarshal current_req;
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
    #ifdef RPC_STATISTICS
    uint64_t rep_count=0;   
        
    #endif
    Client(PollMgr* pollmgr): pollmgr_(pollmgr), status_(NEW) { 
        
    }
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
    virtual uint64_t fd()=0;
    

     template<class T>
     Client& operator <<(const T& v) {
       
        if (status_ == CONNECTED) {
           
            #ifdef DPDK
            this->current_req << v;
            #else
            this->out_ << v;
            #endif
            
        }
       
        return *this;
        
    }

    // NOTE: this function is used *internally* by Python extension
    Client& operator <<(Marshal& m) {
      
        if (status_ == CONNECTED) {
            #ifdef DPDK
            m.read(this->current_req.get_offset(),m.content_size());
            #else
            this->out_.read_from_marshal(m, m.content_size());
            #endif
        }
      
        return *this;
    }    
};

/**
 * @brief A pollable client which enques request in transmit rings and handle replies from inrings.
 * Each client gets a TransportConnection record from dpdk layer from  when connect is called.
 * 
 */

class UDPClient: public Client{
    protected:
        uint64_t sock_;
        int wfd;
        Marshal* out_ptr_;
        uint16_t nr_inrings=0;
        uint64_t conn_id;
        TransportConnection* conn;
        rte_mbuf* pkt_array[32];
        TransportMarshal reply_array[32];
        Marshal::bookmark* bmark_;
        DpdkTransport* transport_ =nullptr;
        i64 current_xid;
        using RefCounted::release;

    public:
        /**
         * @brief Handle replies from transport layer
         * 
         */
        void handle_read();
        void handle_write(){
            // TODO: No need as writing to dpdk layer is taken 
            pollmgr_->update_mode(this, Pollable::READ);
        }
        void handle_error(){
            //TODO: implement error handling 
            verify(0);
        }
        void close();
        int poll_mode();
        /**
         * @brief prepare a rpc requestto be sent to rpc server.
         * 
         * @param rpc_id 32 bit rpc identifier.
         * @param attr FutureAttr, call back after reply is received.
         * @return Future* Future object returned which the callee can wait on until rpc returns.
         */
        Future* begin_request(i32 rpc_id, const FutureAttr& attr = FutureAttr());
        UDPClient(PollMgr* pollmgr): Client(pollmgr), bmark_(nullptr) {
           
            out_ptr_ = &out_;
            
        
            transport_=  DpdkTransport::get_transport();
            //transport_->init(RPCConfig::get_config());
            while((transport_->initialized()) == false){
                //LOG_DEBUG("Waiting for transport to initialize");
                ;
            }
            for(int i=0;i<32;i++){
                pkt_array[i] = (rte_mbuf*)rte_malloc("req_deque_objs", sizeof(struct rte_mbuf), 0);
            }
            nr_inrings = transport_->num_threads_;
            Log_info("nr_rings %d",nr_inrings);
        }/**
         * @brief End the RPC request, enqueue it on dpdk transmit rings.
         * 
         */
        void end_request();
        /**
         * @brief Connect to the rpc server in the address ip::port
         * 
         * @param addr string containing ip and port in human readable format
         * @return int 0 if successfull
         */
        int connect(const char* addr);
        uint64_t fd(){
            return sock_;
        }
        void close_and_release() {
            close();
            release();
        }
        /**
         * @brief Serialize v of type T and try sending it to the server.
         * 
         * @tparam T Type of parameter V
         * @param v object to be serialized and sent.
         * @return UDPClient& 
         */
        template<class T>
        UDPClient& operator <<(const T& v) {
            if(status_ == CONNECTED){
               // LOG_DEBUG("Inserting string");
                (current_req) << v;
            }
            return *this;
        }
        
    /**
     * @brief Send bytes in Marshal buffer to the server.
     * 
     * @param m Marshal object to be sent
     * @return UDPClient& 
     */
     UDPClient& operator <<(Marshal& m) {
        if (status_ == CONNECTED) {
            m.read(current_req.get_offset(),m.content_size());

        }
        return *this;
    }

};
class TCPClient: public Client {
   
    uint64_t sock_;
   
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
     * @brief 
     * 
     * Start a new request. Must be paired with end_request(), even if nullptr returned.
     *
     * The request packet format is: <size> <xid> <rpc_id> <arg1> <arg2> ... <argN>
     */
    Future* begin_request(i32 rpc_id, const FutureAttr& attr = FutureAttr());

    void end_request();

    uint64_t fd() {
        return sock_;
    }

    int poll_mode();
    void handle_read();
    void handle_write();
    void handle_error();
    int connect(const char* addr);
   

};
/**
 * @brief Use this pool to manage a multiple clients connected to different servers.
 * 
 */
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
