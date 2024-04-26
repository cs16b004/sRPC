#include <pthread.h>
#include <stdlib.h>
#include "AppConfig.hpp"
#include "hello.h"
//using namespace ;

int main(int argc, char **argv) {
    
    rrr::RPCConfig::create_config(argc, argv);

    AppConfig::create_config(argc, argv);
    rrr::RPCConfig* conf = rrr::RPCConfig::get_config();

    rrr::DpdkTransport::create_transport(conf);

    AppConfig* appConf = AppConfig::get_config();

   

     


    while(!(rrr::DpdkTransport::get_transport()->initialized())){
        ;
    }

      rrr::PollMgr* pollmgr = new rrr::PollMgr(1);  
    rrr::UDPClient *client = new rrr::UDPClient((rrr::PollMgr*)pollmgr->ref_copy());

    client->connect(appConf->server_address_.c_str());

    HelloWorldProxy* rpc_proxy = new HelloWorldProxy(client);
    rrr::Timer t;
    t.start();
    while(t.elapsed() < appConf->client_duration_){
        std::string reply;
        rpc_proxy->hellow("hello, ", &reply);
        std::cout<<"Client sends hello, receives "<<reply<<std::endl;
        sleep(1);

    }
    t.stop();
    pollmgr->stop_threads();
    while(pollmgr->release()){
        ;
    }
    client->close_and_release();
    rrr::DpdkTransport::get_transport()->trigger_shutdown();
    rrr::DpdkTransport::get_transport()->shutdown();

    

    return 0;    
}
