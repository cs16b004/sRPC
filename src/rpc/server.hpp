#pragma once

#include <unordered_map>
#include <unordered_set>

#include <pthread.h>
#include <netdb.h>
#include <sys/select.h>
#include "misc/marshal.hpp"
#include "misc/stat.hpp"
#include "polling.hpp"
#include "dpdk_transport/transport.hpp"
#include "dpdk_transport/transport_marshal.hpp"
#include "dpdk_transport/transport_connection.hpp"

/**
 * @brief Header file containing server related definitions : ServeConnections, TCP and UDP Server.
 *  Also have data structures and functions for statistics collection under #RPC_STATISTICS.
 *  
 * 
 */


struct addrinfo;
#define MAX_BUFFER_SIZE 12000






namespace rrr {

#ifdef RPC_STATISTICS

static const int g_stat_server_batching_size = 1000;
static int g_stat_server_batching[g_stat_server_batching_size];
static int g_stat_server_batching_idx;
static uint64_t g_stat_server_batching_report_time = 0;
static const uint64_t g_stat_server_batching_report_interval = 2000 * 1000 * 1000;
static uint64_t g_stat_bytes_in = 0;
static bool g_stat_stop_thread=false;
static SpinLock thr_l;
static uint64_t get_and_set_bytes(){
    thr_l.lock();
    uint64_t copy = g_stat_bytes_in;
    g_stat_bytes_in=0;
    thr_l.unlock();
    return copy;
}
static void stat_server_batching(size_t batch) {
   // Log_info("Server Batching started");
    g_stat_server_batching_idx = (g_stat_server_batching_idx + 1) % g_stat_server_batching_size;
    g_stat_server_batching[g_stat_server_batching_idx] = batch;
    uint64_t now = rrr::rdtsc();
    if (now - g_stat_server_batching_report_time > g_stat_server_batching_report_interval) {
        // do report
        int min = std::numeric_limits<int>::max();
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
        Log::info("Server BATCHING: min=%d avg=%.1lf max=%d", min, avg, max);
        g_stat_server_batching_report_time = now;
    }
}

// rpc_id -> <count, cumulative>
static std::unordered_map<i32, std::pair<Counter, Counter>> g_stat_rpc_counter;
static uint64_t g_stat_server_rpc_counting_report_time = 0;
static const uint64_t g_stat_server_rpc_counting_report_interval = 1000 * 1000 * 1000;

static void stat_server_rpc_counting(i32 rpc_id) {
    g_stat_rpc_counter[rpc_id].first.next();

    uint64_t now = rrr::rdtsc();
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


class TCPServer;
class Server;
class UDPServer;
class ServerConnection;
class UDPConnection;

/**
 * @brief Argument struct passed to be passed to thread create function for example pthread_create()
 * or dpdk rte_remote_launch()
 * 
 */
struct start_server_loop_args_type {
    Server* server;
    struct addrinfo* gai_result;
    struct addrinfo* svr_addr;
};
/**
 * The raw packet sent from client will be like this:
 * <size> <xid> <rpc_id> <arg1> <arg2> ... <argN>
 * NOTE: size does not include the size itself (<xid>..<argN>).
 *
 * For the request object, the marshal only contains <arg1>..<argN>,
 * other fields are already consumed.
 */

/**
 * @brief Abstract service class type, used by rpc compiler to generate stub classes for RPC server.
 * 
 * 
 */

class Service {
public:
    virtual ~Service() {}
    /**
     * @brief Abstract register function used by the service to register it self to server object.
     * 
     * @return int returns the number of RPC services added.
     */
    virtual int __reg_to__(Server*) = 0;
};



/**
 * @brief Abstract Connection class used by RRR to handle all communications with the underlying transport
 *  TCP / UDP (DPDK). 
 *  This extends the rrr::Pollable class and exposes 
 *  handle_read() to poll transport for data from network.
 *  handle_write() to write to underlying tranport
 *  << and >> operator for data movement to and from transport buffers like fd in TCP or mbuf structs in DPDK
 *  This is a connection record maintained for each client connected the server.
 *  
 */
class ServerConnection: public Pollable{
    friend class Server;
protected:
    /**
     * @brief Marshal object in_ is used by underlying transport to write incoming data from network.
     *                object out_ is used to write data to be sent by underlying transport.
     * 
     */
    Marshal in_, out_;
    
    SpinLock out_l_,in_l_;
    /**
     * @brief Transport Marshal object achieves same function as out_ but only for DPDK based transport.
     * 
     */
    TransportMarshal current_reply;

    /**
     * @brief Server to which this connection belongs to.
     * 
     */
    Server* server_;
    /**
     * @brief Socket fd used for communication.
     * 
     */
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
   
    virtual void begin_reply(Request<rrr::Marshal>* req, i32 error_code = 0) = 0;
    virtual void begin_reply(Request<rrr::TransportMarshal>* req, i32 error_code = 0) = 0;
    virtual void end_reply() = 0;
    virtual int run_async(const std::function<void()>& f) = 0;
    template<class T>
    ServerConnection& operator <<(const T& v) {
        #ifdef DPDK
        current_reply << v;
        #else
        this->out_ << v;
        #endif
        return *this;
    }
    ServerConnection& operator <<(Marshal& m) {
        #ifdef DPDK
        m.read(current_reply.get_offset(), m.content_size());
        #else
        this->out_.read_from_marshal(m, m.content_size());
        #endif
        return *this;
    }
    uint64_t fd() {
        return socket_;
    }
};
/**
 * @brief ServerConnection record specifically for DPDK based transport
 * pkt_array is just to receive burst of requrest packets from transport ring buffers
 * reply array is for batching replies.
 * us_handlers is just a copy of RPC methods in server class, through this 
 * DPDK layer runs rpcs in the same thread which receives packets from the nic.
 *
 */
class UDPConnection: public ServerConnection {

    friend class UDPServer;
  
    friend class DpdkTransport;

    TransportConnection* conn;
    rte_mbuf* pkt_array[64];
    rte_mbuf* reply_arr[32];
    uint32_t nb_pkts;
    uint16_t nr_inrings = 0;
    uint64_t times=0;
    uint64_t timestamps[32];
    std::unordered_map<i32, std::function<void(Request<rrr::TransportMarshal>*, ServerConnection*)>> us_handlers_;
    Request<TransportMarshal>* request_array[64];
    uint64_t reply_idx=0;
    //test call_back
    std::function<void(Request<rrr::TransportMarshal>*, ServerConnection*)> cb;
    i64 xid;
    void close();
    uint64_t connId;

protected:
    NetAddress toAddress;
    
    // Protected destructor as required by RefCounted.

    ~UDPConnection();

public:
    /**
     * @brief Construct a new UDPConnection object
     * 
     * @param server server object which owns this connection object.
     * @param socket connection id of the connection record in DPDK transport layer.
     */
    UDPConnection(UDPServer* server, uint64_t socket);

    /**
     * @brief Method to add the RPC function call to server's threadpool
     * 
     * @param f the function to be run by threadpool
     * @return int 0 if successfully queued EPERM otherwise. 
     */
    int run_async(const std::function<void()>& f);
    uint64_t fd() {
        return connId;
    }
    /**
     * @brief Templated operator << to serialze an object of type T to the marshal object.
     * 
     * @tparam T 
     * @param v 
     * @return ServerConnection& 
     */
    template<class T>
    ServerConnection& operator <<(const T& v) {
      current_reply << v;
        return *this;
    }
    /**
     * @brief Serialize a Marshal object.
     * 
     * @param m 
     * @return ServerConnection& 
     */
    ServerConnection& operator <<(Marshal& m) {
       // this->out_.read_from_marshal(m, m.content_size());
        return *this;
    }
  
    /**
     * @brief Change and return poll mode (READ) -> (READ | WRITE)
     * 
     * @return int returns the poll mode for this connection
     */
    int poll_mode();

    /**
     * @brief Virtual method from ServerConnection class can be used to send replies via DPDK Transport. 
     * 
     */
    void handle_write();
    /**
     * @brief Method which reads request packets from DPDK Transport ring buffers, runs the RPC and writes the reply to DPDK Transport rings buffers.
     * 
     */
    void handle_read();
    /**
     * @brief Method to handle any error while servicing requests from client.
     * 
     */
    void handle_error(); 
    /**
     * @brief Method call to prepare a reply for the RPC call. Called from stub classes generated by RPC Compiler.
     * NOTE: This method shouldn't be called as the reply is prepared for TCP based server and this is a UDP Connection!.
     * @param req 
     * @param error_code 
     */
    void begin_reply(Request<rrr::Marshal>* req, i32 error_code=0) {/*Should not be called*/
        verify(1);
    }
    /**
     * @brief Method call to prepare a reply message for the RPC call. Called from stub classes generated by RPC Compiler.
     * @param req Request corresponding to the RPC call.
     * @param error_code error code from the RPC call if any.
     */
    void begin_reply(Request<rrr::TransportMarshal>* req, i32 error_code=0);
    /**
     * @brief end the reply message and put it in the tranport layer ring buffers to be transmitted to the client.
     * 
     */
    void end_reply();
};
/**
 * @brief ServerConnection record for a TCP based transport.
 * By extension this is a pollable object and PollMgr threads call hand_read(), handle_write()
 * functions to read from and write to TCP socket to client.
 * 
 */
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

    uint64_t fd() {
        return socket_;
    }

    int poll_mode();
    void handle_write();
    void handle_read();
    void handle_error(); 
    void begin_reply(Request<rrr::Marshal>* req, i32 error_code=0);
    void begin_reply(Request<rrr::TransportMarshal>* req, i32 error_code=0){
        // This shouldn't be called
        verify(1);
        }
    void end_reply();
};
/**
 * @brief Abstract Server class which maintains RPC as an unordered map of functions
 * It also maintains a list of open connections with clients.
 * 
 */
class Server: public NoCopy{
    
    friend class TCPConnection;
    friend class ServerConnection;
    friend class UDPConnection;
    
    protected:
     std::unordered_map<i32, std::function<void(Request<rrr::Marshal>*, ServerConnection*)>> handlers_;
     std::unordered_map<i32, std::function<void(Request<rrr::TransportMarshal>*, ServerConnection*)>> us_handlers_;
     std::unordered_set<ServerConnection*> sconns_{};
    bool svc_registered=false;
    PollMgr* pollmgr_;
    ThreadPool* threadpool_;
   
    Counter sconns_ctr_;

    SpinLock sconns_l_;
    


    pthread_t loop_th_;
    protected:
    #ifdef RPC_STATISTICS
      //  rrr::ReportThroughputJob* rJob;
    #endif
        ~Server() {
            
        };

public:

    Server(PollMgr* pollmgr = nullptr, ThreadPool* thrpool = nullptr):pollmgr_(pollmgr),threadpool_(thrpool){
        #ifdef RPC_STATISTICS
        //rJob = new ReportThroughputJob();
        //pollmgr_->add(rJob);
        
        #endif
    };
    
   

    int reg(Service* svc) {
        
        int ret = svc->__reg_to__(this);
        
        if(ret == 0)
            svc_registered = true;
        LOG_DEBUG("Registered Service at the server thread");
        return ret;
    }
   
     virtual void stop()=0;
    int reg(i32 rpc_id, const std::function<void(Request<rrr::Marshal>*, ServerConnection*)>& func);
    
    template<class S>
    int reg(i32 rpc_id, S* svc, void (S::*svc_func)(Request<rrr::Marshal>*, ServerConnection*)) {

        // disallow duplicate rpc_id
        if (handlers_.find(rpc_id) != handlers_.end()) {
            return EEXIST;
        }

        handlers_[rpc_id] = [svc, svc_func] (Request<rrr::Marshal>* req, ServerConnection* sconn) {
            (svc->*svc_func)(req, sconn);
        };

        return 0;
    }

    int reg(i32 rpc_id, const std::function<void(Request<rrr::TransportMarshal>*, ServerConnection*)>& func);
    
    template<class S>
    int reg(i32 rpc_id, S* svc, void (S::*svc_func)(Request<rrr::TransportMarshal>*, ServerConnection*)) {

        // disallow duplicate rpc_id
        
        if (us_handlers_.find(rpc_id) != us_handlers_.end()) {
            return EEXIST;
        }

        us_handlers_[rpc_id] = [svc, svc_func] (Request<rrr::TransportMarshal>* req, ServerConnection* sconn) {
            (svc->*svc_func)(req, sconn);
        };
        
       // LOG_DEBUG("Adding2 %d, to  udp server", rpc_id);
        return 0;
    }

    void unreg(i32 rpc_id);
};
/**
 * @brief RPC Server which uses TCP as transport.
 * 
 */
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
    void stop();
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
    rrr::Request<rrr::Marshal>* req_;
    rrr::Request<rrr::TransportMarshal>* us_req_;
    rrr::ServerConnection* sconn_;
    std::function<void()> marshal_reply_;
    std::function<void()> cleanup_;

public:

    DeferredReply(rrr::Request<rrr::Marshal>* req, rrr::ServerConnection* sconn,
                  const std::function<void()>& marshal_reply, const std::function<void()>& cleanup)
        : req_(req), sconn_(sconn), marshal_reply_(marshal_reply), cleanup_(cleanup) {}

    ~DeferredReply() {
        cleanup_();
        delete req_;
        #ifdef DPDK
            ((UDPConnection*)sconn_)->release(); 
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
        
            sconn_->begin_reply(req_);
            marshal_reply_();
            sconn_->end_reply();
     
        delete this;
    }

};

/**
 * @brief Server class with DPDK as underlying transport.
 * 
 */

class UDPServer : public Server{
    friend class UDPConnection;
    friend class DpdkTransport;
    
    /**
     * @brief DPDK Transport layer 
     * 
     */
    DpdkTransport* transport_;
    

    protected:
        int wfd;
        
        int server_sock_;

        /**
         * @brief List of connection to clients.
         * 
         */
        std::unordered_set<ServerConnection*> sconns_;

        enum {
            NEW, RUNNING, STOPPING, STOPPED
        } status_;

        pthread_t loop_th_;
    public:
    /**
     * @brief Start a server at this address, this is not required as transport layer handles the bind address but for legacy and for the purpose of
     * integration to janus this method is still exposed.
     * 
     * @param addr 
     */
        void start(const char* addr);
        /**
         * @brief Wait for transport layer to get initaled before launching the server loop
         * 
         */
        void start();
        /**
         * @brief Stop the server ad close all the connections
         * 
         */
        void stop();
        /**
         * @brief Start the server loop, it polls the transport layer for new connection requests and adds a UDPConnection record if one is received.
         * NOTE: This is not required now as transport layer handles the connection request, this was part of the effor to make the server completely userspace.
         * TODO: remove this method
         * 
         * @param arg start_server_arg list passed to thread_create call.
         * @return void* 
         */
        static void* start_server_loop(void* arg);
        /**
         * @brief Serve loop logic
         * 
         * @param arg Context for server loop.
         */
        void server_loop(void* arg);
         void stop_loop();

    public:
        ~UDPServer();
        /**
         * @brief Construct a new UDPServer object
         * 
         * @param pollmgr PollMgr used to poll all the connections to this server.
         * @param thrpool Threadpool to handle requests 
         * @param transport underlying DPDK transport layer.
         */
        UDPServer(PollMgr* pollmgr = nullptr, ThreadPool* thrpool = nullptr,DpdkTransport* transport=nullptr);
};

} // namespace rrr

