#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include "AppConfig.hpp"
#include "hello.h"



class HelloServiceImpl: public HelloWorldService{

    public:
    void hellow(const std::string& in, std::string* out){
        std::cout<<" Server Receives "<<in<<std::endl;
        char reply[256];
        sprintf(reply,"World-%d", rand()%1000);
        
        out->append(std::string(reply));
        std::cout<<"Reply: "<<*out<<" Reply Size: "<<out->length()<<std::endl;
    }
    void add_short(const rrr::i64& in, rrr::i32*  out){

        *out = in+1;

    }
};

int main(int argc, char **argv) {
    

    rrr::RPCConfig::create_config(argc, argv); 
    AppConfig::create_config(argc, argv);

    

    AppConfig* appConf = AppConfig::get_config();
    rrr::RPCConfig* conf = rrr::RPCConfig::get_config();
  
    
    #ifdef DPDK
    rrr::DpdkTransport::create_transport(conf);
    #endif
    base::ThreadPool *tp = new base::ThreadPool(0);
    rrr::PollMgr* pollmgr = new rrr::PollMgr(1);

        
    rrr::UDPServer* server = new rrr::UDPServer((rrr::PollMgr*)pollmgr->ref_copy(),tp);
   
    HelloServiceImpl* csi = new HelloServiceImpl();

    server->reg(csi);
    size_t idx = appConf->server_address_.find(":");
   // std::string server_ip = conf->server_address_.substr(0, idx);
    std::string port = appConf->server_address_.substr(idx + 1);

    
    server->start((std::string("0.0.0.0:") + port).c_str());

    rrr::Timer t;
    t.start();
    while(t.elapsed() < appConf->server_duration_){
        ;
        sleep(1);
    }
   
    pollmgr->stop_threads();
     server->stop();
    while(pollmgr->release()){
        ;
    }
    rrr::DpdkTransport::get_transport()->trigger_shutdown();
    rrr::DpdkTransport::get_transport()->shutdown();
    
    return 0;
}
