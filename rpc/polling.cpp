#ifdef __APPLE__
#define USE_KQUEUE
#endif

#ifdef USE_KQUEUE
#include <sys/event.h>
#else
#include <sys/epoll.h>
#endif


#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <iostream>
#include <fstream>

#include "polling.hpp"
#ifdef PROFILE
#include<gperftools/profiler.h>
#endif
using namespace std;

namespace rrr {


#ifdef RPC_STATISTICS
void Pollable::record_batch(size_t batch_size){
       
            batch_record[batch_id] = batch_size;
            batch_id++;
            batch_id %= 10000;
          
        }
uint64_t Pollable::read_and_set_counter(uint8_t id){
           
            return counters[id];
}

void Pollable::count(uint8_t counter_id){
           
            counters[counter_id]++;
            
        }
void Pollable::put_start_ts(uint64_t xid){
    
    if(start_book.size() > 1000)
        return ;
    ts_lock.lock();
        
        struct timespec ts;
        timespec_get(&ts, TIME_UTC);
        start_book[xid] = ts;
        start_book_counter++;
    
    ts_lock.unlock();
    

}
void Pollable::put_end_ts(uint64_t xid){
        if(end_book.size() > 1000)
            return;
        ts_lock.lock();
        struct timespec ts;
        timespec_get(&ts, TIME_UTC);
        end_book[xid] = ts;
        end_book_counter++;
        ts_lock.unlock();
    
}
#endif

PollMgr::PollThread::PollThread(pthread_t* th, uint16_t tid): RPC_Thread(th, tid, thread_type::POLL_THREAD), poll_mgr_(nullptr), stop_flag_(false) {
#ifdef USE_KQUEUE
        poll_fd_ = kqueue();
#else
        poll_fd_ = epoll_create(10);    // arg ignored, any value > 0 will do
#endif
        verify(poll_fd_ != -1);
}
PollMgr::PollMgr(int n_threads /* =... */)
    : n_threads_(n_threads), poll_threads_() {
    verify(n_threads_ > 0);
    poll_threads_ = new PollThread*[n_threads_];
    for (int i = 0; i < n_threads_; i++) {
        poll_threads_[i] = new PollThread(new pthread_t, i);
        poll_threads_[i]->start(this);
    }
}


void PollMgr::stop_threads(){
    for(int i=0;i<n_threads_;i++){
        poll_threads_[i]->stop_flag_ = true;
        
    }
}
PollMgr::~PollMgr() {
    
    stop_threads();

    delete[] poll_threads_;
    //Log_debug("rrr::PollMgr: destroyed");

}

void PollMgr::PollThread::poll_loop() {
    Log_info("Poll Thread %d started", thread_id_);
    #ifdef DPDK
    #ifdef PROFILE
    std::string sp;
    std::ifstream("/proc/self/comm") >> sp;
    sp+=".prof";
    ProfilerStart(sp.c_str());
    #endif
    while(!stop_flag_){
        for(auto pollable:poll_set_){
            pollable->handle_read();
        }
    }
    #ifdef PROFILE
    ProfilerStop();
    Log_info("PROFILER STOPPED !!");
    #endif
    #endif
    
    while (!stop_flag_) {
        const int max_nev = 100;

#ifdef USE_KQUEUE

        struct kevent evlist[max_nev];
        struct timespec timeout;
        timeout.tv_sec = 0;
        timeout.tv_nsec = 50 * 1000 * 1000; // 0.05 sec

        int nev = kevent(poll_fd_, nullptr, 0, evlist, max_nev, &timeout);

        for (int i = 0; i < nev; i++) {
            Pollable* poll = (Pollable *) evlist[i].udata;
            verify(poll != nullptr);

            if (evlist[i].filter == EVFILT_READ) {
                poll->handle_read();
            }
            if (evlist[i].filter == EVFILT_WRITE) {
                poll->handle_write();
            }

            // handle error after handle IO, so that we can at least process something
            if (evlist[i].flags & EV_EOF) {
                poll->handle_error();
            }
        }

#else

        struct epoll_event evlist[max_nev];
        int timeout = 1; // milli, 0.001 sec

        int nev = epoll_wait(poll_fd_, evlist, max_nev, timeout);

        if (stop_flag_) {
            break;
        }
	
	trigger_fjob();
	
        for (int i = 0; i < nev; i++) {
            Pollable* poll = (Pollable *) evlist[i].data.ptr;
            verify(poll != nullptr);

            if (evlist[i].events & EPOLLIN) {
                poll->handle_read();
            }
            if (evlist[i].events & EPOLLOUT) {
                poll->handle_write();
            }

            // handle error after handle IO, so that we can at least process something
            if (evlist[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                poll->handle_error();
            }
        }
	
	trigger_fjob();

#endif

        // after each poll loop, remove uninterested pollables
        pending_remove_l_.lock();
        list<Pollable*> remove_poll(pending_remove_.begin(), pending_remove_.end());
        pending_remove_.clear();
        pending_remove_l_.unlock();

        for (auto& poll: remove_poll) {
            uint64_t fd = poll->fd();

            l_.lock();
            if (mode_.find(fd) == mode_.end()) {
                // NOTE: only remove the fd when it is not immediately added again
                // if the same fd is used again, mode_ will contains its info
#ifdef USE_KQUEUE

                struct kevent ev;

                bzero(&ev, sizeof(ev));
                ev.ident = fd;
                ev.flags = EV_DELETE;
                ev.filter = EVFILT_READ;
                kevent(poll_fd_, &ev, 1, nullptr, 0, nullptr);

                bzero(&ev, sizeof(ev));
                ev.ident = fd;
                ev.flags = EV_DELETE;
                ev.filter = EVFILT_WRITE;
                kevent(poll_fd_, &ev, 1, nullptr, 0, nullptr);

#else
                struct epoll_event ev;
                memset(&ev, 0, sizeof(ev));

                epoll_ctl(poll_fd_, EPOLL_CTL_DEL, fd, &ev);
#endif
            }
            l_.unlock();

            poll->release();
        }
    }

    Log_info("Polling Thread Stopped");
    close(poll_fd_);
    
}

void PollMgr::PollThread::add(FrequentJob* fjob) {
    fjobs_.insert(fjob);
}

void PollMgr::PollThread::remove(FrequentJob* fjob) {
    fjobs_.erase(fjob);
}

void PollMgr::PollThread::add(Pollable* poll) {
    poll->ref_copy();   // increase ref count

    int poll_mode = poll->poll_mode();
    uint64_t fd = poll->fd();

    l_.lock();

    // verify not exists
    verify(poll_set_.find(poll) == poll_set_.end());
    verify(mode_.find(fd) == mode_.end());
    
    // register pollable
    poll_set_.insert(poll);
    mode_[fd] = poll_mode;

    l_.unlock();
    #ifdef DPDK
        return;
    #endif
#ifdef USE_KQUEUE

    struct kevent ev;
    if (poll_mode & Pollable::READ) {
        bzero(&ev, sizeof(ev));
        ev.ident = fd;
        ev.flags = EV_ADD;
        ev.filter = EVFILT_READ;
        ev.udata = poll;
        verify(kevent(poll_fd_, &ev, 1, nullptr, 0, nullptr) == 0);
    }
    if (poll_mode & Pollable::WRITE) {
        bzero(&ev, sizeof(ev));
        ev.ident = fd;
        ev.flags = EV_ADD;
        ev.filter = EVFILT_WRITE;
        ev.udata = poll;
        verify(kevent(poll_fd_, &ev, 1, nullptr, 0, nullptr) == 0);
    }

#else

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));

    ev.data.ptr = poll;
    ev.events = EPOLLET | EPOLLIN | EPOLLRDHUP; // EPOLLERR and EPOLLHUP are included by default

    if (poll_mode & Pollable::WRITE) {
        ev.events |= EPOLLOUT;
    }
    verify(epoll_ctl(poll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0);

#endif
}

void PollMgr::PollThread::remove(Pollable* poll) {
    bool found = false;
    l_.lock();
    unordered_set<Pollable*>::iterator it = poll_set_.find(poll);
    if (it != poll_set_.end()) {
        found = true;
        assert(mode_.find(poll->fd()) != mode_.end());
        poll_set_.erase(poll);
        mode_.erase(poll->fd());
    } else {
        assert(mode_.find(poll->fd()) == mode_.end());
    }
    l_.unlock();

    if (found) {
        pending_remove_l_.lock();
        pending_remove_.insert(poll);
        pending_remove_l_.unlock();
    }
}

void PollMgr::PollThread::update_mode(Pollable* poll, int new_mode) {
    uint64_t fd = poll->fd();

    l_.lock();

    if (poll_set_.find(poll) == poll_set_.end()) {
        l_.unlock();
        return;
    }

    unordered_map<uint64_t, int>::iterator it = mode_.find(fd);
    verify(it != mode_.end());
    int old_mode = it->second;
    it->second = new_mode;

    if (new_mode != old_mode) {

#ifdef USE_KQUEUE

        struct kevent ev;
        if ((new_mode & Pollable::READ) && !(old_mode & Pollable::READ)) {
            // add READ
            bzero(&ev, sizeof(ev));
            ev.ident = fd;
            ev.udata = poll;
            ev.flags = EV_ADD;
            ev.filter = EVFILT_READ;
            verify(kevent(poll_fd_, &ev, 1, nullptr, 0, nullptr) == 0);
        }
        if (!(new_mode & Pollable::READ) && (old_mode & Pollable::READ)) {
            // del READ
            bzero(&ev, sizeof(ev));
            ev.ident = fd;
            ev.udata = poll;
            ev.flags = EV_DELETE;
            ev.filter = EVFILT_READ;
            verify(kevent(poll_fd_, &ev, 1, nullptr, 0, nullptr) == 0);
        }
        if ((new_mode & Pollable::WRITE) && !(old_mode & Pollable::WRITE)) {
            // add WRITE
            bzero(&ev, sizeof(ev));
            ev.ident = fd;
            ev.udata = poll;
            ev.flags = EV_ADD;
            ev.filter = EVFILT_WRITE;
            verify(kevent(poll_fd_, &ev, 1, nullptr, 0, nullptr) == 0);
        }
        if (!(new_mode & Pollable::WRITE) && (old_mode & Pollable::WRITE)) {
            // del WRITE
            bzero(&ev, sizeof(ev));
            ev.ident = fd;
            ev.udata = poll;
            ev.flags = EV_DELETE;
            ev.filter = EVFILT_WRITE;
            verify(kevent(poll_fd_, &ev, 1, nullptr, 0, nullptr) == 0);
        }

#else

        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));

        ev.data.ptr = poll;
        ev.events = EPOLLET | EPOLLRDHUP;
        if (new_mode & Pollable::READ) {
            ev.events |= EPOLLIN;
        }
        if (new_mode & Pollable::WRITE) {
            ev.events |= EPOLLOUT;
        }
        verify(epoll_ctl(poll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0);

#endif

    }

    l_.unlock();
}
void PollMgr::PollThread::trigger_fjob(){
  
	for (auto &fjob: fjobs_) {
	    fjob->trigger();
	}
    
}
static inline uint64_t hash_fd(uint64_t key) {
    uint64_t c2 = 0x27d4eb2d; // a prime or an odd constant
    key = (key ^ 61) ^ (key >> 16);
    key = key + (key << 3);
    key = key ^ (key >> 4);
    key = key * c2;
    key = key ^ (key >> 15);
    return key;
}

void PollMgr::add(Pollable* poll) {
    uint64_t fd = poll->fd();
    if (fd >= 0) {
        int tid = hash_fd(fd) % n_threads_;
        poll_threads_[tid]->add(poll);
        Log_debug("Poll Job added to thread :%d",tid);
    }
}

void PollMgr::remove(Pollable* poll) {
    uint64_t fd = poll->fd();
    if (fd >= 0) {
        int tid = hash_fd(fd) % n_threads_;
        poll_threads_[tid]->remove(poll);
    }
}

void PollMgr::update_mode(Pollable* poll, int new_mode) {
    uint64_t fd = poll->fd();
    if (fd >= 0) {
        int tid = hash_fd(fd) % n_threads_;
        poll_threads_[tid]->update_mode(poll, new_mode);
    }
}

void PollMgr::add(FrequentJob* fjob) {
    int tid = 0;
    poll_threads_[tid]->add(fjob);
}

void PollMgr::remove(FrequentJob* fjob) {
    int tid = 0;
    poll_threads_[tid]->remove(fjob);
}

int PollMgr::set_cpu_affinity(std::bitset<128> &core_mask){
   for(int i=0; i< n_threads_; i++){
    poll_threads_[i]->set_cpu_affinity(core_mask);
   }
}

} // namespace rrr
