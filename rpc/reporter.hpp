#include<iostream>

#include<ctime>
#include <pthread.h>

#include "polling.hpp"
#include "base/threading.hpp"
#include "base/logging.hpp"


namespace rrr{
class PollThread;
class Reporter{
    protected:
        uint16_t period_; //period in milliseconds

       // rrr::ThreadPool* thp_ ; //threadpool to collect stats from 
        rrr::PollMgr* pm_ ; //poll manager to collect stats from
        
        pthread_t* recorder;
        bool stop=false;

    public:
        Reporter(uint16_t period, rrr::PollMgr* pm): period_(period), pm_(pm){
                recorder = new pthread_t;
                
            }
        void launch();
        static void* run(void* arg);
        
        void trigger_shutdown(){
            stop = true;
        }
        
};
}