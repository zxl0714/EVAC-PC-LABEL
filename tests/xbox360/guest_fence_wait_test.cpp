#include "games/espgaluda2/performance/fence_wait.h"
#include <rex/thread/guest_memory_wait.h>
#include <atomic>
#include <array>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using espgaluda2::performance::FencePending;
using rex::thread::WaitGuestMemoryWord;
using rex::thread::NotifyGuestMemoryWord;
unsigned checks{};
struct OffsetMemory {
    std::array<std::uint32_t, 1026> words{};
    template <typename T> T TranslateVirtual(std::uint32_t address) const {
        return reinterpret_cast<T>(reinterpret_cast<const std::uint8_t*>(words.data()) +
            (address & 0xfff) + (address >= 0xe0000000 ? 0x1000 : 0));
    }
};
void Require(bool yes, const char* message) {
    ++checks;
    if (!yes) throw std::runtime_error(message);
}
std::uint64_t Ticks(FILETIME t) {return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
double CpuMs() {
    FILETIME created{},exited{},kernel{},user{};
    if (!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))
        throw std::runtime_error("GetThreadTimes");
    return (Ticks(kernel)+Ticks(user))/10000.0;
}
int main() {
    try {
        OffsetMemory offset_memory;
        offset_memory.words[0] = 0x01000000;
        offset_memory.words[1024] = 0x02000000;
        Require(espgaluda2::performance::ReadGuestWord(offset_memory, 0xa0000000) == 1,
            "Ordinary alias uses its mapped word");
        Require(espgaluda2::performance::ReadGuestWord(offset_memory, 0xe0000000) == 2,
            "Physical alias respects translation's host offset");
        for (auto last : {0u,1u,0x7fffffffu,0xfffffffeu,0xffffffffu}) {
            for (unsigned target_age=0;target_age!=20;++target_age)
                for (unsigned done_age=0;done_age!=20;++done_age)
                    Require(FencePending(last,last-target_age,last-done_age)==(target_age<done_age),
                        "Original modulo fence comparison");
        }
        // Two aliases of the same physical backing. Both wait/notify endpoints
        // use the canonical one even when the native predicate reads the other.
        HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,65536,nullptr);
        Require(mapping!=nullptr,"Create alias backing");
        auto* canonical=static_cast<std::uint32_t*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,65536));
        auto* alias=static_cast<std::uint32_t*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,65536));
        Require(canonical && alias && canonical!=alias,"Distinct aliases");
        std::atomic_ref<std::uint32_t> value(*canonical), native(*alias);
        value.store(0);
        Require(!WaitGuestMemoryWord(canonical,0,0),"Unchanged word bounded timeout");
        value.store(1);NotifyGuestMemoryWord(canonical);
        Require(WaitGuestMemoryWord(canonical,0,0),"Notify before wait is retained by predicate");
        for (unsigned round=1;round<=256;++round) {
            value.store(round);
            auto waiter=std::async(std::launch::async,[&] {
                while (native.load()==round)
                    if (!WaitGuestMemoryWord(canonical,round,500)) return false;
                return true;
            });
            if (round%2) std::this_thread::sleep_for(1ms);
            value.store(round+1);NotifyGuestMemoryWord(canonical);
            Require(waiter.get(),"Concurrent canonical notification");
            Require(native.load()==round+1,"Native alias reads producer's exact value");
        }
        value.store(0x04030201);
        Require(!WaitGuestMemoryWord(canonical,0x04030201,0),"Stored byte order, not host decoded value");
        Require(WaitGuestMemoryWord(canonical,0x01020304,0),"Different byte order does not block");
        auto spurious=std::async(std::launch::async,[&]{return WaitGuestMemoryWord(canonical,0x04030201,20);});
        std::this_thread::sleep_for(1ms);NotifyGuestMemoryWord(canonical);spurious.get();
        Require(native.load()==0x04030201,"Notification never advances the counter");
        // Production wait versus the original repeated predicate, 60 fixed
        // producer updates. This measures waiting cost, not game performance.
        for (const bool park : {false,true}) {
            value.store(0);
            std::atomic<bool> ready{},failed{};
            auto worker=std::async(std::launch::async,[&] {
                const auto before=CpuMs();ready.store(true);
                unsigned observed=0;
                while (observed<60) {
                    const auto next=native.load();
                    if (next!=observed) observed=next;
                    else if (park && !WaitGuestMemoryWord(canonical,observed,500)) {failed=true;break;}
                }
                return CpuMs()-before;
            });
            while(!ready.load()) std::this_thread::yield();
            const auto begin=std::chrono::steady_clock::now();
            for (unsigned tick=1;tick<=60;++tick) {
                std::this_thread::sleep_until(begin+std::chrono::microseconds(16667*tick));
                value.store(tick);NotifyGuestMemoryWord(canonical);
            }
            std::cout<<"park="<<park<<" worker_cpu_ms="<<worker.get()<<'\n';
            Require(!failed.load(),"Sixty producer handoffs");
        }
        UnmapViewOfFile(alias);UnmapViewOfFile(canonical);CloseHandle(mapping);
        std::cout<<checks<<" guest fence checks passed\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
