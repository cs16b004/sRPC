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
#include "utils.hpp"
#include <rte_lcore.h>
#include <rte_ring.h>

namespace rrr{



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

    class TransportMarshal{
        public:
            uint8_t *payload;
            uint32_t n_bytes;
        TransportMarshal(uint32_t size){
            payload = new uint8_t[size];
            n_bytes = size;

        }
    };
// A connection structure shared by an application thread and a dpdk thread
// application thread reads and write to buffers while processing RPCs
// dpdk thread rxtx message buffers
// A ring for each connectionn as connection is assigned threads in dpdk and rrr
// A single producer and single consumer model is supposed to be fast and requires no, locking
// Pipe fd will remove totally
// UDP Server client only interacts will with tranport using connect, accept, handle_read and handle_write,
    class TransportConnection{
        public:
            uint64_t conn_id;
            int in_fd_;
            int wfd;
            SpinLock outl;
            std::queue<TransportMarshal*> out_messages;
            NetAddress out_addr;
            NetAddress src_addr;
            struct rte_ring* out_bufring; // Should be allocated by assigned dpdk thread
            struct rte_mbuf** sm_msg_buffers;
            struct rte_mbuf** out_msg_buffers; //    Can be allocated earlier;
            size_t out_max_size;
            //My port nbumber ;
            uint16_t udp_port;
            bool connected_ = false;
            int buf_alloc(rte_mempool* mempool,uint16_t max_len);
            void make_headers();
        private:
            void make_pkt_header(rte_mbuf* pkt);
    };
}