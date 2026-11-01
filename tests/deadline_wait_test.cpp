#include "common/deadline_wait.h"
#include "common/windows_timer_resolution.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace {
void Require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
void Contract(bool precise) {
    cave::DeadlineWait waiter(precise);
    std::condition_variable fallback;
    std::mutex mutex;
    std::unique_lock lock(mutex);
    bool ready=true;
    Require(waiter.Until(lock,Clock::now()+1s,[&]{return ready;},fallback),"Ready predicate was lost");
    ready=false;
    waiter.Notify(); // An old signal must never satisfy a false predicate.
    const auto deadline=Clock::now()+3ms;
    Require(!waiter.Until(lock,deadline,[&]{return ready;},fallback) && Clock::now()>=deadline,
            "Spurious notification completed a timeout early");
    std::thread producer([&]{
        {std::lock_guard guard(mutex);ready=true;}
        waiter.Notify();fallback.notify_one();
    });
    const bool signaled=waiter.Until(lock,Clock::now()+1s,[&]{return ready;},fallback);
    lock.unlock();producer.join();
    Require(signaled,"Cross-thread notification was lost");
    lock.lock();ready=false;
    for(unsigned i=0;i<40;++i){
        lock.unlock();
        {std::lock_guard guard(mutex);ready=true;}
        waiter.Notify();fallback.notify_one(); // Notification precedes the wait.
        lock.lock();
        Require(waiter.Until(lock,Clock::now()+1s,[&]{return ready;},fallback),"Pre-wait notification was lost");
        ready=false;
    }
}
void Bench() {
    cave::ScopedTimerResolution resolution;resolution.Enable();
    cave::DeadlineWait precise,coarse(false);
    std::condition_variable fallback;
    std::mutex mutex;
    std::unique_lock lock(mutex);
    std::vector<double> rows[2];
    for(unsigned i=0;i<80;++i)for(unsigned which=0;which<2;++which){
        const auto deadline=Clock::now()+4ms;
        (which?precise:coarse).Until(lock,deadline,[]{return false;},fallback);
        rows[which].push_back(std::chrono::duration<double,std::micro>(Clock::now()-deadline).count());
    }
    for(unsigned which=0;which<2;++which){
        auto& row=rows[which];std::sort(row.begin(),row.end());
        std::cout<<"deadline_bench mode="<<(which?"waitable_timer":"condition_variable")
                 <<" high_resolution="<<(which && precise.HighResolution())
                 <<" median_late_us="<<row[row.size()/2]<<" p95_late_us="<<row[row.size()*95/100]
                 <<" max_late_us="<<row.back()<<'\n';
    }
}
}
int main(int argc,char** argv) {
    try {
        Contract(true);Contract(false);
        if(argc==2 && std::string_view(argv[1])=="--bench")Bench();
        else if(argc!=1)throw std::runtime_error("Unknown deadline test argument");
        std::cout<<"Deadline waits: timeout, predicate, cross-thread and pre-wait notification: PASS\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
