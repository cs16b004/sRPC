#include "reporter.hpp"
#include <unistd.h>

namespace rrr{
 double diff_timespec(const struct timespec &time1, const struct timespec &time0) {
        if (time1.tv_sec - time0.tv_sec ==0)
            return (time1.tv_nsec - time0.tv_nsec);
        else{
            //Log_info("Difference in seconds !!!! %d",time1.tv_sec - time0.tv_sec);
            return 1000*1000*1000.0 + time1.tv_nsec - time0.tv_nsec ;
        }
 }
void Reporter::launch(){
    Pthread_create(recorder,nullptr,this->run,this);
    Pthread_join(*recorder,nullptr);
}
double compute_avg(std::unordered_map<uint64_t,std::timespec>& start_book,
                std::unordered_map<uint64_t,std::timespec>& end_book){
        
        uint64_t count=0;
          
                double_t nano_diff_sum = 0;
                uint64_t freq = 0;
               // Log_info("Read from %p",&end_book);
               if(end_book.empty()){
                return 0.0;
               }
                for(auto rec : end_book){
                    if(start_book.find(rec.first) != start_book.end()){
                        nano_diff_sum+= diff_timespec(rec.second,start_book[rec.first])/end_book.size(); 
                        freq++;
                    }
                }
                double_t avg_lat= nano_diff_sum;
                Log_info("Sample Size : %d Average Latency in nano sec: %lf",freq,avg_lat);
                return avg_lat;


}
std::vector<double> compute_percentile(std::unordered_map<uint64_t,std::timespec>& start_book,
    std::unordered_map<uint64_t,std::timespec>& end_book){
            
    std::vector<double> percentiles;
    std::vector<double>sample;
    if(end_book.empty()){
        return percentiles;
    }
    for(auto rec : end_book){
        if(start_book.find(rec.first) != start_book.end()){
            sample.push_back(diff_timespec(rec.second,start_book[rec.first])); 
                      
        }
    }
    //sorted_sample
    std::sort(std::begin(sample), std::end(sample), std::less<double>{});
    uint64_t sample_size = sample.size();
    uint64_t mid  = sample_size/2;
    double median = (sample_size%2==1)? sample[mid]: (sample[mid]+sample[mid+1])/2;
    uint64_t percentile_9999th_i  =  (9999*sample_size%10000 == 0)? 9999*sample_size/10000: (9999*sample_size/10000+1); 
    double percentile_9999th = sample[percentile_9999th_i];
    percentiles.push_back(median);
    percentiles.push_back(percentile_9999th);
    Log_info("Sample size %d, Median Latency: %f 99.99th: %f",sample_size,median,percentile_9999th);
    return percentiles;           
        
}
std::timespec deepCopyTimespec(const std::timespec& src) {
    std::timespec copy;
    copy.tv_sec = src.tv_sec;
    copy.tv_nsec = src.tv_nsec;
    return copy;
}
void* Reporter::run(void* arg){
    Reporter* reporter = (Reporter*)arg;
    Log::info("Reporter Thread Launched, Observing %d poll threads", reporter->pm_->n_threads_);
    uint64_t job_count=0;
    uint64_t last_job_count=0;
    uint64_t diff_count=0;
  
    std::unordered_map<uint64_t, std::timespec> start_book_copy;
    std::unordered_map<uint64_t, std::timespec> end_book_copy;
    while(! reporter->stop){
        usleep(reporter->period_ * 1000);
        #ifdef RPC_MICRO_STATISTICS
            for(auto entry: reporter->tl->pkt_process_ts){
                Log_debug("diff for id %d -> %d cycles",entry.first, entry.second - reporter->tl->pkt_rx_ts[entry.first] );
            }

        #endif
        double  lat_avg=0;
        double poll_count=0;
        for(int i=0;i<reporter->pm_->n_threads_;i++){
            for(auto poll_job: reporter->pm_->poll_threads_[i]->poll_set_){
               
                // deep copy the books to avoid locks;
                if(reporter->is_client){
                    poll_job->ts_lock.lock();
                    for (const auto& pair : poll_job->end_book) {
                        end_book_copy[pair.first] = deepCopyTimespec(pair.second);
                    }
                    for(const auto& pair: poll_job->start_book){
                        start_book_copy[pair.first] = deepCopyTimespec(pair.second);
                    }
                    poll_job->end_book.clear();
                    poll_job->start_book.clear();
                    poll_count++;
                    poll_job->ts_lock.unlock();
                    
                    lat_avg+= rrr::compute_avg(start_book_copy,end_book_copy);
                    
                    compute_percentile(start_book_copy,end_book_copy);

                    start_book_copy.clear();
                    end_book_copy.clear();
                    
                }
                else
                     job_count+= poll_job->read_and_set_counter(0);
            }

        }
            if(reporter->is_client)
                Log_info("Across all poll, Average Latency %f micro-sec",lat_avg/poll_count);
            else
                Log_info("Total RPCs: %d, Throughput %f/s",job_count-last_job_count, (job_count - last_job_count)*1000.0/(reporter->period_) );
            last_job_count = job_count;
            job_count=0;
            diff_count=0; //ith pollable of poll manager;
    }
    Log::info("Reporter Thread Stopped");
}
}