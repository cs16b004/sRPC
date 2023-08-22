#include <counter.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include "benchmarks.hpp"


int main(int argc, char **argv) {
    
    rrr::Config::create_config(argc, argv);
    rrr::Config* conf = rrr::Config::get_config();
    
    #ifdef DPDK
     rrr::DpdkTransport::create_transport(conf);
    #endif
    
    Benchmarks bm(conf);
    bm.create_server();
    
    return 0;
}
