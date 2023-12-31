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
#ifdef DPDK
#include<rte_launch.h>
#include<rte_lcore.h>
#endif
struct benchmark_thread_info;
class BenchmarkProxy:CounterProxy{
    private:
    uint16_t input_size;
    
    std::string in;
     std::string out;
    public:
    BenchmarkProxy(uint16_t in_size, rrr::Client* cl): CounterProxy(cl), input_size(in_size){
        
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
    uint64_t count_=0;
    std::string out_string;
public:
    BenchmarkServiceImpl(uint16_t num_out): out_size(num_out){
        for(int i=0;i<num_out;i++){
            out_string.push_back('a'+ rand()%26);
        }
    }

    void add() {
        count_++;
    }

    void add_long(const rrr::i32& a, const rrr::i32& b, const rrr::i32& c, const rrr::i64& d, const rrr::i64& e, const std::vector<rrr::i64>& input, rrr::i32* out, std::vector<rrr::i64>* output) {
        count_++;
        output->insert(output->end(), {1, 2/*, 3, 4, 5, 6, 7, 8, 9, 10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10*/});
    }

    void add_short(const rrr::i64& a, rrr::i32* out) {
        count_++;
        *out = a+1; 
    }
    void add_bench(const std::string& in, std::string* out ) {
       // rrr::Log::info(__LINE__,__FILE__, "Out size  = %d * 32",out_size);
        count_++;
        out = &out_string;
    }
};
class Benchmarks{
    BenchmarkServiceImpl *csi;
    BenchmarkProxy** service_proxies;
    pthread_t** client_threads;
    rrr::Reporter* rep;
    std::bitset<128> affinity_mask;

    struct benchmark_thread_info** thread_info;
    rrr::Config* conf;

    rrr::PollMgr* pollmgr_;

    public:
    Benchmarks(rrr::Config* config) : conf(config){
        for(int i=conf->core_affinity_mask_[0];i <= conf->core_affinity_mask_[1];i++)
            affinity_mask.set(i);
    
    }
    ~Benchmarks(){
        
    }
    static void* launch_client_thread(void* args);
    void create_server();
    void create_proxies();
    void create_client_threads();
    void set_cpu_affinity();
    void observe_client();
    void observe_server();
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
struct benchmark_thread_info{
    uint16_t tid;
    bool stop =false;
    BenchmarkProxy* my_proxy;
    uint16_t client_batch_size_;
    benchmark_thread_info(uint16_t id, BenchmarkProxy* pr, uint16_t b_size): tid(id), my_proxy(pr), client_batch_size_(b_size)
    {}
};
