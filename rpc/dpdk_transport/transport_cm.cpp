#include <cstdint>
#include "rpc/dpdk_transport/transport.hpp"
#include <rte_ring.h>
#include <rte_ring_core.h>
#include "rpc/utils.hpp"

#include "rpc/dpdk_transport/transport_marshal.hpp"


namespace rrr{
// Used in in send method to create a packet
std::string DpdkTransport::getMacFromIp(std::string ip){
      for(rrr::Config::NetworkInfo it: config_->get_net_info()){
        if (it.ip == ip){
           // LOG_DEBUG("Found the Mac for IP: %s, MAC: %s",ip.c_str(),it.mac.c_str());
            return (it.mac);
            
        }
    }
    return "02:de:ad:be:ef:60";
}

uint16_t DpdkTransport::get_open_port(){
    
    pc_l.lock();
    uint16_t temp =  u_port_counter;
    u_port_counter+=1;
    pc_l.unlock();
    return temp;
}

uint64_t DpdkTransport::accept(const char* addr_str){
    Config* conf = rrr::Config::get_config();
    while(!initiated){
        LOG_DEBUG("Wating for intialization");
        sleep(1);
        ;
    }

    std::string addr(addr_str);
    size_t idx = addr.find(":");
    if (idx == std::string::npos) {
        Log_error("rrr::Transport: bad accept address: %s", addr);
        return 0;
    }
    std::string server_ip = addr.substr(0, idx);
    uint16_t port = atoi(addr.substr(idx + 1).c_str());
   
     
    
    LOG_DEBUG("Accept request %s",addr_str);
    TransportConnection* oconn = new TransportConnection();
   
    oconn->src_addr = src_addr_[config_->host_name_];
    oconn->out_addr = NetAddress(getMacFromIp(server_ip).c_str(),server_ip.c_str(),port);

    oconn->udp_port = src_addr_[config_->host_name_].port;
        addr = addr+ "::" + std::to_string(oconn->udp_port);
    uint64_t conn_id=0;
    // choose a connection based on round robin principle;
    conn_th_lock.lock();

    next_thread_+=1; 
    uint16_t chosen_tx_thread = (next_thread_%(config_->num_tx_threads_));
    uint16_t chosen_rx_thread = (next_thread_%(config_->num_rx_threads_));

    conn_counter++;
    Log_info("Accept Called");
    //verify(thread_rx_info[chosen_rx_thread].conn_counter == thread_tx_info[chosen_tx_thread].conn_counter);

    conn_id = conn_id | oconn->out_addr.ip;
    conn_id = conn_id<<16;
    // server port in BE 
    conn_id = conn_id | rte_cpu_to_be_16(port);
    //local host port in BE
    conn_id = conn_id<<16;
    conn_id  = conn_id | rte_cpu_to_be_16(oconn->udp_port);
    if(connections.find(conn_id) != connections.end()){
        conn_th_lock.unlock();
         thread_tx_info[chosen_tx_thread]->conn_lock.unlock();
         return 0;
    }
    Log_info("Chosen threads for new conn: %lu, tx-thread %d, rx-thread %d",conn_id, chosen_tx_thread,chosen_rx_thread);
   
   // rte_hash_add_key_data(conn_table, &conn_id, oconn);
   oconn->conn_id = conn_id; 
    oconn->assign_bufring();
    oconn->pkt_mempool = tx_mbuf_pool[chosen_tx_thread];
    oconn->buf_alloc(tx_mbuf_pool[chosen_tx_thread],conf->buffer_len);
    oconn->assign_availring();
    oconn->make_headers_and_produce();  
    connections[conn_id] = oconn;
    in_ring[conn_id]  = oconn->in_bufring;
    r = in_ring[conn_id];
    thread_tx_info[chosen_tx_thread]->out_ring[thread_tx_info[chosen_tx_thread]->conn_counter] = oconn->out_bufring;
    thread_tx_info[chosen_tx_thread]->avail_ring[thread_tx_info[chosen_tx_thread]->conn_counter] = oconn->available_bufring;
    thread_tx_info[chosen_tx_thread]->conn_counter++;
   
    
    conn_th_lock.unlock();
   // this->connections_[conn_id] = conn;

   int wait=0;
   
 

    TransportMarshal accept_marshal  = TransportMarshal(oconn->get_new_pkt());
    accept_marshal.set_pkt_type_sm();
    accept_marshal.write(con_ack,64);
    accept_marshal.format_header();
    while(rte_ring_enqueue(oconn->out_bufring,(void*)accept_marshal.get_mbuf())< 0)
        ;
    wait=0;      
    
    sleep(1);
    oconn->connected_=true;
    oconn->burst_size = conf->client_batch_size_;
    return conn_id;
}

uint64_t DpdkTransport::connect(const char* addr_str){
     Config* conf = rrr::Config::get_config();
    while(!initiated){
        LOG_DEBUG("Waiting for initialization");
        sleep(1);
        ;
    }
    std::string addr(addr_str);
    size_t idx = addr.find(":");
    if (idx == std::string::npos) {
        Log_error("rrr::Transport: bad connect address: %s", addr);
        return EINVAL;
    }
    std::string server_ip = addr.substr(0, idx);
    uint16_t port = atoi(addr.substr(idx + 1).c_str());

 
    // UDPConnection *conn = new UDPConnection(*s_addr);
    TransportConnection* oconn = new TransportConnection();

    LOG_DEBUG("Connecting to a server : %s",addr_str);
   // LOG_DEBUG("Mac: %s",getMacFromIp(server_ip).c_str());
    oconn->src_addr = src_addr_[config_->host_name_];
    oconn->out_addr =     NetAddress(getMacFromIp(server_ip).c_str(),server_ip.c_str(),port);
 
    uint64_t conn_id=0;
    conn_th_lock.lock();
    
    next_thread_++;
    uint16_t chosen_tx_thread = next_thread_%tx_threads_;
    uint16_t chosen_rx_thread = next_thread_%rx_threads_;
    
    oconn->udp_port = get_open_port();
    oconn->src_addr.port = oconn->udp_port;
    addr = addr + "::" + std::to_string(oconn->udp_port);
    
    conn_counter++;  
    conn_id = conn_id | oconn->out_addr.ip; // server ip in BE
    conn_id = conn_id<<16;    
    conn_id = conn_id | rte_cpu_to_be_16(port);  // server port in BE  
    conn_id = conn_id<<16;
    conn_id  = conn_id | rte_cpu_to_be_16(oconn->udp_port); //local host port in BE
    Log_info("Chosen threads for new conn: %llu is tx-thread %d, rx_thread %d",conn_id, chosen_tx_thread, rx_threads_);
    
    
    
    oconn->conn_id = conn_id; 
    oconn->assign_bufring();
    oconn->pkt_mempool = tx_mbuf_pool[chosen_tx_thread];
    oconn->buf_alloc(tx_mbuf_pool[chosen_tx_thread],conf->buffer_len);
    oconn->assign_availring();
    oconn->make_headers_and_produce();
    connections[conn_id] = oconn;
       
    connections[conn_id] = oconn;
    in_ring[conn_id]  = oconn->in_bufring;
    thread_tx_info[chosen_tx_thread]->out_ring[thread_tx_info[chosen_tx_thread]->conn_counter] = oconn->out_bufring;
    thread_tx_info[chosen_tx_thread]->avail_ring[thread_tx_info[chosen_tx_thread]->conn_counter] = oconn->available_bufring;

    
    thread_tx_info[chosen_tx_thread]->conn_counter++;
    
   
    conn_th_lock.unlock();
    // this->connections_[conn_id] = conn;
    
    
    rte_mbuf* t = oconn->get_new_pkt();
     uint8_t* dst = rte_pktmbuf_mtod(t, uint8_t*);
        
    TransportMarshal con_marshal(oconn->get_new_pkt());
    con_marshal.set_pkt_type_sm();
    con_marshal.write(con_req,64);
    con_marshal.format_header();
    rte_mbuf* pkt = con_marshal.get_mbuf();
   
    int wait=0;  
    while(rte_ring_sp_enqueue(oconn->out_bufring,(void*) con_marshal.get_mbuf()) < 0){
        wait++;
        if(wait > 100*1000){
            Log_warn("Unable to enque connection request packet: %llu",oconn->conn_id);
            wait=0;
        }
    }
        wait=0;
    while(!oconn->connected_){
        usleep(50*1000);
        wait++;
        if(wait > 20){
            Log_warn("Waiting for connection Request Ack %llu",conn_id);
            wait=0;
        }
    }
    oconn->burst_size = conf->client_batch_size_;;
    Log_info("Connected to %s",addr.c_str());
   
    return conn_id;
}
}