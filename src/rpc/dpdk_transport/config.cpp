#include "config.hpp"
#include <unistd.h>

#include <boost/algorithm/string.hpp>
namespace rrr{
RPCConfig* RPCConfig::config_s = nullptr;

RPCConfig* RPCConfig::get_config() {
    assert(config_s != nullptr);
    return config_s;
}

int RPCConfig::create_config(int argc, char** argv) {
    if (config_s != nullptr) return -1;

    std::vector<std::string> all_args(argv, argv + argc);
    std::vector<std::string> rpc_opts;
    std::vector<std::string> app_opts;
    int i=0;
    for( i=0; i< argc; i++){
        if(all_args[i] == "--")
            break;
        rpc_opts.push_back(all_args[i]);
    }
    for(; i< argc; i++){
        app_opts.push_back(all_args[i]);
    }
    config_s = new RPCConfig();
    int c;
    int rpc_argc = rpc_opts.size();
    char ** rpc_argv = new char*[rpc_argc];
    for(i=0;i<rpc_argc;i++){
        rpc_argv[i] = new char[256];
    }
    i=0;
    // for(std::string opt: rpc_opts){
    //     strcpy (rpc_argv[i] ,rpc_opts[i].c_str());
    //     i++;
    // }
    std::string filename;
    while ((c = getopt(argc, argv, "f:")) != -1) {
        switch(c) {
        case 'f':
            filename = std::string(optarg);
            config_s->config_paths_.push_back(filename);
            break;

        case '?':
            assert(0);
            break;

        default:
            assert(false);
        }
    }

    config_s->load_cfg_files();
    // for( i=0; i <app_opts.size(); i++ ){
    //     strcpy(argv[i], app_opts[i].c_str());
    // }
    // argv[0] = "aa";
    // optind=0;
    return app_opts.size();
}

void RPCConfig::load_cfg_files() {
    for (auto& filename : config_paths_) {
        if (boost::algorithm::ends_with(filename, "yml")) {
            load_yml(filename);
        } else {
            assert(false);
        }
    }

    assert(cpu_info_.core_per_numa > 1);
}

void RPCConfig::load_yml(std::string& filename) {
    Log_info("Loading configuration from : %s",filename.c_str());
    YAML::Node config = YAML::LoadFile(filename);

    if (config["network"])
        load_network_yml(config["network"]);

    if (config["dpdk"])
        load_dpdk_yml(config["dpdk"]);

    if (config["host"])
        load_host_yml(config["host"]);

    if (config["cpu"])
        load_cpu_yml(config["cpu"]);

    if (config["server"])
        load_server_yml(config["server"]);
}

void RPCConfig::load_network_yml(YAML::Node config) {
    //string mac_s;

    for (const auto& it : config) {
        for (const auto& net_it : it) {
            NetworkInfo net;
            auto& info = net_it.second;

            net.name = net_it.first.as<std::string>();
            net.id = info["id"].as<int>();
            mac_from_str( info["mac"].as<std::string>().c_str(), net.mac);
            net.ip =  ipv4_from_str(info["ip"].as<std::string>().c_str());
            net.port = info["port"].as<uint16_t>();
            net_info_.push_back(net);
        }
    }
}

void RPCConfig::load_dpdk_yml(YAML::Node config) {
    dpdk_options_ = config["option"].as<std::string>();
    num_threads_ = config["num_threads"].as<uint16_t>();
    burst_size = config["pkt_burst_size"].as<uint16_t>();
}

void RPCConfig::load_cpu_yml(YAML::Node config) {
    Log_info("Loading CPU Config");
    cpu_info_.numa = config["numa"].as<int>();
    cpu_info_.core_per_numa = config["core_per_numa"].as<int>();
}

void RPCConfig::load_host_yml(YAML::Node config) {
    host_name_ = config["name"].as<std::string>();
    core_affinity_mask_ = config["thread_affinity"].as<std::vector<uint16_t>>();
    
}

void RPCConfig::load_server_yml(YAML::Node config) {
    
}
}
