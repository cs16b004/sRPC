#include<iostream>

#include<ctime>
#include <pthread.h>

#include "polling.hpp"
#include "base/threading.hpp"
#include "base/logging.hpp"
#include "dpdk_transport/transport.hpp"
#include <unordered_map>
namespace rrr{
class PollThread;

class Reporter{
    public:
        uint16_t period_; //period in milliseconds
        rrr::PollMgr* pm_ ;
        bool is_client = false;
        #ifdef DPDK
        DpdkTransport *tl;

        #endif
       // rrr::ThreadPool* thp_ ; //threadpool to collect stats from 
         //poll manager to collect stats from
        
        pthread_t* recorder;
        bool stop=false;

    public:
        Reporter(uint16_t period, rrr::PollMgr* pm, bool is_clt): period_(period), pm_(pm), is_client(is_clt){
                recorder = new pthread_t;
                #ifdef DPDK
                tl = DpdkTransport::get_transport();
                #endif
            }
        void launch();
        static void* run(void* arg);
        
        void trigger_shutdown(){
            stop = true;
            void ** ret;
            Pthread_join(*recorder, ret);
        }
       // double compute_avg(std::unordered_map<uint64_t,std::timespec>& end_book, 
       //         std::unordered_map<uint64_t,std::timespec>& start_book);
        
};
}