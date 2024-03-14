#include "benchmarks.hpp"

rrr::Counter BenchmarkServiceImpl::at_counter;

void Benchmarks::create_server(){

     csi = new BenchmarkServiceImpl(conf->output_size_);


    pollmgr_ = new rrr::PollMgr(conf->server_poll_threads_);
    pollmgr_->set_cpu_affinity(affinity_mask);
    base::ThreadPool *tp = new base::ThreadPool(0);
    
    #ifdef DPDK
        
         server = new rrr::UDPServer((rrr::PollMgr*)pollmgr_->ref_copy(),tp);
    #else
        server = new rrr::TCPServer((rrr::PollMgr*)pollmgr_->ref_copy(), tp);
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

        
}
void Benchmarks::stop_server_loop(){
    #ifdef DPDK
        
        ((rrr::UDPServer*)server)->stop_loop();
       
    #endif
    pollmgr_->stop_threads();
    pollmgr_->release();
}
void Benchmarks::stop_server(){
     #ifdef DPDK
        
        ((rrr::UDPServer*)server)->stop();
        #else
            ((rrr::TCPServer*)server)->stop();
        #endif

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
    rrr::Future* f;
    while(!ct->stop){
         rrr::FutureGroup fg;
        for (int i = 0; i < ct->client_batch_size_; i++) {
           f =  (pr->add_bench_async());
           sleep(1);
        }
        //fg.wait_all();
         
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
 
    rrr::Timer t;
    uint64_t last = 0;
    t.start();
    double s = 0;
    while(t.elapsed() < AppConfig::get_config()->server_duration_){
        sleep(1);

        
    }
    std::cout<<"Observed Server for : "<<t.elapsed()<<" seconds"<<std::endl;
        uint64_t num = csi->at_counter.next();
            
        std::cout<<(num - last) / (t.elapsed() - s)<<std::endl;
        
        t.stop();
    return;
}
void Benchmarks::observe_client(){
    int i=0;
        rrr::Timer t;
        t.start();
        
        while(t.elapsed() < conf->client_duration_){
            sleep(1);
            
        }
        
        std::cout<<"Observed Client for : "<<t.elapsed()<<" seconds"<<std::endl;

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
       for(int i=0; i< conf->num_client_threads_; i++){
        pthread_join(*client_threads[i],nullptr);
       }
        #ifdef DPDK
         rrr::DpdkTransport::get_transport()->trigger_shutdown();
         rrr::DpdkTransport::get_transport()->shutdown();
    #endif
        pollmgr_->stop_threads();
       for(int i=0; i< conf->client_connections_; i++){
            service_proxies[i]->close();
       }
       
       pollmgr_->release();
       
}
