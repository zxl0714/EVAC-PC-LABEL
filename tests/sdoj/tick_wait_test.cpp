#include "games/sdoj/performance/tick_wait.h"
#include "games/sdoj/performance/word_wait.h"
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace std::chrono_literals;
using sdoj::performance::TickWait;
using sdoj::performance::TickPending;
unsigned checks{};
void Check(bool yes) {++checks;if(!yes)throw std::runtime_error("tick wait check");}
double CpuMs() {
    FILETIME c{},e{},k{},u{};GetThreadTimes(GetCurrentThread(),&c,&e,&k,&u);
    return (((std::uint64_t(k.dwHighDateTime)<<32)|k.dwLowDateTime)+
        ((std::uint64_t(u.dwHighDateTime)<<32)|u.dwLowDateTime))/10000.;
}
int main() {
    try {
        sdoj::performance::WordWait callback,pending;
        callback.Start();pending.Start();
        std::atomic<std::uint32_t> word{};
        Check(!pending.Wait(&word,0,0)); // unchanged: original polling fallback
        Check(pending.Wait(&word,0,0));  // no repeated sleeps without a producer
        Check(!callback.Wait(&word,0,0)); // pending timeout cannot disable callback
        pending.Changed(&word);
        Check(!pending.Wait(&word,0,0)); // real producer re-enables parking
        word=1;pending.Changed(&word);
        Check(pending.Wait(&word,0,0));  // producer before wait is retained
        for(unsigned n=0;n<32;++n) {
            word=n;pending.Changed(&word);
            auto task=std::async(std::launch::async,[&]{return pending.Wait(&word,n,500);});
            std::this_thread::sleep_for(1ms);
            word=n+1;pending.Changed(&word);Check(task.get());
        }
        for(auto baseline:{0u,1u,0x7fffffffu,0xfffffffeu})
            for(unsigned age=0;age<10;++age)for(int depth=0;depth<5;++depth)
                Check(TickPending(baseline+age,baseline,depth)==(int(age)<depth));
        TickWait wait;wait.Start(true);
        Check(wait.Wait([]{return false;},0));
        Check(!wait.Wait([]{return true;},0));
        Check(wait.Wait([]{return true;},0)); // poll until a real producer returns
        wait.Notify();
        Check(!wait.Wait([]{return true;},0)); // producer re-arms the bounded wait
        wait.Stop();wait.Notify();
        Check(wait.Wait([]{return true;},0)); // notification cannot undo Stop
        wait.Start(true);
        Check(wait.Wait([&]{wait.Notify();return true;},0)); // notify in check/wait gap
        for(unsigned round=0;round<256;++round) {
            std::atomic<bool> pending{true},entered{};
            auto task=std::async(std::launch::async,[&]{
                while(pending.load()) {
                    if(!wait.Wait([&]{entered=true;return pending.load();},500))return false;
                }
                return true;
            });
            while(!entered.load())std::this_thread::yield();
            pending=false;wait.Notify();Check(task.get());
        }
        auto stopped=std::async(std::launch::async,[&]{return wait.Wait([]{return true;},500);});
        std::this_thread::sleep_for(1ms);wait.Stop();Check(stopped.get());
        for(bool park:{false,true}) {
            wait.Start(true);std::atomic<unsigned> ticks{};std::atomic<bool> ready{};
            auto task=std::async(std::launch::async,[&]{
                auto before=CpuMs();ready=true;
                for(unsigned tick=0;tick<60;++tick)while(ticks.load()==tick) {
                    if(park)wait.Wait([&]{return ticks.load()==tick;},500);
                    else SwitchToThread();
                }
                return CpuMs()-before;
            });
            while(!ready.load())std::this_thread::yield();
            auto begin=std::chrono::steady_clock::now();
            for(unsigned tick=1;tick<=60;++tick){
                std::this_thread::sleep_until(begin+std::chrono::microseconds(16667*tick));
                ticks=tick;wait.Notify();
            }
            std::cout<<"park="<<park<<" cpu_ms="<<task.get()<<'\n';
        }
        std::cout<<checks<<" tick checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
