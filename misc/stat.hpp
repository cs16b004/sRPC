#pragma once

#include <sstream>

#include <iostream>
#include <chrono>
#include <unordered_map>
#include <map>
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



    std::map<uint64_t, std::chrono::high_resolution_clock::time_point> start_book;
    std::map<uint64_t, std::chrono::high_resolution_clock::time_point> end_book;



    public:
    StopWatch(){
       // start.reserve(10*1000*1000);
       // end.reserve(10*1000*1000);
    }
    double_t dur_avg(){
        
        uint64_t total_freq = 0;
      
        double_t diff_sum = 0.0;
        for(auto it = end_book.begin(); it != end_book.end(); ++it){

            std::chrono::duration<double> duration = it->second - start_book[it->first] ;

            diff_sum+=duration.count();
            total_freq++;
        }
          Log_info("this pointer %p Sample size for latency: %d",this,total_freq);
        return diff_sum/total_freq;

    }
    void start_timer(uint64_t id){
        Log_info("Entry Added with id 0x%8x",id);
        start_book[id] = std::chrono::high_resolution_clock::now();
    }
    void end_timer(uint64_t id){
        Log_info("Entry ended with id 0x%8x",id);
        end_book[id] = std::chrono::high_resolution_clock::now();
    }
};

} // namespace rrr
