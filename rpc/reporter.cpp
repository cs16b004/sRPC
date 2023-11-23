#include "reporter.hpp"
#include <unistd.h>

namespace rrr{
 
void Reporter::launch(){
    Pthread_create(recorder,nullptr,Reporter::run,this);

}

void* Reporter::run(void* arg){
    Reporter* reporter = (Reporter*)arg;
    Log::info("Reporter Thread Launched, Observing  poll threads" );
    int i=0;
    while(! reporter->stop){
        
        usleep(reporter->period_ * 1000);  
        
    }
    Log::info("Reporter Thread Stopped");
}

}//rrr