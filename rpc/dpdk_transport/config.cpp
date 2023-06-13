#pragma once
#include "config.hpp"
#include <unistd.h>
#include <boost/algorithm/string.hpp>
namespace rrr{
Config* Config::config_s = nullptr;

Config* Config::get_config() {
    assert(config_s != nullptr);
    return config_s;
}

int Config::create_config(int argc, char** argv) {
    if (config_s != nullptr) return -1;

    config_s = new Config();

    char* end_ptr = nullptr;

    int c;
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
    return 0;
}

void Config::load_cfg_files() {
    for (auto& filename : config_paths_) {
        if (boost::algorithm::ends_with(filename, "yml")) {
            load_yml(filename);
        } else {
            assert(false);
        }
    }

    assert(cpu_info_.core_per_numa > 1);
    cpu_info_.compute_maxs(dpdk_rxtx_threads_ratio_);
}

void Config::load_yml(std::string& filename) {
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

void Config::load_network_yml(YAML::Node config) {
    for (const auto& it : config) {
        for (const auto& net_it : it) {
            NetworkInfo net;
            auto& info = net_it.second;

            net.name = net_it.first.as<std::string>();
            net.id = info["id"].as<int>();
            net.mac = info["mac"].as<std::string>();
            net.ip = info["ip"].as<std::string>();
            net.port = info["port"].as<uint32_t>();

            net_info_.push_back(net);
        }
    }
}

void Config::load_dpdk_yml(YAML::Node config) {
    dpdk_options_ = config["option"].as<std::string>();
    dpdk_rxtx_threads_ratio_ = config["rxtx_thread"].as<float>();
}

void Config::load_cpu_yml(YAML::Node config) {
    Log_info("Loading CPU Config");
    cpu_info_.numa = config["numa"].as<int>();
    cpu_info_.core_per_numa = config["core_per_numa"].as<int>();
}

void Config::load_host_yml(YAML::Node config) {
    host_name_ = config["name"].as<std::string>();
}

void Config::load_server_yml(YAML::Node config) {
    default_server_ = config["default_server"].as<int>();
    server_update_path_ = config["update_path"].as<std::string>();
}


}
