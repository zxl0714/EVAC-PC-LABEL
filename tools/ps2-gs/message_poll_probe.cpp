#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "message_poll_candidate.h"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
uint64_t received=0,sent=0;
LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==WM_APP){received+=wparam;return 0;}
    if(message==WM_APP+1){++sent;return 1;}
    return DefWindowProcW(window,message,wparam,lparam);
}
void Pump(bool hints,cave::ps2::WindowMessagePoll& poll) {
    if(hints && !poll.Due(std::chrono::steady_clock::now()))return;
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))DispatchMessageW(&message);
}
uint64_t Cycles() {
    ULONG64 value{};
    if(!QueryThreadCycleTime(GetCurrentThread(),&value))throw std::runtime_error("Cannot read thread cycles");
    return value;
}
}
int main() {
    try {
        WNDCLASSW info{};info.lpfnWndProc=Procedure;info.hInstance=GetModuleHandleW(nullptr);
        info.lpszClassName=L"EVAC.PS2.MessageProbe";
        if(!RegisterClassW(&info))throw std::runtime_error("Cannot register probe window");
        const auto window=CreateWindowW(info.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,info.hInstance,nullptr);
        if(!window)throw std::runtime_error("Cannot create message-only probe window");
        constexpr uint64_t count=500000;
        std::cout<<"mode,iterations,cpu_cycles,wall_ms\n";
        for(unsigned mode:{0u,1u,1u,0u,0u,1u,1u,0u}) {
            cave::ps2::WindowMessagePoll poll;
            for(unsigned i=0;i<1000;++i)Pump(mode!=0,poll);
            const auto start=std::chrono::steady_clock::now();const auto before=Cycles();
            for(uint64_t i=0;i<count;++i)Pump(mode!=0,poll);
            const auto cycles=Cycles()-before;
            const auto wall=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            std::cout<<mode<<','<<count<<','<<cycles<<','<<wall<<'\n';
            received=sent=0;
            for(unsigned batch=0;batch<100;++batch) {
                for(unsigned i=1;i<=64;++i)
                    if(!PostMessageW(window,WM_APP,i,0))throw std::runtime_error("PostMessage failed");
                Pump(mode!=0,poll);
            }
            if(received!=100ull*64*65/2)throw std::runtime_error("Posted messages were lost");
            bool finished=false;
            std::thread sender([&]{
                for(unsigned i=0;i<32;++i) {
                    DWORD_PTR result{};
                    if(!SendMessageTimeoutW(window,WM_APP+1,0,0,SMTO_ABORTIFHUNG,2000,&result) || result!=1)
                        std::terminate();
                }
                PostMessageW(window,WM_APP+2,0,0);
            });
            // The owned test thread sends real nonqueued messages. Completion
            // is observed through its posted sentinel rather than a shared flag.
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(!finished && std::chrono::steady_clock::now()<deadline) {
                if(!mode || poll.Due(std::chrono::steady_clock::now())) {
                    MSG message{};
                    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                        if(message.message==WM_APP+2)finished=true;
                        DispatchMessageW(&message);
                    }
                }
                std::this_thread::yield();
            }
            sender.join();
            if(!finished || sent!=32)throw std::runtime_error("Sent messages stalled");
        }
        DestroyWindow(window);
        std::cout<<"Posted and nonqueued message delivery: PASS\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
