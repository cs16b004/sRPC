#pragma once

#include <sstream>

#include <iostream>
#include <chrono>
#include <unordered_map>
#include <map>
#include <iomanip>
#include <ctime> 

namespace rrr {

class AvgStat {
public:
    int64_t n_stat_;
    int64_t sum_;
    int64_t avg_;
    int64_t max_;
    int64_t min_;

    AvgStat(): n_stat_(0), sum_(0), avg_(0), max_(0), min_(0) {}
    
    void sample(int64_t s = 1) {
        ++n_stat_;
        sum_ += s;
        avg_ = sum_ / n_stat_;
        max_ = s > max_ ? s : max_;
        min_ = s < min_ ? s : min_;
    }

    void clear() {
        n_stat_ = 0;
        sum_ = 0;
        avg_ = 0;
        max_ = 0;
        min_ = 0;
    }

    AvgStat reset() {
        AvgStat stat = *this;
        clear();
        return stat;
    }

    AvgStat peek() {
        return *this;
    }

    int64_t avg() {
        return avg_;
    }
};

class StopWatch {



    std::map<uint64_t, std::timespec> start_book;
    std::map<uint64_t, std::timespec> end_book;

    

    public:
    StopWatch(){
       // start.reserve(10*1000*1000);
       // end.reserve(10*1000*1000);
    }
    double_t dur_avg(){
        
        uint64_t total_freq = 0;
      
        double_t diff_sum = 0.0;
        Log_info("Pointer comparison %p : %p", end_book.begin(),end_book.end());
        for(auto it = end_book.begin(); it != end_book.end(); ++it){
            Log_info("entering diff loop");
            if(it->second.tv_sec - start_book[it->first].tv_sec   == 0){
            double_t duration = it->second.tv_nsec - start_book[it->first].tv_nsec ;

            diff_sum+=duration;
            total_freq++;
            }
        }
          Log_info("this pointer %p Sample size for latency: %d",this,total_freq);
        return diff_sum/total_freq;

    }
    void start_timer(uint64_t id){
        Log_info("Entry Added with id 0x%8x",id);
        std::timespec ts;
        std::timespec_get(&ts, TIME_UTC);
       start_book.insert(std::make_pair(id, ts));
       
    }
    void end_timer(uint64_t id){

        Log_info("Entry ended with id 0x%8x",id);
         std::timespec ts;
        std::timespec_get(&ts, TIME_UTC);
        end_book.insert(std::make_pair(id,ts));
        
    }
};

} // namespace rrr
