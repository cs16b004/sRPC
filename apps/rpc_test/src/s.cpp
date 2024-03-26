#include <counter.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include "benchmarks.hpp"


int main(int argc, char **argv) {
    

    rrr::RPCConfig::create_config(argc, argv); 
    AppConfig::create_config(argc, argv);

    

    AppConfig* appConf = AppConfig::get_config();
    rrr::RPCConfig* conf = rrr::RPCConfig::get_config();
    
    #ifdef DPDK
    rrr::DpdkTransport::create_transport(conf);
    #endif

    Benchmarks bm(appConf);
    pthread_t curr_th = pthread_self();
     bm.set_cpu_affinity(&curr_th);
     
    bm.create_server();
    bm.observe_server();    
    #ifdef DPDK
         rrr::DpdkTransport::get_transport()->trigger_shutdown();
         
    #endif
    bm.stop_server();
    
    #ifdef DPDK
    rrr::DpdkTransport::get_transport()->shutdown();
    #endif
    bm.stop_server_loop();
    return 0;
}
