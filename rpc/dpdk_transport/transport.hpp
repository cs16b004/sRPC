#include <iostream>
#include <cstdint>
#include <map>
#include <thread>
#include <functional>
#include <string>
#include "../../misc/marshal.hpp"
#include "config.hpp"
#include "utils.hpp"


namespace rrr{
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
    };
    
    class Connection{
        private:
            uint32_t fd;
            char* name;
            NetAddress to_addr_;
            Marshal in_,out_;
            SpinLock in_l_;
            SpinLock out_l_;
            uint32_t bytes_recvd, bytes_txd;
            
            enum {OPEN, CLOSED} status_;
            public:
            Connection(NetAddress & addr){
                to_addr_ = addr;
                status_ = CLOSED;
                bytes_recvd=0;
                bytes_txd = 0;
                
            }
            uint32_t putIn(uint8_t *in_bytes,uint32_t n){
                in_l_.lock();
                int n_written = in_.write(in_bytes,n);
                if(n_written != n){
                    Log_warn("Bytes dropped as Buffer is full");
                }
                bytes_recvd+=n_written;
                in_l_.unlock();
                return n_written;
            }
            uint32_t pourOut(void* dst_,uint32_t n){
                out_l_.lock();
                int n_read = out_.peek(dst_,n);
                bytes_txd+=n_read;
                out_l_.unlock();
            }

    };
    
    class DpdkTransport {
    private:
        int port_num_ = 0;
        int tx_threads_ = 0;
        int rx_threads_ = 0;
        uint16_t conn_counter=0;
        SpinLock conn_lock;
        uint16_t rx_queue_ = 1, tx_queue_ = 1;
        struct rte_mempool **tx_mbuf_pool;
        struct rte_mempool **rx_mbuf_pool;
        std::map<uint32_t,rrr::Connection*> connections_;

        std::map<int, NetAddress> src_addr_;
        std::map<int, NetAddress> dest_addr_;

        struct dpdk_thread_info *thread_rx_info{nullptr};
        struct dpdk_thread_info *thread_tx_info{nullptr};
        struct qdma_port_info *port_info_{nullptr};
      
        std::function<int(uint8_t*, int, int, int)> response_handler;
        std::thread main_thread;
        bool force_quit{false};
        char * getMacFromIp(std::string ip);
        void addr_config(std::string host_name,
        std::vector<Config::NetworkInfo> net_info);
        void init_dpdk_main_thread(const char* argv_str);
        void init_dpdk_echo(const char* argv_str);
        
        int port_init(uint16_t port_id);
        int port_reset(uint16_t port_id);
        int port_close(uint16_t port_id);
        static void install_flow_rule(size_t phy_port, size_t qp_id, uint32_t ipv4_addr,uint16_t udp_port);

        static int dpdk_rx_loop(void* arg);
        void process_incoming_packets(dpdk_thread_info* rx_buf_info);

        int make_pkt_header(uint8_t *pkt, int payload_len,
                        int src_id, int dest_id, int port_offset);


public:
    void init(Config* config);
    void send(uint8_t* payload, unsigned length, int server_id, int client_id);
    uint32_t connect (std::string server_ip,uint32_t port);
    int connect(std::string addr);
    // int accept (); For Server Implementation;
    void shutdown();
    void trigger_shutdown();

    void register_resp_callback();

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
        int max_size;
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