#include <counter.h>
#include <pthread.h>
#include <stdlib.h>
#include "benchmarks.hpp"
//using namespace ;

int main(int argc, char **argv) {
    
    rrr::RPCConfig::create_config(argc, argv);

    AppConfig::create_config(argc, argv);
    rrr::RPCConfig* conf = rrr::RPCConfig::get_config();

   
    

    
    
    #ifdef DPDK
     rrr::DpdkTransport::create_transport(conf);
    #endif


    while(!(rrr::DpdkTransport::get_transport()->initialized())){
        ;
    }
    LOG_DEBUG("Tranport at %p", rrr::DpdkTransport::get_transport());

    Benchmarks bm(AppConfig::get_config());
     pthread_t curr_th = pthread_self();
     bm.set_cpu_affinity(&curr_th);

  
    bm.create_proxies();

    bm.create_client_threads();
    bm.observe_client();
    
    bm.stop_client();
    
    

    
}
