#pragma once
#include <cstdint>
#include <chrono>

namespace cave::ps2 {
// Complete privileged scanout state for one GS field. Snapshot on the EE
// producer; never sample live guest state later from the window owner.
struct GsFrame {
    uint64_t pmode=0,smode1=0,smode2=0;
    uint64_t srfsh=0,synch1=0,synch2=0,syncv=0;
    uint64_t dispfb1=0,display1=0,dispfb2=0,display2=0;
    uint64_t extbuf=0,extdata=0,extwrite=0,bgcolor=0;
    uint64_t vsyncTick=0;
    uint32_t field=0;
    // Older diagnostic traces can request raw single-field output.
    bool fixedFieldDrawing=false;
    std::chrono::steady_clock::time_point presentAfter{};
    bool Prepared() const {return presentAfter!=std::chrono::steady_clock::time_point{};}
    bool operator==(const GsFrame&) const = default;

    template<class Registers>
    static GsFrame Snapshot(const Registers& r,uint64_t tick,unsigned parity) {
        GsFrame f{};
        f.pmode=r.pmode;f.smode1=r.smode1;f.smode2=r.smode2;
        f.srfsh=r.srfsh;f.synch1=r.synch1;f.synch2=r.synch2;f.syncv=r.syncv;
        f.dispfb1=r.dispfb1;f.display1=r.display1;f.dispfb2=r.dispfb2;f.display2=r.display2;
        f.extbuf=r.extbuf;f.extdata=r.extdata;f.extwrite=r.extwrite;f.bgcolor=r.bgcolor;
        f.vsyncTick=tick;f.field=parity;
        return f;
    }
};
}
