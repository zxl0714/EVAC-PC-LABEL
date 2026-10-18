#include <rex/graphics/guest_video_timing.h>
#include <iostream>
#include <limits>
#include <stdexcept>

void Require(bool condition, const char* text) {
    if (!condition) throw std::runtime_error(text);
}
int main() {
    try {
        using rex::graphics::GuestVblankIntervalTicks;
        using rex::graphics::GuestVblankHostWaitMicros;
        Require(GuestVblankHostWaitMicros(1000000,60000000,1)==16666,"Remaining guest interval to host deadline");
        Require(GuestVblankHostWaitMicros(1000000,60000000,2)==8333,"Guest time scale preserved");
        Require(GuestVblankHostWaitMicros(0,60000000,1)==0,"Due frame must not sleep");
        Require(GuestVblankHostWaitMicros(UINT64_MAX,1,1)==100000,"Deadline wait bound");
        Require(GuestVblankHostWaitMicros(1,0,1)==1000 && GuestVblankHostWaitMicros(1,1,0)==1000,"Invalid clock wait fallback");
        Require(GuestVblankIntervalTicks(1000000,60)==16666,"60 Hz cadence");
        Require(GuestVblankIntervalTicks(1000000,59.94)==16683,"Fractional refresh cadence");
        Require(GuestVblankIntervalTicks(3000000,60)==50000,"Different timer frequency");
        Require(GuestVblankIntervalTicks(2400000000ULL,60)==40000000,"High timer frequency");
        Require(GuestVblankIntervalTicks(1000000,120)==8333,"120 Hz video mode");
        Require(GuestVblankIntervalTicks(1,60)==1 && GuestVblankIntervalTicks(0,60)==1,"Minimum tick bounds");
        Require(GuestVblankIntervalTicks(1000000,0)==1000000,"Invalid refresh clamp");
        Require(GuestVblankIntervalTicks(1000000,std::numeric_limits<double>::quiet_NaN())==1000000,"NaN refresh");
        Require(GuestVblankIntervalTicks(std::numeric_limits<uint64_t>::max(),1)==std::numeric_limits<uint64_t>::max(),"Large timer bound");
        // The original video timer's catch-up loop, with deterministic time.
        const auto interval=GuestVblankIntervalTicks(60000000,60);
        uint64_t last{}, frames{};
        for(uint64_t now=0;now<=120000000;now+=60000) {
            while(now-last>=interval) {++frames;last+=interval;}
        }
        Require(frames==120,"Two seconds did not produce 120 guest vblanks");
        std::cout<<"Guest video cadence passed; no host vsync or CPU-specific frequency input\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 2;}
}
