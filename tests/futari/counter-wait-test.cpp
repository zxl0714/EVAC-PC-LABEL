#include "games/futari/performance/counter_wait.h"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace futari::performance;
using namespace std::chrono_literals;
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){
    try {
        alignas(8) std::uint64_t word=0;
        std::atomic_ref<std::uint64_t> value(word);
        Require(!WaitCounter(&word,0,0),"Unchanged word times out");
        Require(GetLastError()==ERROR_TIMEOUT,"Timeout is reported");
        value.store(1); NotifyCounter(&word);
        Require(WaitCounter(&word,0,0),"Notification before wait is not lost");
        Require(value.load()==1,"Wait never modifies native counter");
        for(unsigned i=1;i<=256;++i){
            value.store(i);
            auto waiter=std::async(std::launch::async,[&]{
                // Same predicate recheck as the unmodified native loop.
                while(value.load()==i)Require(WaitCounter(&word,i,500),"Concurrent notification arrives");
            });
            if(i%2)std::this_thread::sleep_for(1ms);
            value.store(i+1); NotifyCounter(&word);
            waiter.get();
            Require(value.load()==i+1,"Exactly the producer's value");
        }
        value.store(0x0807060504030201ull);
        Require(WaitCounter(&word,0x0102030405060708ull,0),"Comparison uses stored byte order");
        Require(!WaitCounter(&word,0x0807060504030201ull,0),"Complete 64-bit match blocks");
        // A signal alone is not permission to advance the game's frame.
        auto spur=std::async(std::launch::async,[&]{return WaitCounter(&word,value.load(),20);});
        std::this_thread::sleep_for(1ms);NotifyCounter(&word);spur.get();
        Require(value.load()==0x0807060504030201ull,"Spurious wake preserves predicate");
        std::cout<<"Futari counter wait: 256 concurrent handoffs and predicate checks passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
