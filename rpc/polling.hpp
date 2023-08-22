#pragma once

#include <map>
#include <set>

//#include "rrr.hpp"
#include "base/misc.hpp"
#include <bitset>

#include <unordered_map>
#include <unordered_set>

#include "base/all.hpp"

#include "utils.hpp"

using rrr::FrequentJob;

namespace rrr {
class Reporter;
class Pollable: public rrr::RefCounted {
   
protected:
     #ifdef RPC_STATISTICS
        friend class Reporter;
        uint64_t clamps[200*1000] = {0};
        uint64_t counters[6] = {0};
        uint64_t batch_record[10000] = {0};
        uint16_t batch_id=0;
        SpinLock c_locks[6];
        void record_batch(size_t batch_size){
       
            batch_record[batch_id] = batch_size;
            batch_id++;
            batch_id %= 10000;
          
        }
        uint64_t read_and_set_counter(uint8_t id){
            uint64_t ret = counters[id];
            c_locks[id].lock();
            counters[id] = 0;
            c_locks[id].unlock();
            return ret;
        }

        void count(uint8_t counter_id){
            c_locks[counter_id].lock();
            counters[counter_id]++;
            c_locks[counter_id].unlock();
        }
    #endif
    // RefCounted class requires protected destructor
    virtual ~Pollable() {}

public:

    enum {
        READ = 0x1, WRITE = 0x2
    };

    virtual int fd() = 0;
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
    
    class PollThread : RPC_Thread {
        friend class PollMgr;
        #ifdef RPC_STATISTICS
        friend class Reporter;
        #endif
        PollMgr* poll_mgr_;

        // guard mode_ and poll_set_
        rrr::SpinLock l_;
        std::unordered_map<int, int> mode_;
        std::unordered_set<Pollable*> poll_set_;
        int poll_fd_;

        std::set<FrequentJob*> fjobs_;

        std::unordered_set<Pollable*> pending_remove_;
        SpinLock pending_remove_l_;
        bool stop_flag_;

        static void* start_poll_loop(void* arg) {
            PollThread* thiz = (PollThread *) arg;
            thiz->poll_loop();
            pthread_exit(nullptr);
            return nullptr;
        }

        void poll_loop();

        void start(PollMgr* poll_mgr) {
            poll_mgr_ = poll_mgr;
            Pthread_create(p_th_, nullptr, PollMgr::PollThread::start_poll_loop, this);
        }
        public:
         
        PollThread(pthread_t* th, uint16_t tid);
        void add(Pollable*);
        void remove(Pollable*);
        void update_mode(Pollable*, int new_mode);

        void add(FrequentJob*);
        void remove(FrequentJob*);
        ~PollThread() {
            stop_flag_ = true;
            Pthread_join(*p_th_, nullptr);

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
