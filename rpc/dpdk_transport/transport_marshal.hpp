
#include<string>
#include<unordered_map>
#include<map>
#include<vector>
#include<set>
#include<unordered_set>
#include "../..//base/all.hpp"
#include <rte_mbuf_core.h>
#include <rte_mempool.h>
#include <rte_ip.h>
#include <rte_ether.h>
#include <rte_udp.h>
namespace rrr{
    class TransportMarshal{
        private: 
            rte_mbuf* req_data;
            uint16_t book_mark_offset=0;
            uint16_t book_mark_len=0;
            uint16_t offset =  sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr);
        public:
            TransportMarshal(rte_mbuf* req): req_data(req){}
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
              return offset - sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) - book_mark_len;
            }
            rte_mbuf* get_mbuf(){
              return req_data;
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
  char buf[5];
  size_t bsize = rrr::SparseInt::dump(v.get(), buf);
  verify(m.write(buf, bsize) == bsize);
  return m;
}

inline rrr::TransportMarshal &operator<<(rrr::TransportMarshal &m, const rrr::v64 &v) {
  char buf[9];
  size_t bsize = rrr::SparseInt::dump(v.get(), buf);
  verify(m.write(buf, bsize) == bsize);
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
  char byte0;
  verify(m.peek(&byte0, 1) == 1);
  size_t bsize = rrr::SparseInt::buf_size(byte0);
  char buf[5];
  verify(m.read(buf, bsize) == bsize);
  i32 val = rrr::SparseInt::load_i32(buf);
  v.set(val);
  return m;
}

inline rrr::TransportMarshal &operator>>(rrr::TransportMarshal &m, rrr::v64 &v) {
  char byte0;
  verify(m.peek(&byte0, 1) == 1);
  size_t bsize = rrr::SparseInt::buf_size(byte0);
  char buf[9];
  verify(m.read(buf, bsize) == bsize);
  i64 val = rrr::SparseInt::load_i64(buf);
  v.set(val);
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