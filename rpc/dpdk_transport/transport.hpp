#pragma once
#include <iostream>
#include <cstdint>
#include <map>
#include <thread>
#include <functional>
#include <string>
#include "../../misc/marshal.hpp"
#include "config.hpp"

#include "../polling.hpp"
#include <rte_ethdev.h>
#include <rte_eth_ctrl.h>
#include <rte_flow.h>
#include <rte_ip.h>
#include <sys/time.h>
#include <mutex>
namespace rrr{
    
    struct Request;
    class UDPConnection;
    class UDPClient;
    // Pakcet Type    
    const uint8_t SM = 0x07;
    const uint8_t RR = 0x09;
    const uint8_t DIS = 0x01;
    const uint8_t CON = 0x02;
    
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
    
    struct TransportConnection{
        int in_fd_;
        int wfd;
        SpinLock outl;
        std::queue<Marshal*> out_messages;
        NetAddress out_addr;
    };
       
    class DpdkTransport {
        friend class UDPServer;
        friend class UDPClient;
        friend class UDPConnection;
    private:
        static DpdkTransport* transport_l;
        unsigned udp_hdr_offset = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr);
        unsigned ip_hdr_offset = sizeof(struct rte_ether_hdr);
        Config* config_;
        int port_num_ = 0;
        int tx_threads_ = 0;
        int rx_threads_ = 0;
        uint16_t conn_counter=0;
        SpinLock conn_lock;
        uint16_t rx_queue_ = 1, tx_queue_ = 1;
        struct rte_mempool **tx_mbuf_pool;
        struct rte_mempool **rx_mbuf_pool;
      //  std::map<uint16_t,rrr::Connection*> connections_;
        SpinLock connLock;
        SpinLock init_lock;
        bool initiated=false;
        
        std::map<std::string,uint32_t> addr_lookup;
        std::map<uint32_t, TransportConnection*> out_connections;
        std::map<std::string, NetAddress> src_addr_;
        std::map<std::string, NetAddress> dest_addr_;
        SpinLock sm_queue_l;
        std::queue<Marshal*> sm_queue;
 
        

        struct dpdk_thread_info *thread_rx_info{nullptr};
        struct dpdk_thread_info *thread_tx_info{nullptr};
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
      
        int make_pkt_header(uint8_t *pkt, int payload_len, uint32_t conn_id);
        int isolate(uint8_t phy_port);
        void do_dpdk_send(int port_num, int queue_id, void** bufs, uint64_t num_pkts);
        void send(uint8_t* payload, unsigned length,
                      uint16_t conn_id, dpdk_thread_info* tx_info, uint8_t pkt_type);

public:
   // static int createTransport();
   // static DpdkTransport* getTransport();
    void init(Config* config);
    static void create_transport(Config* config);
    static DpdkTransport* get_transport();
   // void send(uint8_t* payload, unsigned length, int server_id, int client_id);
    uint32_t connect (const char* addr);
      uint32_t accept(const char* addr);
    int connect(std::string addr);
  
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
        packet_stats stat;
        int udp_port_id = 0;
        struct rte_mbuf **buf{nullptr};
        rrr::DpdkTransport* dpdk_th;

        dpdk_thread_info() { }
        void init(DpdkTransport* th, int th_id, int p_id,
                  int q_id, int burst_size);
        int buf_alloc(struct rte_mempool* mbuf_pool);

        ~dpdk_thread_info() {
            if (buf)
                delete [] buf;
        }
    };
     
       
}