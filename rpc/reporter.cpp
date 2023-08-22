#include "reporter.hpp"
#include <unistd.h>

namespace rrr{


void Reporter::launch(){
    Pthread_create(recorder,nullptr,this->run,this);
    Pthread_join(*recorder,nullptr);
}


void* Reporter::run(void* arg){
    Reporter* reporter = (Reporter*)arg;
    Log::info("Reporter Thread Launched, Observing %d poll threads", reporter->pm_->n_threads_);
    uint64_t job_count=0;
    uint64_t diff_count=0;
    while(! reporter->stop){
        usleep(reporter->period_ * 1000);
        
        for(int i=0;i<reporter->pm_->n_threads_;i++){
            for(auto poll_job: reporter->pm_->poll_threads_[i]->poll_set_){
                job_count+= poll_job->read_and_set_counter(0);
            
            } //ith pollable of poll manager;
        }
        
        Log_info("Total RPCs: %d, Throughput %f/s",job_count, job_count*1000.0/(reporter->period_) );
        job_count=0;
        diff_count=0;
    }
    Log::info("Reporter Thread Stopped");

}
}