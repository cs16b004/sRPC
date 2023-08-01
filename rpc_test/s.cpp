#include <counter.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

class CounterServiceImpl : public CounterService {
private:
    unsigned int time_;
    uint64_t last_count_, count_;
    struct timespec tv_;
    static void alarm_handler(int sig) {
        struct timespec tv_buf;
        clock_gettime(CLOCK_REALTIME, &tv_buf);
        double time = (double)(tv_buf.tv_sec - csi_s->tv_.tv_sec) + ((double)(tv_buf.tv_nsec - csi_s->tv_.tv_nsec)) / 1000000000.0;
        fprintf(stdout, "------------------------------------------------------------\n\n\n\n\n");
        fprintf(stdout, "time: %lf, count: %lu, rpc per sec: %lf\n", time, csi_s->count_ - csi_s->last_count_, (csi_s->count_ - csi_s->last_count_) / time);
        fprintf(stdout, "n\n\n\n\n\n\n------------------------------------------------------------\n\n\n\n\n");
        csi_s->last_count_ = csi_s->count_;
        csi_s->tv_.tv_sec = tv_buf.tv_sec;
        csi_s->tv_.tv_nsec = tv_buf.tv_nsec;
        alarm(csi_s->time_);
    }

    static CounterServiceImpl *csi_s;
public:
    CounterServiceImpl(unsigned int time = 0) : time_(time), last_count_(0), count_(0) {
        clock_gettime(CLOCK_REALTIME, &tv_);

        struct sigaction sact;
        sigemptyset(&sact.sa_mask);
        sact.sa_flags = 0;
        sact.sa_handler = CounterServiceImpl::alarm_handler;
        sigaction(SIGALRM, &sact, NULL);
        alarm(time);

        csi_s = this;
    }

    ~CounterServiceImpl() {
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
    }
};

CounterServiceImpl *CounterServiceImpl::csi_s = NULL;


int main(int argc, char **argv) {
    if (argc != 3)
        return -1;
    unsigned int time = atoi(argv[2]);
    CounterServiceImpl *csi = new CounterServiceImpl(time);
    rrr::PollMgr *pm = new rrr::PollMgr(1);
    base::ThreadPool *tp = new base::ThreadPool(1);
    #ifdef DPDK
    rrr::UDPServer *server = new rrr::UDPServer(pm,tp);
    #else
    rrr::TCPServer *server = new rrr::TCPServer(pm, tp);
    #endif
    server->reg(csi);



    char* argv2[] = {"bin/server","-fconfig_files/cpu.yml","-fconfig_files/dpdk.yml","-fconfig_files/host_catskill.yml","-fconfig_files/network_catskill.yml"};
     rrr::Config::create_config(5, argv2);
      

   
    
   
    server->start((std::string("0.0.0.0:") + argv[1]).c_str());
    printf("djajskjhdfjksdfh");
      int i=0;
    while (i < 100){
        usleep(100000);
        i++;
    }
    server->stop();
    //pm->release();
    //tp->release();
  
    
    delete server;
    delete csi;
    return 0;
}
