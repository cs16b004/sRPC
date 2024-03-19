#include "AppConfig.hpp"
#include <unistd.h>
#include <rrr.hpp>
#include <boost/algorithm/string.hpp>

AppConfig* AppConfig::config_s = nullptr;

AppConfig* AppConfig::get_config() {
    assert(config_s != nullptr);
    return config_s;
}

int AppConfig::create_config(int argc, char** argv) {
    if (config_s != nullptr) return -1;

    config_s = new AppConfig();
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

void AppConfig::load_cfg_files() {
    for (auto& filename : config_paths_) {
        if (boost::algorithm::ends_with(filename, "yml")) {
            load_yml(filename);
        } else {
            assert(false);
        }
    }
}

void AppConfig::load_yml(std::string& filename) {
    rrr::Log::info("Loading application configuration from : %s",filename.c_str());
    YAML::Node config = YAML::LoadFile(filename);
    
    load_benchmark_yml(config["benchmarks"]);
}

void AppConfig::load_benchmark_yml(YAML::Node config){
    server_poll_threads_ = config["server_poll_threads"].as<int>();
    client_poll_threads_ = config["client_poll_threads"].as<int>();
    client_connections_ = config["client_connections"].as<std::uint16_t>();
    num_client_threads_ = config["client_threads"].as<uint16_t>();
    input_size_ = config["input_size"].as<std::uint16_t>();
    output_size_ = config["output_size"].as<std::uint16_t>();
    client_duration_ = config["client_duration"].as<std::uint16_t>();
    server_duration_ = config["server_duration"].as<std::uint16_t>();

    server_address_ = config["server_address"].as<std::string>();
    client_batch_size_ = config["client_batch_size"].as<uint16_t>();
    rate = config["rate"].as<std::uint64_t>();
    rrr::Log::info("Loading Affinity ");
    core_affinity_mask_ = config["core_affinity"].as<std::vector<uint16_t>>();

    exp_name = config["experiment"].as<std::string>();

    if(core_affinity_mask_.size() <=0){
        rrr::Log::warn("AFFINITY MASK NOT PROVIDED Setting Default 0, 32");
        core_affinity_mask_.push_back(0);
        core_affinity_mask_.push_back(32);
    }


}

