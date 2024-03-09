#pragma once
#include <iostream>
#include <cstdint>
#include <map>
#include <thread>
#include <functional>
#include <string>
#include <bitset>
#include <unordered_map>

#include "config.hpp"
#include "transport_connection.hpp"
#include "../polling.hpp"
#include <rte_ethdev.h>
#include <rte_eth_ctrl.h>
#include <rte_flow.h>
#include <rte_ip.h>
#include <sys/time.h>
#include <mutex>
#include <rte_hash.h>
#include <rte_jhash.h>
#include "transport_marshal.hpp"
#define DEV_TX_OFFLOAD_VLAN_INSERT RTE_ETH_TX_OFFLOAD_VLAN_INSERT
#define DEV_TX_OFFLOAD_IPV4_CKSUM RTE_ETH_TX_OFFLOAD_IPV4_CKSUM
#define DEV_TX_OFFLOAD_UDP_CKSUM RTE_ETH_TX_OFFLOAD_UDP_CKSUM
#define DEV_TX_OFFLOAD_TCP_CKSUM RTE_ETH_TX_OFFLOAD_TCP_CKSUM
#define DEV_TX_OFFLOAD_SCTP_CKSUM RTE_ETH_TX_OFFLOAD_SCTP_CKSUM
#define DEV_TX_OFFLOAD_TCP_TSO DEV_TX_OFFLOAD_TCP_CKSUM
#define SM 0x07
#define RR 0x09
#define RR_BG 0xa
#define DIS 0x01
#define CON 0x02
#define CON_ACK 0x3
namespace rrr
{

    class UDPConnection;
    class UDPClient;
    class UDPServer;
    class ServerConnection;
    void swap_udp_addresses(rte_mbuf *pkt);
    struct USHWrapper
    {
        std::function<void(Request<rrr::TransportMarshal> *, ServerConnection *)> us_handler_;
        i32 rpc_id;

        USHWrapper(i32 id, std::function<void(Request<rrr::TransportMarshal> *, ServerConnection *)> &uh)
        {
            us_handler_ = uh;
            rpc_id = id;
        }

        void run(Request<rrr::TransportMarshal> *req, TransportConnection *conn)
        {
            LOG_DEBUG("%s", req->m.print_request().c_str());
            us_handler_(req, (ServerConnection *)(conn->sconn));
            LOG_DEBUG("pkt %p", req->m.get_mbuf());
            // swap_udp_addresses((req->m.get_mbuf()));
        }
    };

    // Pakcet Type
    
    class DpdkTransport
    {
        friend class UDPServer;
        friend class UDPClient;
        friend class UDPConnection;

    private:
        static DpdkTransport *transport_l;

        static std::unordered_map<uint64_t, TransportConnection *> out_connections;

        static std::unordered_map<uint64_t, rte_ring *> in_rings;

        struct rte_hash *conn_table;

        static RPCConfig *config_;
        int port_num_ = 0;

        int num_threads_ = 0;

        uint16_t rx_queue_ = 1, tx_queue_ = 1;
        static struct rte_mempool **tx_mbuf_pool;
        static struct rte_mempool **rx_mbuf_pool;

        // Session Management rings for each thread;
        //    (Single consumer multiple producer)
        static struct rte_ring **sm_rings;

        static Counter u_port_counter;
        static Counter conn_counter;
        rrr::SpinLock pc_l;
        //  std::map<uint16_t,rrr::Connection*> connections_;
        static SpinLock conn_th_lock;
        static SpinLock init_lock;

        bool initiated = false;

        static rrr::Counter next_thread_;

        static std::unordered_map<std::string, NetAddress> src_addr_;
        static std::unordered_map<std::string, NetAddress> dest_addr_;
        static std::unordered_map<i32, USHWrapper *> handlers;
        static SpinLock sm_queue_l;
        static std::queue<Marshal *> sm_queue;

        struct d_thread_ctx **thread_ctx_arr{nullptr};

        bool force_quit{false};
        static uint8_t *getMacFromIp(uint32_t ip);
        void addr_config(std::string host_name,
                         std::vector<RPCConfig::NetworkInfo> net_info);
        void init_dpdk_main_thread(const char *argv_str);

        int port_init(uint16_t port_id);
        int port_reset(uint16_t port_id);
        int port_close(uint16_t port_id);
        static void install_flow_rule(size_t phy_port);

        static int ev_loop(void *arg);
        static UDPServer *us_server;

        static int isolate(uint8_t phy_port);
        SpinLock sendl;

    public:
        // static int createTransport();
        // static DpdkTransport* getTransport();
        void init(RPCConfig *config);
        bool initialized()
        {
            return initiated;
        }
        static TransportConnection *get_conn(uint64_t conn_id)
        {
            conn_th_lock.lock();
            TransportConnection *conn = out_connections[conn_id];
            conn_th_lock.unlock();
            return conn;
        }
        static void create_transport(RPCConfig *config);
        static void process_requests(d_thread_ctx *ctx);
        static void do_transmit(d_thread_ctx *ctx);
        static void process_sm_req(d_thread_ctx *ctx);
        // static void
        static DpdkTransport *get_transport();
        static void reg_us_server(UDPServer *ser);
        static void reg_us_handler(i32 id, std::function<void(Request<rrr::TransportMarshal> *, ServerConnection *)>);
        static void send_ack(TransportConnection *oconn);
        // void send(uint8_t* payload, unsigned length, int server_id, int client_id);

        // Send a connec request to server at addr_str
        // Called from Application thread
        // Assigns a dedicated dpdk thread
        // returns the conn_id to use in future
        uint64_t connect(const char *addr);
        // Accept a Connection
        // Called from application thread to (server loop)
        // creates a transport_connection, assigns a dedicated tx_thread;
        // returns a connection  id to be used by application thread
        static void accept(uint64_t conn_id);
        int connect(std::string addr);
        uint16_t get_open_port();
        void shutdown();
        void trigger_shutdown();

        ~DpdkTransport()
        {

            this->trigger_shutdown();
            this->shutdown();
            if (thread_ctx_arr)
                delete[] thread_ctx_arr;
            if (tx_mbuf_pool)
                delete[] tx_mbuf_pool;
            if (rx_mbuf_pool)
                delete[] rx_mbuf_pool;
        }
    };

    // DPDK thread context;
    struct d_thread_ctx
    {
        int thread_id;
        int port_id;
        int queue_id;
        int conn_count = 0;
        int max_size = 100;
        uint16_t conn_counter = 0;
        uint16_t nb_rx = 0;
        uint16_t nb_tx = 0;
        bool shutdown = false;
        SpinLock conn_lock;
        // Dedicated Connections
        std::unordered_map<uint64_t, TransportConnection *> out_connections;

        // Application thread put connection_ptr in this ring ,
        // thread will organize mbuf and other structs
        struct rte_ring *sm_ring;
        struct rte_mempool *mem_pool;

        struct rte_mbuf **tx_bufs{nullptr};

        struct rte_mbuf **rx_bufs{nullptr};
        rrr::DpdkTransport *t_layer;

        d_thread_ctx() {}
        void init(DpdkTransport *th, int th_id, int p_id,
                  int q_id, int burst_size);

        int buf_alloc(struct rte_mempool *mbuf_pool);

        ~d_thread_ctx()
        {
        }
    };

    /**
     * Helper function to process Session Management (SM) requests from other threads;
     * \param sm_ring,
     * the ring to deque and process
     * \param mempool,
     * the  mempool to use allocate buffers etc.
     */
    uint16_t process_sm_requests(rte_ring *sm_ring, rte_mempool *mempool);
    void inline swap_udp_addresses(rte_mbuf *pkt)
    {
        // Extract Ethernet header
        rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, rte_ether_hdr *);
        uint8_t *pkt_ptr = rte_pktmbuf_mtod(pkt, uint8_t *);
        // Extract IP header
        rte_ipv4_hdr *ip_hdr = (rte_ipv4_hdr *)(pkt_ptr + ip_hdr_offset);

        // Extract UDP header
        rte_udp_hdr *udp_hdr = (rte_udp_hdr *)(pkt_ptr + udp_hdr_offset);

        // LOG_DEBUG("src MAC: %s  dst MAC: %s", rte_eth_add)
        rte_ether_addr temp;
        rte_ether_addr_copy(&(eth_hdr->src_addr), &temp);
        rte_ether_addr_copy(&(eth_hdr->dst_addr), &(eth_hdr->src_addr));
        rte_ether_addr_copy(&temp, &(eth_hdr->dst_addr));

        // Swap IP addresses
        RTE_SWAP(ip_hdr->dst_addr, ip_hdr->src_addr);
        // Swap UDP port numbers
        RTE_SWAP(udp_hdr->src_port, udp_hdr->dst_port);
    }
}