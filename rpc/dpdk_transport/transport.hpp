#pragma once
#include <iostream>
#include <cstdint>
#include <map>
#include <thread>
#include <functional>
#include <string>
#include <bitset>
#include <unordered_map>
#include "../../misc/marshal.hpp"
#include "config.hpp"
#include "transport_connection.hpp"
#include "../polling.hpp"
#include <rte_ethdev.h>
#include <rte_eth_ctrl.h>
#include <rte_flow.h>
#include <rte_ip.h>
#include <sys/time.h>
#include <mutex>


#define DEV_TX_OFFLOAD_VLAN_INSERT RTE_ETH_TX_OFFLOAD_VLAN_INSERT
#define DEV_TX_OFFLOAD_IPV4_CKSUM RTE_ETH_TX_OFFLOAD_IPV4_CKSUM
#define DEV_TX_OFFLOAD_UDP_CKSUM  RTE_ETH_TX_OFFLOAD_UDP_CKSUM
#define DEV_TX_OFFLOAD_TCP_CKSUM  RTE_ETH_TX_OFFLOAD_TCP_CKSUM
#define DEV_TX_OFFLOAD_SCTP_CKSUM RTE_ETH_TX_OFFLOAD_SCTP_CKSUM
#define DEV_TX_OFFLOAD_TCP_TSO DEV_TX_OFFLOAD_TCP_CKSUM
#define SM       0x07
#define RR       0x09
#define DIS      0x01
#define CON      0x02
#define CON_ACK  0x3
namespace rrr{
    
const uint16_t DATA_OFFSET = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr);
const uint16_t IPV4_OFFSET =  sizeof(struct rte_ether_hdr);
const uint16_t UDP_OFFSET = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr);

    struct Request;
    class UDPConnection;
    class UDPClient;
    class Reporter;
    // Pakcet Type    
 
    
    class DpdkTransport {
        friend class UDPServer;
        friend class UDPClient;
        friend class UDPConnection;
        #ifdef RPC_MICRO_STATISTICS
        friend class Reporter;
        #endif
        #ifdef RPC_STATISTICS
        friend class Reporter;
        #endif

        const uint8_t con_req[64] = {SM, CON, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
        const uint8_t con_ack[64] = {SM, CON_ACK, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 ,0x0,
                            0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};



    private:
        static DpdkTransport* transport_l;
        std::map<std::string,uint32_t> addr_lookup_table;
        std::map<uint64_t, TransportConnection*> out_connections;
        uint16_t udp_hdr_offset = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr);
        uint16_t ip_hdr_offset = sizeof(struct rte_ether_hdr);
        uint16_t data_offset =  sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr);
        Config* config_;
        int port_num_ = 0;
        int tx_threads_ = 0;
        int rx_threads_ = 0;
        
        uint16_t rx_queue_ = 1, tx_queue_ = 1;
        struct rte_mempool **tx_mbuf_pool;
        struct rte_mempool **rx_mbuf_pool;

        // Session Management rings for each thread; 
        //    (Single consumer multiple producer)
        struct rte_ring** tx_sm_rings;
        struct rte_ring** rx_sm_rings;
        

        uint16_t u_port_counter = 9000;
        uint16_t conn_counter=0;
        rrr::SpinLock pc_l;
      //  std::map<uint16_t,rrr::Connection*> connections_;
        SpinLock conn_th_lock;
        SpinLock init_lock;
        
        bool initiated=false;
        
        uint64_t next_thread_ = 0;
        
        std::map<std::string, NetAddress> src_addr_;
        std::map<std::string, NetAddress> dest_addr_;
        SpinLock sm_queue_l;
        std::queue<Marshal*> sm_queue;
        #ifdef RPC_MICRO_STATISTICS
        std::unordered_map<uint64_t,std::timespec> pkt_rx_ts;
        std::unordered_map<uint64_t,std::timespec> pkt_process_ts;
        std::unordered_map<uint64_t,std::timespec> pkt_complete_ts;
        rrr::SpinLock r_ts_lock;
        rrr::SpinLock t_ts_lock;
        uint64_t pkt_counter=0;
        
        #endif
        

        struct dpdk_thread_info **thread_rx_info{nullptr};
        struct dpdk_thread_info **thread_tx_info{nullptr};
        
        struct timeval start_clock, current;
        
        std::thread main_thread;
        bool force_quit{false};
        std::string getMacFromIp(std::string ip);
        void addr_config(std::string host_name,
        std::vector<Config::NetworkInfo> net_info);
        void init_dpdk_main_thread(const char* argv_str);
        void init_dpdk_echo(const char* argv_str);
        
        int port_init(uint16_t port_id);
        int port_reset(uint16_t port_id);
        int port_close(uint16_t port_id);
        void install_flow_rule(size_t phy_port);

        static int dpdk_rx_loop(void* arg);
        static int dpdk_tx_loop(void* arg);
        void tx_loop_one(dpdk_thread_info * arg);
        void initialize_tx_mbufs(void* args);

        
        void process_incoming_packets(dpdk_thread_info* rx_buf_info);
      
        
        int isolate(uint8_t phy_port);
        void do_dpdk_send(int port_num, int queue_id, void** bufs, uint64_t num_pkts);
        void send(uint8_t* payload, unsigned length,
                      uint64_t conn_id, dpdk_thread_info* tx_info, uint8_t pkt_type);
        SpinLock sendl;

public:
   // static int createTransport();
   // static DpdkTransport* getTransport();
    void init(Config* config);
    static void print_packet(rte_mbuf* pkt);
    static void create_transport(Config* config);
    static DpdkTransport* get_transport();
   // void send(uint8_t* payload, unsigned length, int server_id, int client_id);

    // Send a connec request to server at addr_str
    // Called from Application thread 
    // Assigns a dedicated dpdk thread 
    // returns the conn_id to use in future
    uint64_t connect (const char* addr);



    // Accept a Connection
    // Called from application thread to (server loop)
    // creates a transport_connection, assigns a dedicated tx_thread;
    // returns a connection  id to be used by application thread
    uint64_t accept(const char* addr);
    int connect(std::string addr);
    uint16_t get_open_port();
    void shutdown();
    void trigger_shutdown();

    ~DpdkTransport() {
        
        this->trigger_shutdown();
        this->shutdown();
        if (thread_rx_info)
            delete[] thread_rx_info;
        if (thread_tx_info)
            delete[] thread_tx_info;
        if (tx_mbuf_pool)
            delete[] tx_mbuf_pool;
        if (rx_mbuf_pool)
            delete[] rx_mbuf_pool;
    }

        private:
    };
        // Structure to provide informaion to the thread; launched;
      struct dpdk_thread_info {
        int thread_id;
        int port_id;
        int queue_id;
        int count = 0;
        int max_size=100;
        uint16_t conn_counter=0;
        bool shutdown=false;
        SpinLock conn_lock;
        // Dedicated Connections 
        std::map<uint64_t, TransportConnection*> out_connections;
        // Application thread put connection_ptr in this ring , 
        //thread will organize mbuf and other structs
        struct rte_ring* sm_ring;  
        std::map<std::string,uint32_t> addr_lookup_table;
        struct rte_mempool* mem_pool;

        struct rte_mbuf **buf{nullptr};
        
        rrr::DpdkTransport* t_layer;

        dpdk_thread_info() { }
        void init(DpdkTransport* th, int th_id, int p_id,
                  int q_id, int burst_size);
        
        int buf_alloc(struct rte_mempool* mbuf_pool);

        
        ~dpdk_thread_info() {
        }
    };
     /**
      * Helper function to process Session Management (SM) requests from other threads;
      * \param sm_ring, 
      * the ring to deque and process
      * \param mempool, 
      * the  mempool to use allocate buffers etc.
     */
     uint16_t process_sm_requests(rte_ring* sm_ring, rte_mempool* mempool);  
}