#include "games/espgaluda2/latency/vblank_gate.h"
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using espgaluda2::latency::VblankGate;
using namespace std::chrono_literals;
void Require(bool condition,const char* what) {if(!condition) throw std::runtime_error(what);}
int main() {
    try {
        VblankGate gate;
        using Result=VblankGate::Result;
        Require(gate.Wait(10,0ms)==Result::stopped,"Default stopped state");
        gate.Start();
        gate.Publish(10);
        Require(gate.Wait(10,0ms)==Result::timeout,"Do not reuse setup interrupt");
        gate.Publish(10);
        Require(gate.Wait(10,0ms)==Result::advanced,"Completed VBlank before wait is retained");
        Require(gate.Wait(10,0ms)==Result::timeout,"Never consume one VBlank twice");
        for(int i=0;i<7;++i) gate.Publish(10);
        Require(gate.Wait(10,0ms)==Result::advanced,"Late frame accepts latest completed VBlank");
        Require(gate.Wait(10,0ms)==Result::timeout,"No synthetic catch-up game frames");
        auto waiter=std::async(std::launch::async,[&]{return gate.Wait(10,2s);});
        gate.Publish(10);
        Require(waiter.get()==Result::advanced,"Publish/wait race cannot lose wake");
        gate.Publish(20);
        Require(gate.Wait(10,0ms)==Result::device_changed,"Foreign device cannot advance frame");
        Require(gate.Wait(20,0ms)==Result::timeout,"New device requires fresh interrupt");
        gate.Publish(20);
        Require(gate.Wait(20,0ms)==Result::advanced,"New device advances normally");
        auto stopping=std::async(std::launch::async,[&]{return gate.Wait(20,2s);});
        gate.Stop();
        Require(stopping.get()==Result::stopped,"Stop wakes pending wait");
        gate.Publish(20);
        Require(gate.Wait(20,0ms)==Result::stopped,"Stopped producer cannot resume gate");
        gate.Start();
        Require(gate.Wait(20,0ms)==Result::timeout,"Restart clears prior state");
        for(int i=0;i<1000;++i) {
            auto next=std::async(std::launch::async,[&]{return gate.Wait(20,2s);});
            gate.Publish(20);
            Require(next.get()==Result::advanced,"Concurrent wake stress");
        }
        std::cout<<"G2 VBlank gate: 1013 checks passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
