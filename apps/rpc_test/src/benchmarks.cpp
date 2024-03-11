#include "benchmarks.hpp"

rrr::Counter BenchmarkServiceImpl::at_counter;

void Benchmarks::create_server(){

     csi = new BenchmarkServiceImpl(conf->output_size_);


    pollmgr_ = new rrr::PollMgr(conf->server_poll_threads_);
    pollmgr_->set_cpu_affinity(affinity_mask);
    base::ThreadPool *tp = new base::ThreadPool(0);
    
    #ifdef DPDK
        
        rrr::UDPServer *server = new rrr::UDPServer((rrr::PollMgr*)pollmgr_->ref_copy(),tp);
    #else
        rrr::TCPServer *server = new rrr::TCPServer((rrr::PollMgr*)pollmgr_->ref_copy(), tp);
    #endif
    
    server->reg(csi);

    
    size_t idx = conf->server_address_.find(":");
    if (idx == std::string::npos) {
        rrr::Log::error(__LINE__, __FILE__,"Bad address %s", conf->server_address_.c_str());
        return;
    }
   // std::string server_ip = conf->server_address_.substr(0, idx);
    std::string port = conf->server_address_.substr(idx + 1);

    
    
   
    
    server->start((std::string("0.0.0.0:") + port).c_str());

    
    
    observe_server();
   
      
      
        #ifdef DPDK
        
        ((rrr::UDPServer*)server)->stop();
        #else
            ((rrr::TCPServer*)server)->stop();
        #endif

        pollmgr_->release();
    
     // pollmgr_->stop_threads();
      
    

    
   
  
    
    delete server;
    delete csi;
    //delete rep;

}
void Benchmarks::create_proxies(){
     
      
        pollmgr_ = new rrr::PollMgr(conf->client_poll_threads_);


        // pollmgr_->set_cpu_affinity(affinity_mask);
  
        service_proxies = new BenchmarkProxy*[conf->client_connections_];

        uint16_t input_size;
        input_size = conf->input_size_;
     
        for (int i=0; i < conf->client_connections_; i++) {
            #ifdef DPDK
                rrr::UDPClient *client = new rrr::UDPClient((rrr::PollMgr*)pollmgr_->ref_copy());
             
            #else
            rrr::TCPClient* client = new rrr::TCPClient((rrr::PollMgr*)pollmgr_->ref_copy());
            #endif
            client->connect(conf->server_address_.c_str());
            service_proxies[i] = new BenchmarkProxy(input_size,client);
        }
       
}
void* Benchmarks::launch_client_thread(void *arg){
    rrr::RPCConfig* conf= rrr::RPCConfig::get_config();
     while(1){
      //  Log_info("thread cpu %d",sched_getcpu());
        if(sched_getcpu() >= (conf->cpu_info_.numa)*(conf->cpu_info_.core_per_numa)
                || sched_getcpu() <= (conf->cpu_info_.numa +1)*(conf->cpu_info_.core_per_numa) ){
            break;
        }else{
            Log_warn("Waiting for scheduled on right node");
            sleep(1);
        }
    }
    benchmark_ctx* ct = (benchmark_ctx*)arg;
    rrr::Log::info(__LINE__, __FILE__,"Benchmark thread: %d launched", ct->tid);
    BenchmarkProxy* pr= ct->my_proxy;
    while(!ct->stop){
         rrr::FutureGroup fg;
        for (int i = 0; i < ct->client_batch_size_; i++) {
            (pr->add_bench_async());
        }
        //fg.wait_all();
        pr->reply_count+=ct->client_batch_size_;
        #ifdef DPDK
        #ifdef LOG_LEVEL_AS_DEBUG
        //break;
        #endif
        #endif
    }
    rrr::Log::info(__LINE__, __FILE__,"Benchmark thread: %d stopped", ct->tid);
      
    int *a  = new int;
    return  (void*)a;
}
void Benchmarks::create_client_threads(){
    thread_info = new benchmark_ctx* [conf->num_client_threads_];
    client_threads = new pthread_t*[conf->num_client_threads_];
    
    for(int j=0;j<conf->num_client_threads_;j++){
        thread_info[j] = new benchmark_ctx(j,service_proxies[j%conf->client_connections_],conf->client_batch_size_);
    }
    int ret;
    for(int j=0;j<conf->num_client_threads_;j++){
        client_threads[j] = (pthread_t*) malloc(sizeof(pthread_t));
        rrr::Log::info(__LINE__,__FILE__,"New client thread created %d, with client %d",j, j%conf->client_connections_);

    }
   // set_cpu_affinity();
    
    for(int j=0;j<conf->num_client_threads_;j++){
       pthread_create(client_threads[j], nullptr, Benchmarks::launch_client_thread, thread_info[j]);
    }
    set_cpu_affinity();  
      
    
}
void Benchmarks::observe_server(){
    int i=0;
    while(i < AppConfig::get_config()->server_duration_){
        sleep(1);
        i++;
    }
    return;
}
void Benchmarks::observe_client(){
    int i=0;
        rrr::Timer t;
        t.start();
        uint64_t last=0;
        while(t.elapsed() < conf->client_duration_){
            sleep(1);
            uint64_t num =0;
            for(int i=0; i< conf->num_client_threads_; i++)
                num+= service_proxies[i]->reply_count;
            std::cout<<"RPC/sec "<<num-last<<std::endl;
            last = num;

        }
        std::cout<<"Observed for : "<<t.elapsed()<<" seconds"<<std::endl;
        t.stop();
}
void Benchmarks::set_cpu_affinity(){

   for(int i=0; i< conf->num_client_threads_; i++){
    rrr::Log::debug(__LINE__, __FILE__, "Setting CPU affinity for thread %d",i);
    cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        int core_id;
        for(core_id=0; core_id< affinity_mask.size(); core_id++){
            if (affinity_mask.test(core_id)){
               // rrr::Log::debug(__LINE__, __FILE__,"Setting cpu affinity for thread: %d",i);
                CPU_SET(core_id, &cpuset);
            }
        }

        int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
        assert((core_id <= num_cores));
        assert(client_threads[i] != nullptr);
        rrr::Log::debug(__LINE__,__FILE__, "Client thread %d, %p",i, client_threads[i]);
        int err = pthread_setaffinity_np(*(client_threads[i]), sizeof(cpu_set_t), &cpuset);
        if (err < 0) {
           // rrr::Log::debug(__LINE__, __FILE__,"Couldn't set affinity of thread %d to core %d",i, core_id);
            return ;
        }
   }

}
void Benchmarks::stop_client(){
        
       for(int i=0; i< conf->num_client_threads_; i++){
         thread_info[i]->stop = true;
         
       }
    //    for(int i=0; i< conf->num_client_threads_; i++){
    //     pthread_join(*client_threads[i],nullptr);
    //    }
       for(int i=0; i< conf->client_connections_; i++){
            service_proxies[i]->close();
       }

       pollmgr_->release();
       
}
