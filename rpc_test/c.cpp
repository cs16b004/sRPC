#include <counter.h>
#include <pthread.h>
#include <stdlib.h>
#include "benchmarks.hpp"
//using namespace ;

int main(int argc, char **argv) {
    
    rrr::RPCConfig::create_config(argc, argv);
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


    Benchmarks bm(conf);

  
    bm.create_proxies();

    bm.create_client_threads();
    bm.observe_client();
    bm.stop_client();
   \
    
}
