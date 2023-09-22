#include <counter.h>
#include <pthread.h>
#include <stdlib.h>
#include "benchmarks.hpp"
//using namespace ;

int main(int argc, char **argv) {
    
    rrr::Config::create_config(argc, argv);
    rrr::Config* conf = rrr::Config::get_config();
    
    #ifdef DPDK
     rrr::DpdkTransport::create_transport(conf);
    #endif
    
    Benchmarks bm(conf);

    bm.create_proxies();
    //bm.create_client_threads();
    bm.observe_client();
    bm.stop_client();
    
    
}
