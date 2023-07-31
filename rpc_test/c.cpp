#include <counter.h>
#include <pthread.h>
#include <stdlib.h>

//using namespace ;

char **servers;
unsigned int ns;
unsigned int npg;

CounterProxy **get_proxy() {
    unsigned int i = 0;
    rrr::PollMgr **pm = (rrr::PollMgr **)malloc(sizeof(rrr::PollMgr *) * ns);
    CounterProxy **ret = (CounterProxy **)malloc(sizeof(CounterProxy *) * ns);
    for (; i < ns; i++) {
        pm[i] = new rrr::PollMgr();
        #ifdef DPDK
        rrr::UDPClient *client = new rrr::UDPClient(pm[i]);
        #else
        rrr::TCPClient* client = new rrr::TCPClient(pm[i]);
        #endif
        client->connect(servers[i]);
        ret[i] = new CounterProxy((rrr::Client*)client);
    }
    return ret;
}


void *do_add(void *) {
    CounterProxy **proxy = get_proxy();
    unsigned int start = rand() % ns;
    unsigned int i = 0, j;
    while (1) {
        rrr::FutureGroup fg;
        for (i = 0; i < ns; i++) {
            for (j = 0; j < npg; j++) {
                fg.add(proxy[start++]->async_add());
                start %= ns;
            }
        }
     fg.wait_all();
        
    }
    return NULL;
}

void *do_add_long(void *) {
    CounterProxy **proxy = get_proxy();
    unsigned int start = rand() % ns;
    unsigned int i = 0;
    while (1) {
        rrr::FutureGroup fg;
        for (i = 0; i < npg; i++) {
            fg.add(proxy[start++]->async_add_long(1, 2, 3, 4, 5, std::vector<rrr::i64>(2, 1)));
            start %= ns;
        }
        fg.wait_all();
    }
    return NULL;
}

void *do_add_short(void *) {
    CounterProxy **proxy = get_proxy();
    unsigned int start = rand() % ns;
    unsigned int i = 0;
    unsigned int j = 0;
    while (j<300*1000) {
        rrr::FutureGroup fg;
        for (i = 0; i < npg; i++) {
            fg.add(proxy[start++]->async_add_short((rrr::i64)1));
            start %= ns;
        }
        
        //j++;
       // Log_debug("req num %d",j);
        fg.wait_all();
        
    }
    return NULL;
}
#ifdef RPC_STATISTICS

#endif
int main(int argc, char **argv) {

     char* argv2[] = {"bin/server","-fconfig_files/cpu.yml","-fconfig_files/dpdk.yml","-fconfig_files/host_greenport.yml","-fconfig_files/network_greenport.yml"};
     rrr::Config::create_config(5, argv2);
   

    if (argc < 5)
        return -1;

    unsigned int nt = atoi(argv[1]);

    void *(*func)(void *);
    switch(atoi(argv[2])) {
        case 0:
            func = &do_add;
            break;
        case 1:
            func = &do_add_short;
            break;
        case 2:
            func = &do_add_long;
            break;
        default:
            return -3;
    }

    npg = atoi(argv[3]);
    ns = atoi(argv[4]);

    if ((unsigned int)argc < 5 + ns)
        return -2;

    servers = (char **)malloc(ns * sizeof(char *));

    unsigned int i = 0;
    for (; i < ns; i++)
        servers[i] = argv[i + 5];

    pthread_t *ph = (pthread_t *)malloc(sizeof(pthread_t) * nt);
    for (i = 0; i < nt; i++)
        pthread_create(ph + i, NULL, func, NULL);

    for (i = 0; i < nt; i++)
        pthread_join(ph[i], NULL);

   
    
    return 0;
}
