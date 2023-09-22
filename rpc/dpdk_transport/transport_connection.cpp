#include "transport_connection.hpp"

namespace rrr{


int TransportConnection::buf_alloc(rte_mempool* mempool, uint16_t max_len){
    out_msg_buffers = new struct rte_mbuf*[max_len];
    int retval =  rte_pktmbuf_alloc_bulk(mempool, out_msg_buffers, (unsigned int)max_len);
    verify(retval == 0);
    return retval;
}
void TransportConnection::make_headers(){
     for(int i=0;i<Config::get_config()->buffer_len;i++){
        //log_debug("Packet address for pkt %d while making header: %p",i,&buf[i]);
        make_pkt_header(out_msg_buffers[i]);
    }
}
void TransportConnection::make_pkt_header(rte_mbuf* pkt){
    verify(pkt !=nullptr);
    Config* conf = Config::get_config();
    uint16_t pkt_offset=0;
   
    pkt->next = NULL;
    pkt->ol_flags = RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_UDP_CKSUM;
    /* Initialize Ethernet header. */
    uint8_t* pkt_buf = rte_pktmbuf_mtod(pkt, uint8_t*);

    
   
    rte_ether_hdr* eth_hdr = reinterpret_cast<rte_ether_hdr*>(pkt_buf);
    gen_eth_header(eth_hdr, src_addr.mac, out_addr.mac);

   // Log_debug("Making pkt ether addr %s at address %p",mac_to_string(eth_hdr->dst_addr.addr_bytes).c_str(), eth_hdr);

    pkt_offset += sizeof(rte_ether_hdr);
    rte_ipv4_hdr* ipv4_hdr = reinterpret_cast<rte_ipv4_hdr*>(pkt_buf + pkt_offset);
    gen_ipv4_header(ipv4_hdr, src_addr.ip, (out_addr.ip), 64);

    pkt_offset += sizeof(rte_ipv4_hdr);
    rte_udp_hdr* udp_hdr = reinterpret_cast<rte_udp_hdr*>(pkt_buf + pkt_offset);
   
    gen_udp_header(udp_hdr, src_addr.port, out_addr.port , 64);

    pkt_offset += sizeof(rte_udp_hdr);
    pkt->l2_len = sizeof(struct rte_ether_hdr);
    pkt->l3_len = sizeof(struct rte_ipv4_hdr);
    pkt->l4_len = sizeof(struct rte_udp_hdr);
    pkt->nb_segs = 1;
}




void NetAddress::init(const char* mac_i, const char* ip_i, const int port_i) {
    mac_from_str(mac_i, mac);
    ip = ipv4_from_str(ip_i);
    port = port_i;
}

NetAddress::NetAddress(const char* mac_i, const char* ip_i, const int port_i) {
    init(mac_i, ip_i, port_i);
}

NetAddress::NetAddress(const uint8_t* mac_i, const uint32_t ip_i, const int port_i) {
    memcpy(mac, mac_i, sizeof(mac));
    ip = ip_i;
    port = port_i;
}

std::string NetAddress::getAddr(){
    std::stringstream ss;
    ss<<ipv4_to_string(ip)<<":"<<std::to_string(port);
    return ss.str();
}

std::string NetAddress::to_string(){
            std::stringstream ss;
            ss<<"\n[ IP: "<<ipv4_to_string((ip))<<"\n  "<<"mac: "<<mac_to_string(mac)<<"\n ";
            ss<<"port: "<<std::to_string(port)<<"\n]";
            return ss.str();
} 
bool NetAddress::operator==(const NetAddress& other) {
    if (&other == this)
        return true;

    for (uint8_t i = 0; i < sizeof(mac); i++)
        if (this->mac[i] != other.mac[i])
            return false;

    if ((this->ip != other.ip) || (this->port != other.port))
        return false;

    return true;
}

NetAddress& NetAddress::operator=(const NetAddress& other) {
    if (this == &other)
        return *this;

    memcpy(this->mac, other.mac, sizeof(this->mac));
    this->ip = other.ip;
    this->port = other.port;

    return *this;
}

}