#include <counter.h>
#include <pthread.h>
#include <stdlib.h>
#include "benchmarks.hpp"
//using namespace ;

int main(int argc, char **argv) {
    
    rrr::RPCConfig::create_config(argc, argv);

    AppConfig::create_config(argc, argv);
    rrr::RPCConfig* conf = rrr::RPCConfig::get_config();
    while(1){
      //  Log_info(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n thread cpu %d\n\n>>>>>>>>>>>>>>>>>>>>>>\n",sched_getcpu());
        if(sched_getcpu() >= (conf->cpu_info_.numa)*(conf->cpu_info_.core_per_numa)
                || sched_getcpu() <= (conf->cpu_info_.numa +1)*(conf->cpu_info_.core_per_numa) ){
            break;
        }else{
            Log_warn("Waiting for scheduled on right node");
            sleep(1);
        }
    }
    
    #ifdef DPDK
     rrr::DpdkTransport::create_transport(conf);
    #endif


    while(!(rrr::DpdkTransport::get_transport()->initialized())){
        ;
    }
    LOG_DEBUG("Tranport at %p", rrr::DpdkTransport::get_transport());

    Benchmarks bm(AppConfig::get_config());

  
    bm.create_proxies();

    bm.create_client_threads();
    bm.observe_client();
    #ifdef DPDK
         rrr::DpdkTransport::get_transport()->trigger_shutdown();
         rrr::DpdkTransport::get_transport()->shutdown();
    #endif
    bm.stop_client();
    

    
}
