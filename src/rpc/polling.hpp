#pragma once

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
    /**
     * @brief Pollable Abstract class, this class exposes several methods like handle_read(), handle_write() and poll_mode(),
     * which are called from PollThreads to read requests from network and write requests(client) replies(server) to the network. 
     * 
     */
class Pollable: public rrr::RefCounted {
   
protected:
    // RefCounted class requires protected destructor
    virtual ~Pollable() {}

public:

    enum {
        READ = 0x1, WRITE = 0x2
    };
    /**
     * @brief return the socket fd for this pollable object, if DPDK is used then a 64bit conneciton id is used, this is used to access a TransportConnection object in 
     * transport layer.
     * 
     * @return uint64_t 
     */
    virtual uint64_t fd() = 0;
    virtual int poll_mode() = 0;
    /**
     * @brief Read bytes from the network, deserialze into Requests objects
     * and run RPCs.
     * 
     */
    virtual void handle_read() = 0;
    /**
     * @brief Write reply/requests bytes to the network.
     * 
     */
    virtual void handle_write() = 0;
    /**
     * @brief Handle any error during recv/write calls.
     * 
     */
    virtual void handle_error() = 0;
};

/**
 * @brief Poll Manager keeps several threads and is used by both client and server to poll connections and listen for requests /  replies.
 * Each Manager keeps track of several threads and ServerConnection or ClientConnection (Pollable instances) are added to threads in a random / round robin fashion.
 * 
 * 
 */
class PollMgr: public rrr::RefCounted {

    
 public:
    class PollThread;
    /**
     * @brief Poll thread array
     * 
     */
    PollThread** poll_threads_;
    /**
     * @brief Number of threads to manage.
     * 
     */
    const int n_threads_;


protected:
    #ifdef DPDK
    /**
     * @brief Due to multiple transport layer adding client/server connections a Counter object is used to avoid locks. Locks are costly.
     * 
     */
    Counter poll_counter;
    std::unordered_map<Pollable*, int> poll_map;
    #endif
    // RefCounted object uses protected dtor to prevent accidental deletion
    ~PollMgr();

public:
    /**
     * @brief Construct a new Poll Mgr object
     * 
     * @param n_threads number of polling threads to add, usually a pollable instance is handled by only a single thread throught out the RPC lidfecycle.
     * however, to handle multiple connections several threads should b used for parallelism.
     */
    PollMgr(int n_threads = 1);
    PollMgr(const PollMgr&) = delete;
    PollMgr& operator=(const PollMgr&) = delete;

    /**
     * @brief Add a pollable object a thread.
     * @param poll Pollable object.
     */
    void add(Pollable* poll);
    /**
     * @brief Remove a pollable object.
     * 
     * @param poll 
     */
    void remove(Pollable*poll);
    void update_mode(Pollable*, int new_mode);
    
    // Frequent Job
    /**
     * @brief Add a frequent job: add afunction which should be called frequently by poll threads other than handle_read, handle_write etc.
     * 
     */
    void add(FrequentJob*);
    void remove(FrequentJob*);
    /**
     * @brief Set the cpu affinity for all the threads managed by this Poll Manager.
     * 
     * @param core_mask cpus on which the poll threads can be scheduled as a bitset, ith bit corresponds to ith core.
     * @return 0
     */
    int set_cpu_affinity(std::bitset<128> &core_mask);
    void stop_threads();
    /**
     * @brief Context for  threads managed by PollManager.
     * 
     */
    class PollThread : RPC_Thread {
        friend class PollMgr;
        
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
        /**
         * @brief Launch the polling loop for dpdk based pollable objects (UDPConnection / UDPClient)
         * 
         * @param arg Thread context passed to the loop.
         * @return void* 
         */
        static void* start_poll_loop_dpdk(void* arg) {
            PollThread* thiz = (PollThread *) arg;
            thiz->poll_loop();
           
            //pthread_exit(nullptr);
            
            return arg;
        }
        /**
         * @brief Launch the polling loop for dpdk based pollable objects (TCPConnection / TCPClient)
         * 
         * @param arg Thread context passed to the loop.
         * @return void* 
         */
        static void* start_poll_loop(void*arg){
            PollThread* thiz = (PollThread *) arg;
            thiz->poll_loop();
           
            pthread_exit(nullptr);
            
            return nullptr;
        }

        /**
         * @brief The actual Polling loop, the thread polls on pollable objects and perform actions like runnning RPCs, reading and writing to network.
         * 
         * 
         */
        void poll_loop();
        /**
         * @brief Spawn the thread.
         * 
         * @param poll_mgr 
         */
        void start(PollMgr* poll_mgr) {
            poll_mgr_ = poll_mgr;
             #ifdef DPDK
             Pthread_create(p_th_, nullptr, PollMgr::PollThread::start_poll_loop_dpdk, this);
             #else
            Pthread_create(p_th_, nullptr, PollMgr::PollThread::start_poll_loop, this);
            #endif
        }
        public:
         
        PollThread(pthread_t* th, uint16_t tid);
        /**
         * @brief Add and poll this pollable object.
         * 
         */
        void add(Pollable*);
        /**
         * @brief Stop polling this object.
         * 
         */
        void remove(Pollable*);
        /**
         * @brief Update polling mode for a pollable.
         * 
         * @param new_mode 
         */
        void update_mode(Pollable*, int new_mode);
        /**
         * @brief Add a frequent job to be run between polling (when there is no polling kind of work)
         * 
         */
        void add(FrequentJob*);
        /**
         * @brief Stop running the job.
         * 
         */
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
        /**
         * @brief Trigger all the frequent jobs.
         * 
         */
        void trigger_fjob();
    };

};

} // namespace rrr
