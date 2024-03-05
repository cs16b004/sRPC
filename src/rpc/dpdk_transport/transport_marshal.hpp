#pragma once
#include<string>
#include<unordered_map>
#include<map>
#include<vector>
#include<set>
#include<unordered_set>
#include "../../base/all.hpp"
#include "../../misc/marshal.hpp"
#include <rte_mbuf_core.h>
#include <rte_mempool.h>
#include <rte_ip.h>
#include <rte_ether.h>
#include <rte_udp.h>
#include <rte_ether.h>
namespace rrr{
  constexpr uint16_t udp_hdr_offset = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr);
    constexpr uint16_t ip_hdr_offset = sizeof(struct rte_ether_hdr);
    constexpr uint16_t data_offset = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr);
    const uint8_t pad[64] = {  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
                                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, };
template<class T>
class Request {
    public:
    T m;
    i64 xid;
};

    class TransportMarshal{
        private:
            
            rte_mbuf* req_data =nullptr;
            rte_ipv4_hdr* ipv4_hdr;
            rte_ether_hdr* eth_hdr;
            rte_udp_hdr* udp_hdr; 

            uint16_t book_mark_offset=0;
            uint16_t book_mark_len=0;
            uint16_t offset =  data_offset;
        public:
            TransportMarshal(){

            }
            TransportMarshal(rte_mbuf* req){
              req_data = (rte_mbuf*)req;     
              uint8_t* dst = rte_pktmbuf_mtod(req_data, uint8_t*);
              eth_hdr = reinterpret_cast<rte_ether_hdr*>(dst);
              
              ipv4_hdr = reinterpret_cast<rte_ipv4_hdr*>(dst + ip_hdr_offset);
              udp_hdr = reinterpret_cast<rte_udp_hdr*>(dst + udp_hdr_offset);
             // verify(dst !=nullptr);
              dst+=offset;
              
              uint8_t pkt_type = 0x09;
              rte_memcpy(dst,&pkt_type,1);
              
              offset+=1;
            }
            void allot_buffer(rte_mbuf* req){
              book_mark_offset=0;
              book_mark_len=0;
              offset = data_offset;
              req_data = (rte_mbuf*)req;     
              uint8_t* dst = rte_pktmbuf_mtod(req_data, uint8_t*);
              eth_hdr = reinterpret_cast<rte_ether_hdr*>(dst);
              
              ipv4_hdr = reinterpret_cast<rte_ipv4_hdr*>(dst + ip_hdr_offset);
              udp_hdr = reinterpret_cast<rte_udp_hdr*>(dst + udp_hdr_offset);
             // verify(dst !=nullptr);
              dst+=offset;
              
              uint8_t pkt_type = 0x09;
              rte_memcpy(dst,&pkt_type,1);
              
              offset+=1;
            }
            void allot_buffer_x(rte_mbuf* req){
              book_mark_offset=0;
              book_mark_len=0;
              offset =  data_offset;
              req_data = req;     
              uint8_t* dst = rte_pktmbuf_mtod(req_data, uint8_t*);
              eth_hdr = reinterpret_cast<rte_ether_hdr*>(dst);
              
              ipv4_hdr = reinterpret_cast<rte_ipv4_hdr*>(dst + ip_hdr_offset);
              udp_hdr = reinterpret_cast<rte_udp_hdr*>(dst + udp_hdr_offset);
             // verify(dst !=nullptr);
              dst+=offset;
              
             
              
              offset+=1;
            }
            size_t read(void *p, size_t n){
              void* src = rte_pktmbuf_mtod_offset(req_data, void*,offset);
              rte_memcpy(p, src, n);
              offset+=n;
              return n;
            }
            size_t peek(void *p, size_t n) const{
              void* src = rte_pktmbuf_mtod_offset(req_data, void*, offset);
              rte_memcpy(p, src, n);
              return n;
            }
            size_t write(const void* p, size_t n ){
              void* dst = rte_pktmbuf_mtod_offset(req_data, void*, offset);
             
              rte_memcpy(dst,p,n);
              offset+=n;
              return n;
            }
            void set_book_mark(uint16_t n){
              book_mark_offset = offset;
              book_mark_len = n;
              offset+=n;
            }
            void write_book_mark(void *p, size_t n){
              void* dst = rte_pktmbuf_mtod_offset(req_data, void*, book_mark_offset);
              rte_memcpy(dst,p,n);
            }
            size_t content_size(){
              return offset - (sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr)) - book_mark_len -1;
            }
            rte_mbuf* get_mbuf(){
              return req_data;
            }
            void format_header(){
              if(offset < 64){
                void* dst = rte_pktmbuf_mtod_offset(req_data, void*, offset);
                rte_memcpy(dst,pad,64-offset);
                offset = 64;
              }

              uint32_t content_size = offset - (sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr)) - book_mark_len -1;
               void* dst = rte_pktmbuf_mtod_offset(req_data, void*, book_mark_offset);
              rte_memcpy(dst,&content_size,sizeof(uint32_t));
              req_data->data_len = offset;
              req_data->pkt_len = offset;
              ipv4_hdr->total_length = htons(offset -  (sizeof(struct rte_ether_hdr)));
              udp_hdr->dgram_len = htons(offset -  (sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) ));
            }
            void set_pkt_type_sm(){
              uint8_t* dst = rte_pktmbuf_mtod(req_data, uint8_t*);
              dst+=sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr);
              uint8_t pkt_type = 0x07;
              rte_memcpy(dst,&pkt_type,1);
            }
            std::string print_request(){
               char* req = new char[1024];
               uint8_t* pkt_data = rte_pktmbuf_mtod(req_data, uint8_t*);
               int j=0;
               for(int i=  (sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr));
                        i < offset; i++){
                  
                    sprintf(req+j,"%02x ", pkt_data[i]);
                    j+=3;   
                    if(j%25==0){
                      req[j] = '\n';
                      j++;
                    } 
                }
                req[j] = 0;
                return std::string(req);
            }
            void* get_offset(){
              return rte_pktmbuf_mtod_offset(req_data, void*, offset);
            }
            void set_pkt_type_bg(){
              uint8_t* dst = rte_pktmbuf_mtod(req_data, uint8_t*);
              dst+=sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr);
             
              *dst = 0xa;
            }

    };


inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const rrr::i8 &v) {
verify(m.write(&v, sizeof(v)) == sizeof(v));
return m;
}

inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const rrr::i16 &v) {
  verify(m.write(&v, sizeof(v)) == sizeof(v));
  return m;
}

inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const rrr::i32 &v) {
  verify(m.write(&v, sizeof(v)) == sizeof(v));
  return m;
}

inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const rrr::i64 &v) {
  verify(m.write(&v, sizeof(v)) == sizeof(v));
  return m;
}

inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const rrr::v32 &v) {
  m << v.get();
  return m;
}

inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const rrr::v64 &v) {
 
  m << v.get();
  return m;
}

inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const uint8_t &u) {
  verify(m.write(&u, sizeof(u)) == sizeof(u));
  return m;
}

inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const uint16_t &u) {
  verify(m.write(&u, sizeof(u)) == sizeof(u));
  return m;
}

inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const uint32_t &u) {
  verify(m.write(&u, sizeof(u)) == sizeof(u));
  return m;
}

inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const uint64_t &u) {
  verify(m.write(&u, sizeof(u)) == sizeof(u));
  return m;
}

inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const double &v) {
  verify(m.write(&v, sizeof(v)) == sizeof(v));
  return m;
}

inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const std::string &v) {
  v64 v_len = v.length();
  m << v_len;
  if (v_len.get() > 0) {
    verify(m.write(v.c_str(), v_len.get()) == (size_t) v_len.get());
  }
  return m;
}

template<class T1, class T2>
inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const std::pair<T1, T2> &v) {
  m << v.first;
  m << v.second;
  return m;
}

template<class T>
inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const std::vector<T> &v) {
  v64 v_len = v.size();
  m << v_len;
  for (typename std::vector<T>::const_iterator it = v.begin(); it != v.end();
       ++it) {
       
    m << *it;
  }
  return m;
}

template<class T>
inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const std::list<T> &v) {
  v64 v_len = v.size();
  m << v_len;
  for (typename std::list<T>::const_iterator it = v.begin(); it != v.end();
       ++it) {
    m << *it;
  }
  return m;
}

template<class T>
inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const std::set<T> &v) {
  v64 v_len = v.size();
  m << v_len;
  for (typename std::set<T>::const_iterator it = v.begin(); it != v.end();
       ++it) {
    m << *it;
  }
  return m;
}

template<class K, class V>
inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const std::map<K, V> &v) {
  v64 v_len = v.size();
  m << v_len;
  for (typename std::map<K, V>::const_iterator it = v.begin(); it != v.end();
       ++it) {
    m << it->first << it->second;
  }
  return m;
}

template<class T>
inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m,
                                const std::unordered_set<T> &v) {
  v64 v_len = v.size();
  m << v_len;
  for (typename std::unordered_set<T>::const_iterator it = v.begin();
       it != v.end(); ++it) {
    m << *it;
  }
  return m;
}

template<class K, class V>
inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m,
                                const std::unordered_map<K, V> &v) {
  v64 v_len = v.size();
  m << v_len;
  for (typename std::unordered_map<K, V>::const_iterator it = v.begin();
       it != v.end(); ++it) {
    m << it->first << it->second;
  }
  return m;
}

inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal & m, rrr::Marshal m2){
  m2.read(m.get_offset(),m.content_size());
  return m;
}

inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, rrr::i8 &v) {
  verify(m.read(&v, sizeof(v)) == sizeof(v));
  return m;
}

inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, rrr::i16 &v) {
  verify(m.read(&v, sizeof(v)) == sizeof(v));
  return m;
}

inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, rrr::i32 &v) {
  verify(m.read(&v, sizeof(v)) == sizeof(v));
  return m;
}

inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, rrr::i64 &v) {
  verify(m.read(&v, sizeof(v)) == sizeof(v));
  return m;
}

inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, rrr::v32 &v) {
  verify(m.read(&v, sizeof(v)) == sizeof(v));
  return m;
}

inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, rrr::v64 &v) {

  verify(m.read(&v, sizeof(v)) == sizeof(v));
  return m;
}

inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, uint8_t &u) {
  verify(m.read(&u, sizeof(u)) == sizeof(u));
  return m;
}

inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, uint16_t &u) {
  verify(m.read(&u, sizeof(u)) == sizeof(u));
  return m;
}

inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, uint32_t &u) {
  verify(m.read(&u, sizeof(u)) == sizeof(u));
  return m;
}

inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, uint64_t &u) {
  verify(m.read(&u, sizeof(u)) == sizeof(u));
  return m;
}

inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, double &v) {
  verify(m.read(&v, sizeof(v)) == sizeof(v));
  return m;
}

inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, std::string &v) {
  v64 v_len;
  m >> v_len;
  v.resize(v_len.get());
  if (v_len.get() > 0) {
    verify(m.read(&v[0], v_len.get()) == (size_t) v_len.get());
  }
  return m;
}

template<class T1, class T2>
inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, std::pair<T1, T2> &v) {
  m >> v.first;
  m >> v.second;
  return m;
}

template<class T>
inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, std::vector<T> &v) {
  v64 v_len;
  m >> v_len;
  v.clear();
  v.reserve(v_len.get());
  for (int i = 0; i < v_len.get(); i++) {
    T elem;
    m >> elem;
    v.push_back(elem);
  }
  return m;
}

template<class T>
inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, std::list<T> &v) {
  v64 v_len;
  m >> v_len;
  v.clear();
  for (int i = 0; i < v_len.get(); i++) {
    T elem;
    m >> elem;
    v.push_back(elem);
  }
  return m;
}

template<class T>
inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, std::set<T> &v) {
  v64 v_len;
  m >> v_len;
  v.clear();
  for (int i = 0; i < v_len.get(); i++) {
    T elem;
    m >> elem;
    v.insert(elem);
  }
  return m;
}

template<class K, class V>
inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, std::map<K, V> &v) {
  v64 v_len;
  m >> v_len;
  v.clear();
  for (int i = 0; i < v_len.get(); i++) {
    K key;
    V value;
    m >> key >> value;
    insert_into_map(v, key, value);
  }
  return m;
}

template<class T>
inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, std::unordered_set<T> &v) {
  v64 v_len;
  m >> v_len;
  v.clear();
  for (int i = 0; i < v_len.get(); i++) {
    T elem;
    m >> elem;
    v.insert(elem);
  }
  return m;
}

template<class K, class V>
inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, std::unordered_map<K, V> &v) {
  v64 v_len;
  m >> v_len;
  v.clear();
  for (int i = 0; i < v_len.get(); i++) {
    K key;
    V value;
    m >> key >> value;
    insert_into_map(v, key, value);
  }
  return m;
}








}