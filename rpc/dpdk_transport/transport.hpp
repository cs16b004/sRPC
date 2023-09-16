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

namespace rrr{
    
    struct Request;
    class UDPConnection;
    class UDPClient;
    class Reporter;
    // Pakcet Type    
    const uint8_t SM = 0x07;
    const uint8_t RR = 0x09;
    const uint8_t DIS = 0x01;
    const uint8_t CON = 0x02;
    const uint8_t CON_ACK = 0x3;
    
    struct packet_stats {
        uint64_t pkt_count = 0;

        std::map<int, uint64_t> pkt_port_dest;

        uint64_t pkt_error = 0;
        uint64_t pkt_eth_type_err = 0;
        uint64_t pkt_ip_prot_err = 0;
        uint64_t pkt_port_num_err = 0;
        uint64_t pkt_app_err = 0;

        void merge(packet_stats& other);
        void show_statistics();
    };
   
     
      struct NetAddress {
        uint8_t id;

        uint8_t mac[6];
        uint32_t ip;
        uint16_t port;

        NetAddress() {};

        NetAddress(const char* mac_i, const char* ip_i, const int port);
        NetAddress(const uint8_t* mac_i, const uint32_t ip, const int port);
        void init(const char* mac_i, const char* ip_i, const int port);
        bool operator==(const NetAddress& other);
        NetAddress& operator=(const NetAddress& other);
        std::string getAddr();
        std::string to_string();
    };
    struct TransportMarshal{
        uint8_t *payload;
        uint32_t n_bytes;
        TransportMarshal(uint32_t size){
            payload = new uint8_t[size];
            n_bytes = size;

        }
    };
    struct TransportConnection{
        int in_fd_;
        int wfd;
        SpinLock outl;
        std::queue<TransportMarshal*> out_messages;
        NetAddress out_addr;
        NetAddress src_addr;
        //My port nbumber ;
        uint16_t udp_port;
        bool connected_ = false;
    };
       
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
        struct qdma_port_info *port_info_{nullptr};
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
    static void create_transport(Config* config);
    static DpdkTransport* get_transport();
   // void send(uint8_t* payload, unsigned length, int server_id, int client_id);
    uint64_t connect (const char* addr);
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
        if (port_info_)
            delete [] port_info_;
        if (tx_mbuf_pool)
            delete[] tx_mbuf_pool;
        if (rx_mbuf_pool)
            delete[] rx_mbuf_pool;
    }

        private:
    };
      struct dpdk_thread_info {
        int thread_id;
        int port_id;
        int queue_id;
        int count = 0;
        int max_size=100;
        uint16_t conn_counter=0;
        SpinLock conn_lock;
        std::map<uint64_t, TransportConnection*> out_connections;
        std::map<std::string,uint32_t> addr_lookup_table;
        packet_stats stat;
        int udp_port_id = 0;
        struct rte_mbuf **buf{nullptr};
        rrr::DpdkTransport* dpdk_th;

        dpdk_thread_info() { }
        void init(DpdkTransport* th, int th_id, int p_id,
                  int q_id, int burst_size);
        int buf_alloc(struct rte_mempool* mbuf_pool);
        int make_pkt_header(uint8_t *pkt, int payload_len, uint64_t conn_id);

        ~dpdk_thread_info() {
            if (buf)
                delete [] buf;
        }
    };
     
       
}