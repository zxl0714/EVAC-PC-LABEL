#pragma once
#include <cstdint>

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
};
}
