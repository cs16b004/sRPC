#ifndef TRANSPORT_CONNECTION_HPP
#define TRANSPORT_CONNECTION_HPP

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
#include <pthread.h>
#include <rte_ring.h>

namespace rrr{


    class DPDKTransport;
    class UDPconnection;
    class UDPServer;
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
// A connection structure shared by an application thread and a dpdk thread
// application thread reads and write to buffers while processing RPCs
// dpdk thread rxtx message buffers
// A ring for each connectionn as connection is assigned threads in dpdk and rrr
// A single producer and single consumer model is supposed to be fast and requires no, locking
// Pipe fd will remove totally
// UDP Server client only interacts will with tranport using connect, accept, handle_read and handle_write,
    class TransportConnection{
        friend class DPDKTransport;
        friend class UDPConnection;
        friend class UDPServer;
        
        public:
            TransportConnection(){}
            uint64_t conn_id;
            NetAddress out_addr;
            NetAddress src_addr;
            rte_mempool* pkt_mempool;
            struct rte_ring* out_bufring; // Should be allocated by assigned dpdk thread
            struct rte_ring* in_bufring;
            struct rte_ring* available_bufring;
            struct rte_mbuf** sm_msg_buffers;
            struct rte_mbuf** out_msg_buffers; //    Can be allocated earlier;
            uint16_t burst_size = 1;
            Counter out_msg_counter;
            size_t out_max_size;
            //My port nbumber ;
            uint16_t udp_port;
            bool connected_ = false;
        public:
            int buf_alloc(rte_mempool* mempool,uint16_t max_len);
            void make_headers_and_produce();
            void make_pkt_header(rte_mbuf* pkt);
            int assign_bufring();
            int assign_availring();
            rte_mbuf* get_new_pkt();

    };
}
#endif