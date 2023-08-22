#include "benchmarks.hpp"

void Benchmarks::create_server(){

     csi = new BenchmarkServiceImpl(conf->input_size_);


    pollmgr_ = new rrr::PollMgr(conf->server_poll_threads_);
    pollmgr_->set_cpu_affinity(affinity_mask);
    base::ThreadPool *tp = new base::ThreadPool(0);
    
    #ifdef DPDK
        rrr::DpdkTransport::create_transport(conf);
        rrr::UDPServer *server = new rrr::UDPServer(pollmgr_,tp);
    #else
        rrr::TCPServer *server = new rrr::TCPServer(pollmgr_, tp);
    #endif
    
    server->reg(csi);

    
    size_t idx = conf->server_address_.find(":");
    if (idx == std::string::npos) {
        rrr::Log::error("Badd address %s", conf->server_address_.c_str());
        return;
    }
   // std::string server_ip = conf->server_address_.substr(0, idx);
    std::string port = conf->server_address_.substr(idx + 1);

    
    
    rrr::Reporter rep(5000,pollmgr_);
    
    server->start((std::string("0.0.0.0:") + port).c_str());

    rep.launch();
    
    observe_server();

    server->stop();
    rep.trigger_shutdown();

    #ifdef DPDK
        rrr::DpdkTransport::get_transport()->trigger_shutdown();
        rrr::DpdkTransport::get_transport()->shutdown();
    #endif
    pollmgr_->release();
    tp->release();
  
    
    delete server;
    delete csi;
    //delete rep;

}
void Benchmarks::create_proxies(){
        pollmgr_ = new rrr::PollMgr(conf->client_poll_threads_);

        pollmgr_->set_cpu_affinity(affinity_mask);

        service_proxies = new BenchmarkProxy*[conf->client_connections_];

        uint16_t input_size;
        input_size = conf->input_size_/sizeof(rrr::i64);
        for (int i=0; i < conf->client_connections_; i++) {
            #ifdef DPDK
                rrr::UDPClient *client = new rrr::UDPClient(pollmgr_);
             
            #else
            rrr::TCPClient* client = new rrr::TCPClient(pollmgr_);
            #endif
            client->connect(conf->server_address_.c_str());
            service_proxies[i] = new BenchmarkProxy(input_size,client);
        }
}
void* Benchmarks::launch_client_thread(void *arg){
    benchmark_thread_info* ct = (benchmark_thread_info*)arg;
    rrr::Log::info("Benchmark thread: %d launched", ct->tid);
    while(!ct->stop){
         rrr::FutureGroup fg;
        for (int i = 0; i < ct->client_batch_size_; i++) {
            fg.add(ct->my_proxy->add_bench_async());
        }
        fg.wait_all();
    }
    rrr::Log::info("Benchmark thread: %d stopped", ct->tid);
    int *a  = new int;
    return (void*) a;
}
void Benchmarks::create_client_threads(){
    thread_info = new benchmark_thread_info* [conf->num_client_threads_];
    client_threads = new pthread_t*[conf->num_client_threads_];
    
    for(int j=0;j<conf->num_client_threads_;j++){
        thread_info[j] = new benchmark_thread_info(j,service_proxies[j%conf->client_connections_],conf->client_batch_size_);
    }
    int ret;
    for(int j=0;j<conf->num_client_threads_;j++){
        client_threads[j] = new pthread_t;

    }
    set_cpu_affinity();

    for(int j=0;j<conf->num_client_threads_;j++){
        pthread_create(client_threads[j], nullptr, Benchmarks::launch_client_thread, thread_info[j]) == 0;
    }

    
    
}
void Benchmarks::observe_client(){
    int i=0;
    while (i < conf->client_duration_*1000){
        usleep(1000);
        i++;
    }
}
void Benchmarks::observe_server(){
    int i=0;
 while (i < conf->server_duration_*1000){
        usleep(1000);
        i++;
    }
}
void Benchmarks::set_cpu_affinity(){

   for(int i=0; i< conf->num_client_threads_; i++){
    cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        int core_id;
        for(core_id=0; core_id< affinity_mask.size(); core_id++){
            if (affinity_mask.test(core_id)){
                rrr::Log::debug("Setting cpu affinity for thread: %d",i);
                CPU_SET(core_id, &cpuset);
            }
        }

        int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
        assert((core_id <= num_cores));

        int err = pthread_setaffinity_np(*(client_threads[i]), sizeof(cpu_set_t), &cpuset);
        if (err < 0) {
            //rrr::Log::debug("Couldn't set affinity of thread %s-%d to core %d",stringify(type_).c_str(),thread_id_, core_id);
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
       for(int i=0; i< conf->client_connections_; i++){
            service_proxies[i]->close();
       }
       pollmgr_->release();
       
}