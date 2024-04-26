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


    uint16_t client_duration_ = 20;
    uint16_t server_duration_ = 30;
    std::string server_address_;

private:
    void load_cfg_files();
    void load_yml(std::string &filename);
    void load_app_yml(YAML::Node config);
    // void load_partition_type(YAML::Node config);

public:
    static int create_config(int argc, char **argv);
    static AppConfig *get_config();

};

#endif