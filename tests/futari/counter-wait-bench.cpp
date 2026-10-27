#include "games/futari/performance/counter_wait.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace futari::performance;
std::uint64_t Ticks(FILETIME x){return (std::uint64_t(x.dwHighDateTime)<<32)|x.dwLowDateTime;}
double CpuMs(){FILETIME c{},e{},k{},u{};GetThreadTimes(GetCurrentThread(),&c,&e,&k,&u);return (Ticks(k)+Ticks(u))/10000.;}
std::int64_t Qpc(){LARGE_INTEGER x;QueryPerformanceCounter(&x);return x.QuadPart;}
struct Result {double cpu;std::vector<double> wake_us;};
Result Run(bool park) {
    alignas(8) std::uint64_t word=0,ack=0;
    std::atomic_ref<std::uint64_t> counter(word), done(ack);
    std::atomic<std::int64_t> published{};
    LARGE_INTEGER freq;QueryPerformanceFrequency(&freq);
    auto waiter=std::async(std::launch::async,[&]{
        Result result;result.wake_us.reserve(60);const auto cpu=CpuMs();
        for(std::uint64_t frame=1;frame<=60;++frame){
            while(counter.load(std::memory_order_acquire)==frame-1){
                if(park && !WaitCounter(&word,frame-1,500))throw std::runtime_error("counter notification timed out");
            }
            if(counter.load()!=frame)throw std::runtime_error("counter skipped a frame");
            result.wake_us.push_back(double(Qpc()-published.load())*1e6/freq.QuadPart);
            done.store(frame,std::memory_order_release);NotifyCounter(&ack);
        }
        result.cpu=CpuMs()-cpu;return result;
    });
    HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
    if(!timer)throw std::runtime_error("high-resolution timer unavailable");
    for(std::uint64_t frame=1;frame<=60;++frame){
        LARGE_INTEGER due;due.QuadPart=-166667;
        if(!SetWaitableTimerEx(timer,&due,0,nullptr,nullptr,nullptr,0)||WaitForSingleObject(timer,500)!=WAIT_OBJECT_0)
            throw std::runtime_error("cadence timer failed");
        published.store(Qpc());counter.store(frame,std::memory_order_release);NotifyCounter(&word);
        while(done.load()!=frame)if(!WaitCounter(&ack,frame-1,500))throw std::runtime_error("consumer did not finish");
    }
    CloseHandle(timer);return waiter.get();
}
int main(){
    try {
        std::puts("mode,trial,waits,consumer_cpu_ms,wake_median_us,wake_p95_us,wake_max_us");
        for(int trial=0;trial<3;++trial)for(bool park:{false,true}){
            auto r=Run(park);std::sort(r.wake_us.begin(),r.wake_us.end());
            std::printf("%s,%d,60,%.3f,%.3f,%.3f,%.3f\n",park?"address_wait":"original_poll",trial+1,r.cpu,r.wake_us[30],r.wake_us[56],r.wake_us.back());
        }
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
