#ifndef _APP_CONFIG_H_
#define _APP_CONFIG_H_
#include <string>
#include <cstdint>
#include <yaml-cpp/yaml.h>

class AppConfig
{
private:
public:
    uint16_t connections;

    
private:
    std::vector<std::string> config_paths_;

public:
    static AppConfig *config_s;




    // benchmarks yml
    uint16_t num_client_threads_;
    uint16_t client_poll_threads_ = 1;
    uint16_t server_poll_threads_ = 1;
    uint16_t client_connections_ = 1;
    uint16_t input_size_ = 64;
    uint16_t output_size_ = 64;
    uint16_t client_duration_ = 20;
    uint16_t server_duration_ = 30;
    std::string server_address_;
    uint16_t client_batch_size_ = 3;

    std::vector<uint16_t> core_affinity_mask_;
    //

    uint16_t buffer_len = 8 * 1024 - 2;

private:
    void load_cfg_files();
    void load_yml(std::string &filename);
    void load_benchmark_yml(YAML::Node config);
    // void load_partition_type(YAML::Node config);

public:
    static int create_config(int argc, char **argv);
    static AppConfig *get_config();

};

#endif