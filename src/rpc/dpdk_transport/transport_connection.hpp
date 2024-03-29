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
#include "utils.hpp"
#include <rte_lcore.h>
#include <pthread.h>
#include <rte_ring.h>

namespace rrr{


    class DPDKTransport;
    class UDPConnection;
    class UDPServer;
    class UDPClient;
    struct NetAddress {
        uint8_t id;

        uint8_t mac[6];
        uint32_t ip;
        uint16_t port;

        NetAddress() {};

        NetAddress(const char* mac_i, const char* ip_i, const int port);
        NetAddress(const uint8_t* mac_i, const uint32_t ip, const int port);
       // NetAddress()
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
        friend class ServerConnection;
        friend class UDPServer;
        
        
        public:

            
            uint64_t conn_id;
            int in_fd_;
            int wfd;
            NetAddress out_addr;
            NetAddress src_addr;
            rte_mempool* pkt_mempool;
            rte_mempool** all_pools;
            struct rte_ring** out_bufring; // Should be allocated by assigned dpdk thread
            struct rte_ring** in_bufring;
            struct rte_ring* available_bufring;
            struct rte_mbuf** sm_msg_buffers;
            struct rte_mbuf** out_msg_buffers; //    Can be allocated earlier;
            uint16_t nr_inrings;
            uint16_t nr_outrings;
            uint16_t burst_size = 1;
            uint16_t chosen_thread=0;
            Counter out_msg_counter;
            size_t out_max_size;
            //My port nbumber ;
            uint16_t udp_port;
            bool connected_ = false;
            UDPConnection** sconn;
            int buf_alloc(rte_mempool* mempool,uint16_t max_len);
            
            void make_headers_and_produce();
            void make_pkt_header(rte_mbuf* pkt);
            int assign_bufring();
            int assign_availring();
            rte_mbuf* get_new_pkt();
            rte_mbuf* get_new_pkt(uint16_t mempool_idx);
            TransportConnection(uint16_t num_dpdk_threads){
                sconn = new UDPConnection*[num_dpdk_threads];
                in_bufring = new rte_ring*[num_dpdk_threads];  
                out_bufring = new rte_ring*[num_dpdk_threads];
                
                nr_inrings = num_dpdk_threads;
                nr_outrings = num_dpdk_threads;
                for( int i=0;i<num_dpdk_threads;i++)
                    sconn[i] = nullptr, in_bufring[i] = nullptr, out_bufring[i]=nullptr;
            }
            std::string to_string(){
                std::string ret = "";
                ret += out_addr.getAddr() + "::" + std::to_string((src_addr.port)); 
                return ret;
            }

    };
}