#include <counter.h>
#include <pthread.h>
#include <stdlib.h>

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <vector>
#include<cstdlib>
#include <sstream>
#include<iomanip>
#include<iostream>

#include<fstream>
#include<filesystem>

#include<rte_launch.h>
#include<rte_lcore.h>
#include<rte_cycles.h>
#include "AppConfig.hpp"
struct benchmark_ctx;
class BenchmarkProxy:CounterProxy{
    private:
    uint16_t input_size;

    
    std::string in;
     std::string out;
    public:
    uint64_t reply_count=0;
    rrr::Client* clnt = nullptr;
    BenchmarkProxy(uint16_t in_size, rrr::Client* cl): CounterProxy(cl), input_size(in_size){
        clnt = cl;
        for(int i=0;i<input_size;i++){
           in.push_back('a'+ rand()%26);
        }
        //rrr::Log::debug(__LINE__,__FILE__, "Input string %s",in.c_str());

    }
    void add_bench(){
       // add_bench(in,&out);
    }
    rrr::Future* add_bench_async(){
       // rrr::Log::info(__LINE__,__FILE__, "Input size  = %d bytes",input_size);
        return async_add_bench(in);
    }
    void close(){
        __cl__->close_and_release();
    }

};
class BenchmarkServiceImpl : public CounterService {
private:
    unsigned int time_;
    uint16_t out_size=1;
    rrr::PollMgr* pollmgr_;
    rrr::Timer server_timer;
    std::string data_file_name;
    
    std::string out_string;
    std::ofstream csvFile;
public:
    uint64_t count_=0;
     static rrr::Counter at_counter;
    BenchmarkServiceImpl(uint16_t num_out, std::string data_file): out_size(num_out), data_file_name(data_file){
        for(int i=0;i<num_out;i++){
            out_string.push_back('a'+ rand()%26);
        }
        csvFile.open(data_file_name.c_str());
        csvFile << "Received,rate\n";
        at_counter.next();
        
    }

    void add() {
        //count_++;
    }

    void add_long(const rrr::i32& a, const rrr::i32& b, const rrr::i32& c, const rrr::i64& d, const rrr::i64& e, const std::vector<rrr::i64>& input, rrr::i32* out, std::vector<rrr::i64>* output) {
        //count_++;
        output->insert(output->end(), {1, 2/*, 3, 4, 5, 6, 7, 8, 9, 10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10*/});
    }

    void add_short(const rrr::i64& a, rrr::i32* out) {
        //count_++;
        *out = a+1; 
    }
    void add_bench(const std::string& in, std::string* out ) {
       // rrr::Log::info(__LINE__,__FILE__, "Out size  = %d * 32",out_size);
        //count_++;
        
        uint64_t val =  at_counter.next();
        out->append(out_string.c_str());
        if(val == 1000*1000){
            server_timer.start();
        }
        else if((val%(1000*1000)) == 0){
            csvFile << val <<"," << val/server_timer.elapsed() <<std::endl;
        }
        
    }
    void closeFile(){
        csvFile.close();
    }
};
class Benchmarks{
    
    BenchmarkProxy** service_proxies;
    pthread_t** client_threads;
    std::thread stat_thread;
     #ifdef DPDK
        
        rrr::UDPServer *server = nullptr;
    #else
        rrr::TCPServer *server = nullptr;
    #endif
    bool stop=false;
    int num_client=0;
    std::bitset<128> affinity_mask;

    struct benchmark_ctx** thread_info;
    AppConfig* conf;

    rrr::PollMgr* pollmgr_;

    public:
    BenchmarkServiceImpl *csi;
    Benchmarks(AppConfig* config) : conf(config){
        rrr::Log::info("Core Affinity from %d - %d", conf->core_affinity_mask_[0], conf->core_affinity_mask_[1]);
        for(int i=conf->core_affinity_mask_[0];i <= conf->core_affinity_mask_[1];i++)
            affinity_mask.set(i);
    
    }
    ~Benchmarks(){
        
    }
    static void* launch_client_thread(void* args);
    void create_server();
    void stop_server();
    void stop_server_loop(){
        #ifdef DPDK
        
        ((rrr::UDPServer*)server)->stop();
        #else
            ((rrr::TCPServer*)server)->stop();
        #endif
    }
    void observe_server();
    void create_proxies();
    void create_client_threads();
    void set_cpu_affinity();
    void set_cpu_affinity(pthread_t* th);
    void observe_client();
    void stop_client();
    double diff_timespec(const struct timespec &time1, const struct timespec &time0) {
        if (time1.tv_sec - time0.tv_sec ==0)
            return (time1.tv_nsec - time0.tv_nsec);
        else{
            //Log_info("Difference in seconds !!!! %d",time1.tv_sec - time0.tv_sec);
            return 1000*1000*1000.0 + time1.tv_nsec - time0.tv_nsec ;
        }
        }
 

    
};
struct benchmark_ctx{
    uint16_t tid;
    bool stop =false;
    BenchmarkProxy* my_proxy;
    uint16_t client_batch_size_;
    benchmark_ctx(uint16_t id, BenchmarkProxy* pr, uint16_t b_size): tid(id), my_proxy(pr), client_batch_size_(b_size)
    {}
};
