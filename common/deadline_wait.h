#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace cave {
// One waiting owner, notifications from any thread. The caller's mutex and
// predicate retain condition-variable semantics, including spurious wakes.
class DeadlineWait {
public:
    explicit DeadlineWait(bool precise=true) {
#if defined(_WIN32)
        if(!precise)return;
        timer_=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
        highResolution_=timer_!=nullptr;
        if(!timer_)timer_=CreateWaitableTimerExW(nullptr,nullptr,0,TIMER_MODIFY_STATE|SYNCHRONIZE);
        wake_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!timer_ || !wake_)Reset();
#else
        (void)precise;
#endif
    }
    ~DeadlineWait(){Reset();}
    DeadlineWait(const DeadlineWait&)=delete;
    DeadlineWait& operator=(const DeadlineWait&)=delete;
    void Notify() noexcept {
#if defined(_WIN32)
        if(wake_)SetEvent(wake_);
#endif
    }
    bool HighResolution()const noexcept {
#if defined(_WIN32)
        return highResolution_;
#else
        return false;
#endif
    }
    template<class Predicate>
    bool Until(std::unique_lock<std::mutex>& lock,std::chrono::steady_clock::time_point deadline,
               Predicate predicate,std::condition_variable& fallback) {
#if defined(_WIN32)
        if(timer_ && wake_) {
            while(!predicate()) {
                const auto remaining=deadline-std::chrono::steady_clock::now();
                if(remaining<=decltype(remaining)::zero())return false;
                LARGE_INTEGER due{};
                // Round up to 100 ns so a sub-tick deadline never fires early.
                const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
                due.QuadPart=-((ns+99)/100);
                if(!SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE))break;
                const HANDLE handles[]{wake_,timer_};
                lock.unlock();
                const auto result=WaitForMultipleObjects(2,handles,FALSE,INFINITE);
                lock.lock();
                if(result==WAIT_FAILED)break;
            }
            if(predicate())return true;
        }
#endif
        return fallback.wait_until(lock,deadline,predicate);
    }
private:
    void Reset()noexcept {
#if defined(_WIN32)
        if(timer_){CancelWaitableTimer(timer_);CloseHandle(timer_);timer_=nullptr;}
        if(wake_){CloseHandle(wake_);wake_=nullptr;}
        highResolution_=false;
#endif
    }
#if defined(_WIN32)
    HANDLE timer_{},wake_{};
    bool highResolution_=false;
#endif
};
}
