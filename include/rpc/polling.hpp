#ifndef POLLING_HPP
#define POLLING_HPP

#include <map>
#include <set>

//#include "rrr.hpp"
#include "base/misc.hpp"
#include <bitset>
#include <ctime>
#include <unordered_map>
#include <unordered_set>

#include "base/all.hpp"

#include "utils.hpp"
#include <atomic>
#ifdef DPDK
#include <rte_launch.h>
#include<rte_lcore.h>
#endif
using rrr::FrequentJob;

namespace rrr {
class Reporter;
class Pollable: public rrr::RefCounted {
   
protected:
    // RefCounted class requires protected destructor
    virtual ~Pollable() {}

public:

    enum {
        READ = 0x1, WRITE = 0x2
    };

    virtual uint64_t fd() = 0;
    virtual int poll_mode() = 0;
    virtual void handle_read() = 0;
    virtual void handle_write() = 0;
    virtual void handle_error() = 0;
};

class PollMgr: public rrr::RefCounted {

    friend class Reporter;
    
 public:
    class PollThread;

    PollThread** poll_threads_;
    const int n_threads_;

protected:

    // RefCounted object uses protected dtor to prevent accidental deletion
    ~PollMgr();

public:

    PollMgr(int n_threads = 1+1);
    PollMgr(const PollMgr&) = delete;
    PollMgr& operator=(const PollMgr&) = delete;

    void add(Pollable*);
    void remove(Pollable*);
    void update_mode(Pollable*, int new_mode);
    
    // Frequent Job
    void add(FrequentJob*);
    void remove(FrequentJob*);
    int set_cpu_affinity(std::bitset<128> &core_mask);
    void stop_threads();
    
    class PollThread : RPC_Thread {
        friend class PollMgr;
        #ifdef RPC_STATISTICS
        friend class Reporter;
        #endif
        PollMgr* poll_mgr_;

        // guard mode_ and poll_set_
        rrr::SpinLock l_;
        std::unordered_map<uint64_t, int> mode_;
        std::unordered_set<Pollable*> poll_set_;
        Pollable* poll_set_arr_[1024];
        int poll_arr_size = 0;
        int poll_fd_;

        std::set<FrequentJob*> fjobs_;

        std::unordered_set<Pollable*> pending_remove_;
        SpinLock pending_remove_l_;
        bool stop_flag_;

        static int start_poll_loop_dpdk(void* arg) {
            PollThread* thiz = (PollThread *) arg;
            thiz->poll_loop();
           
            //pthread_exit(nullptr);
            
            return 0;
        }
        static void* start_poll_loop(void*arg){
            PollThread* thiz = (PollThread *) arg;
            thiz->poll_loop();
           
            pthread_exit(nullptr);
            
            return nullptr;
        }

        void poll_loop();

        void start(PollMgr* poll_mgr) {
            poll_mgr_ = poll_mgr;
            // #ifdef DPDK
            // rte_eal_remote_launch(PollMgr::PollThread::start_poll_loop_dpdk,this, LCORE_ID_ANY );
            // #else
            Pthread_create(p_th_, nullptr, PollMgr::PollThread::start_poll_loop, this);
            //#endif
        }
        public:
         
        PollThread(pthread_t* th, uint16_t tid);
        void add(Pollable*);
        void remove(Pollable*);
        void update_mode(Pollable*, int new_mode);

        void add(FrequentJob*);
        void remove(FrequentJob*);
        ~PollThread() {
            if (!stop_flag_){
            stop_flag_ = true;
           // #ifndef DPDK
                Pthread_join(*p_th_, nullptr);
            }
            //#endif
        // when stopping, release anything registered in pollmgr
            for (auto& it: poll_set_) {
                this->remove(it);
            }
            for (auto& it: pending_remove_) {
                it->release();
            }
        }
        void trigger_fjob();
    };

};

} // namespace rrr
#endif