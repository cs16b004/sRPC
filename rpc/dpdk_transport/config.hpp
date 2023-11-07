#ifndef _CONFIG_H_
#define _CONFIG_H_
#include <string>
#include <cstdint>
#include "../../base/logging.hpp"
#include <yaml-cpp/yaml.h>
namespace rrr{
class Config {
private:

public:
    #ifdef RPC_STATISTICS
        uint16_t connections;
    #endif
    struct NetworkInfo {
        std::string name;
        int id;
        std::string mac;
        std::string ip;
        uint32_t port;
        std::string to_string(){
            std::stringstream ss;
            ss<<"[ Name: "<<name<<"\n  Id : "<<id<<"\n  MAC: "<<mac<<"\n  IP: "<<ip<<"\n  Port : "<<port<<"\n";
            return ss.str();
        }
    };

    /* TODO: use system info instead of file for boundries; Specially when
     * there is more than one NUMA core */
    struct CpuInfo {
        int numa;
        int core_per_numa;
        int max_rx_threads;
        int max_tx_threads;
        void compute_maxs(float rxtx_ratio) {
            float total_cores = (float) numa * (float) core_per_numa;
            float total_ratio = rxtx_ratio + 1.0;
            max_tx_threads = (int) (total_cores / total_ratio);
            max_rx_threads = (int) ((rxtx_ratio * total_cores) / total_ratio);

            assert(max_rx_threads + max_tx_threads <= (int) total_cores);
            Log_debug("max tx threads %d, max rx threads %d",
                      max_tx_threads, max_rx_threads);
        }
    };

private:
    std::vector<std::string> config_paths_;

public:
    static Config* config_s;

    std::string host_name_;

    int host_threads_ = 1;

    std::string name_;
    std::vector<NetworkInfo> net_info_;
    std::string dpdk_options_;
    CpuInfo cpu_info_;
    std::vector<uint16_t> core_affinity_mask_;
  

    uint16_t num_tx_threads_;
    uint16_t num_rx_threads_;
    uint16_t burst_size=32;
    //benchmarks yml
    uint16_t num_client_threads_;
    uint16_t client_poll_threads_ = 1;
    uint16_t server_poll_threads_ = 1;
    uint16_t client_connections_=1;
    uint16_t input_size_ = 64;
    uint16_t output_size_ = 64;
    uint16_t client_duration_ = 20;
    uint16_t server_duration_ = 60;
    std::string server_address_;
    uint32_t rte_ring_size = 32768*2;
    uint16_t client_batch_size_=5;
    //

    
    uint16_t buffer_len=8*4096-2;

 

private:
    void load_cfg_files();
    void load_yml(std::string& filename);
    void load_network_yml(YAML::Node config);
    void load_dpdk_yml(YAML::Node config);
    void load_cpu_yml(YAML::Node config);
    void load_host_yml(YAML::Node config);
    void load_server_yml(YAML::Node config);
    void load_benchmark_yml(YAML::Node config);
   // void load_partition_type(YAML::Node config);

public:
    static int create_config(int argc, char** argv);
    static Config* get_config();

    const char* get_dpdk_options() const {
        return dpdk_options_.c_str();
    }
    Config::CpuInfo get_cpu_info() const {
        return cpu_info_;
    }
    std::vector<Config::NetworkInfo> get_net_info() const {
        return net_info_;
    }
    int get_host_threads() const {
        return host_threads_;
    }
};
}
#endif