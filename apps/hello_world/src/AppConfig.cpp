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
    load_app_yml(config["app"]);
    
}

void AppConfig::load_app_yml(YAML::Node config){
    client_duration_ = config["client_duration"].as<std::uint16_t>();
    server_duration_ = config["server_duration"].as<std::uint16_t>();

    server_address_ = config["server_address"].as<std::string>();
   
}

