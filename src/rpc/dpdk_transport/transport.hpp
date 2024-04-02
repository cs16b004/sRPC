#pragma once
#include <iostream>
#include <cstdint>
#include <map>
#include <thread>
#include <functional>
#include <string>
#include <bitset>
#include <unordered_map>
#include <unordered_set>

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
    class Pollable;
    /**
     * @brief swap src and sdestination address in the pkt mbuf, used by single thread server to send requests
     * back to the client.
     * 
     * @param pkt Mbuf Packet from client/sender.
     */
    void swap_udp_addresses(rte_mbuf *pkt);
    // TODO: use wrapper instead od directly calling handlers from us_server
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
    // TODO: better representation for conn_id
    struct cnn_id{
        uint64_t server_ip : 32;
        uint64_t server_port: 16;
        uint64_t local_port : 16;
    };
    /**
     * @brief This class can be called the DPDK transport layer, it hides away netowrk related details from UDPClient and 
     * Server.This is a static class, and only one instance should exists in a runtime.
     * 
     */
    
    class DpdkTransport
    {
        friend class UDPServer;
        friend class UDPClient;
        friend class UDPConnection;

    private:

        static DpdkTransport *transport_l;
        /**
         * @brief host's ip address in network (BE) byte order.
         * 
         */
        static rte_be32_t host_ip;
        /**
         * @brief A list of all cTransport connection managed by this transport layer.
         * 
         */
        static std::unordered_map<uint64_t, TransportConnection *> out_connections;
        /**
         * @brief Experimental: this was part of experiments. Should be ignored.
         * 
         */
        static std::unordered_map<uint64_t, rte_ring *> in_rings;

        /**
         * @brief Config class, provides information like network, burst size, ring size, host information etc.
         * 
         */
        static RPCConfig *config_;
        int port_num_ = 0;
        /**
         * @brief Number of dpdk threads which run event loop.
         * 
         */
        static int num_threads_;
        /**
         * @brief number of receive queues (rx_queue_), number of transmit queues (tx_queue_)
         * to configure the ETH device with.
         */
        static uint16_t rx_queue_, tx_queue_ ;
        /**
         * @brief mempools from which packet mbufs are taken while receiving and forming requests, replies.
         * Each dpdk thread has its own mempool to increase parallelism.
         */
        static struct rte_mempool **tx_mbuf_pool;
        static struct rte_mempool **rx_mbuf_pool;
        /**
         * @brief A list of accpeted connections, maintained for a rpc server, helps in handling multiple/duplicated
         * connect request.  
         * The key is the inital conn_id client_ip::client_port::server_port and value is client_ip::client_port::assigned_port
         * Assigning a different port to every connection helps in RSS as the addresses are different.
         */
        static std::unordered_map<uint64_t, uint64_t> accepted;
       
        
        
        /**
         * @brief Session management rings for each thread, this the default means of inter-thread communications
         * Whenever applications want to connect through a client or server accepts etc, a pointer to concerned Transport COnnection 
         *  is loaded onto the ring, and the dpdk threads poll on this ring signal and handle them appropriately.
         * Polling on ring is better than using locks.
         * 
         */
        static struct rte_ring **sm_rings;
        /**
         * @brief A counter to allot increasing ports to outgoing connections from clients.
         * 
         */
        static Counter u_port_counter;
        /**
         * @brief A counter to allot increasing ports to incoming connections from clients.
         * 
         */
        static Counter s_port_counter;
        /**
         * @brief Keep track of number of connections in the system.
         * 
         */
        static Counter conn_counter;

        rrr::SpinLock pc_l;
        //  std::map<uint16_t,rrr::Connection*> connections_;
        /**
         * @brief Its possible that a multiple connection requests are handled in parallel, this locks prevents racing 
         * between threads while modifying shared data structures like out_connection table etc.
         * 
         */
        static SpinLock conn_th_lock;
        /**
         * @brief In case dpdk layer is initialzed multiple times this lock helps in providing atmost once initialization
         * 
         */
        static SpinLock init_lock;

        bool initiated = false;
        /**
         * @brief A counter to assign connections to dpdk threads in a round robing manner.
         * 
         */
        static rrr::Counter next_thread_;
        /**
         * @brief src_addr_ and dest_addr_ keep addresses for each host on the network, with their ip and mac addresses, these are used 
         * in forming packet headers. Since DPDK lacks ARP, these data structures help in maintaining an arp table. 
         * 
         */
        static std::unordered_map<std::string, NetAddress> src_addr_;
        static std::unordered_map<std::string, NetAddress> dest_addr_;
        

        //Part of earlier experiments
        static SpinLock sm_queue_l;
        static std::queue<Marshal *> sm_queue;
        static std::unordered_map<i32, USHWrapper *> handlers;


        /**
         * @brief Context provided to each thread to run. Each thread receives and trasnmit from different queues to increase parallelism.
         * moreover they service different pollable if added  via add_poll_job()
         * 
         */
        struct d_thread_ctx **thread_ctx_arr{nullptr};

        bool force_quit{false};
        /**
         * @brief Get the Mac for the given ip.
         * 
         * @param ip IPv4 address of the machin in BE
         * @return uint8_t* mac address bytes
         */
        static uint8_t *getMacFromIp(uint32_t ip);
        /**
         * @brief Populate src_addr4_ and dest_addr data structures.
         * 
         * @param host_name name of the host where this method is called. (Could be catskill, brooklyn anything provided in the name field in the RPC config)
         * @param net_info network information from config files.
         */
        void addr_config(std::string host_name,
                         std::vector<RPCConfig::NetworkInfo> net_info);
        void init_dpdk_main_thread(const char *argv_str);
        /**
         * @brief Initialize the port of ETH device, set up queues, RSS RX descriptors TX descriptors etc.
         * 
         * @param port_id port to configure.
         * @return int 0 if successful.
         */
        int port_init(uint16_t port_id);
        /**
         * @brief Reset a port 
         * 
         * @param port_id port to reset.
         * @return int 0 if successful.
         */
        int port_reset(uint16_t port_id);
        /**
         * @brief Close the port , stop receiving and tranmit packets from the port.
         * 
         * @param port_id port to close.
         * @return int 0 if successful.
         */
        int port_close(uint16_t port_id);
        /**
         * @brief Install flow rule on the port. For eg. filter packets for the host's ip, RSS setup etc.
         * 
         * @param phy_port port to be configured.
         */
        static void install_flow_rule(size_t phy_port);
        /**
         * @brief Event loop that each dpdk thread performs.
         * 
         * @param arg the thread context in which the thread performs.
         * @return int 
         */
        static int ev_loop(void *arg);
        /**
         * @brief User space UDPServer, to handle the requests in dpdk threads instead of passing it to some 
         * poll thread.
         * 
         */
        static UDPServer *us_server;
        /**
         * @brief Isolate all incoming traffic to this port, this along with flow rule helps in establishing a filter which
         * helps in keeping the network flow simple.
         * 
         * 
         * @param phy_port port to isolate
         * @return int 0 if successful
         */
        static int isolate(uint8_t phy_port);
        // Part of previous work should be removed.
        SpinLock sendl;

    public:
        /**
         * @brief Initialize the DPDK Eal and this layer with the given confiugration
         * 
         * @param config Config for DPDK Transport layer.
         */
        void init(RPCConfig *config);
        /**
         * @brief Check if the layer is initialzed or not
         * 
         * @return true if initialized
         * @return false otherwise
         */
        bool initialized()
        {
            return initiated;
        }
        /**
         * @brief Get the connection record for the goven connection id.
         * 
         * @param conn_id Connection Id of the record sought.
         * @return TransportConnection* connection record.
         */
        static TransportConnection *get_conn(uint64_t conn_id)
        {
            conn_th_lock.lock();
            TransportConnection *conn = out_connections[conn_id];
            conn_th_lock.unlock();
            return conn;
        }
        /**
         * @brief Create a transport layer (singleton) instance.
         * 
         * @param config Configs for this instance.
         */
        static void create_transport(RPCConfig *config);
        /**
         * @brief process packets received by the dpdk thread runnning in ctx.
         * 
         * @param ctx  Context of thread which received pakcets from NIC.
         */
        static void process_requests(d_thread_ctx *ctx);

        /**
         * @brief Transmit packets from thread contexts out transmit rings.
         * 
         * @param ctx the thread context from which the packets are to be sent.
         */
        static void do_transmit(d_thread_ctx *ctx);
        /**
         * @brief Process session management (sm) request fo this thread.
         * 
         * @param ctx the thread for which sm request is intended.
         */
        static void process_sm_req(d_thread_ctx *ctx);
        /**
         * @brief Trigger a poll job if a thread has one.
         * 
         * @param ctx thread which holds the poll job.
         */
        static void do_poll_job(d_thread_ctx* ctx);
        /**
         * @brief Counter to assign poll jobs in a round robin fashion to threads.
         * 
         */
        Counter poll_q_c_;
        // static void
        static DpdkTransport *get_transport();
        /**
         * @brief Register a UDP Server in this layer to handle request in a single threaded fashion
         * 
         * @param ser User space server to be registered.
         */
        static void reg_us_server(UDPServer *ser);
        static void reg_us_handler(i32 id, std::function<void(Request<rrr::TransportMarshal> *, ServerConnection *)>);
        /**
         * @brief Send Ack to the client for for this connection. 
         * 
         * @param oconn the connection which is acknowledged by the server.
         */
        static void send_ack(TransportConnection *oconn);
        /**
         * @brief Add a poll job to be triggered periodically by dpdk threads. 
         * 
         * @param poll Poll Job to be triggered.
         */
        void add_poll_job(Pollable* poll);
        /**
         * @brief Send a connec request to server at addr_str
         Called from Application thread
         Assigns a dedicated dpdk thread.
         returns the conn_id to use in future.
         * 
         * @param addr Address of the RPC server.
         * @return uint64_t  id of the connection established.
         */
        
        uint64_t connect(const char *addr);
        // Accept a Connection
        // Called from application thread to (server loop)
        // creates a transport_connection, assigns a dedicated tx_thread;
        // returns a connection  id to be used by application thread
        /**
         * @brief 
         * 
         * @param conn_id 
         */
        static void accept(uint64_t conn_id);
        int connect(std::string addr);
        uint16_t get_open_port();
        void shutdown();
        void trigger_shutdown();
        static std::string ConnToString(uint64_t conn_id);

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
        uint16_t nb_rx = 0;
        uint16_t nb_tx = 0;
        bool shutdown = false;
        SpinLock conn_lock;
        uint64_t sent_pkts  =0;
        uint64_t rx_pkts    =0;
        uint64_t dropped_packets=0;
        rte_ring* poll_req_q;
        Pollable** poll_reqs;
        
        TransportConnection **conn_arr;
        // Dedicated Connections
        std::unordered_map<uint64_t, TransportConnection *> out_connections;
        std::unordered_set<TransportConnection*> t_conns;
        // Dedicated poll jobs
        std::mutex poll_l_;
        std::unordered_set<Pollable*> poll_jobs;
        //TransportConnection** conn_arr;
        Counter conn_counter;
        uint16_t max_conn=0;

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
        // void add_poll_job(Pollable* pj);

        ~d_thread_ctx()
        {
            delete[]conn_arr;
            delete[] poll_reqs;
        }
    };
    
    /**
     * Helper function to process Session Management (SM) requests from other threads;
     * \param sm_ring,
     * the ring to deque and process
     * \param mempool,
     * the  mempool to use allocate buffers etc.
     */
    
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