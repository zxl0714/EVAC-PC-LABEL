#include "games/guwange/performance/core_phase.h"
#include <iostream>

int main() {
    guwange::performance::CorePhase p;
    p.Begin(false);
    for(unsigned i=0;i<64;++i) if(p.DeferInput() || p.ContinueAtYield()) return 1;
    p.Begin(true);
    if(p.ContinueAtYield() || p.StoppedAtInput()) return 2; // boot: no input
    p.Begin(true);
    if(p.DeferInput() || !p.ContinueAtYield() || !p.DeferInput()) return 3;
    // A 32-op unrolled dispatcher must not consume the next input repeatedly.
    for(unsigned i=0;i<32;++i) if(!p.DeferInput() || p.ContinueAtYield()) return 4;
    for(unsigned i=0;i<600;++i) {
        p.Begin(true);
        if(p.DeferInput() || !p.ContinueAtYield() || !p.DeferInput()) return 5;
    }
    p.Begin(true);
    if(p.DeferInput() || !p.ContinueAtYield() || p.ContinueAtYield()) return 6;
    p.Begin(false);
    if(p.DeferInput() || p.ContinueAtYield() || p.StoppedAtInput()) return 7;
    std::cout<<"core phase: boot, original, bounded yield, repeated dispatch and restart passed\n";
}
