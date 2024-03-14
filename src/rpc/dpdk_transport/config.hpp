#ifndef _CONFIG_H_
#define _CONFIG_H_
#include <string>
#include <cstdint>
#include "../../base/logging.hpp"
#include "utils.hpp"
#include <yaml-cpp/yaml.h>

namespace rrr
{
    class RPCConfig
    {
    private:
    public:
#ifdef RPC_STATISTICS
        uint16_t connections;
#endif
        struct NetworkInfo
        {
            std::string name;
            int id;
            uint8_t *mac;
            uint32_t ip;
            uint16_t port;
            NetworkInfo(){
                id=0;
                mac = new uint8_t[6];
                ip=0;
                port=0;
            }
            std::string to_string()
            {
                std::stringstream ss;
                ss << "[ Name: " << name << "\n  Id : " << id << "\n  MAC: " << mac_to_string(mac) << "\n  IP: " << ipv4_to_string(ip) << "\n  Port : " << port << "\n";
                return ss.str();
            }
        };
        struct CpuInfo
        {
            int numa;
            int core_per_numa;
        };

    private:
        std::vector<std::string> config_paths_;

    public:
        static RPCConfig *config_s;

        std::string host_name_;

        int host_threads_ = 1;

        std::string name_;
        std::vector<NetworkInfo> net_info_;
        std::string dpdk_options_;
        CpuInfo cpu_info_;
        std::vector<uint16_t> core_affinity_mask_;

        uint16_t num_threads_;
        uint16_t burst_size = 32;
        uint32_t rte_ring_size = 8192*4*2;
        //

        uint16_t buffer_len = 4 * 8024 - 2;

    private:
        void load_cfg_files();
        void load_yml(std::string &filename);
        void load_network_yml(YAML::Node config);
        void load_dpdk_yml(YAML::Node config);
        void load_cpu_yml(YAML::Node config);
        void load_host_yml(YAML::Node config);
        void load_server_yml(YAML::Node config);
        // void load_partition_type(YAML::Node config);

    public:
        static int create_config(int argc, char **argv);
        static RPCConfig *get_config();

        const char *get_dpdk_options() const
        {
            return dpdk_options_.c_str();
        }
        RPCConfig::CpuInfo get_cpu_info() const
        {
            return cpu_info_;
        }
        std::vector<RPCConfig::NetworkInfo> get_net_info() const
        {
            return net_info_;
        }
        int get_host_threads() const
        {
            return host_threads_;
        }
    };
}
#endif