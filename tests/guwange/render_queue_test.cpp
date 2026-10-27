#include "games/guwange/performance/render_queue.h"
#include <cstdint>
#include <iostream>

int main() {
    unsigned checks=0;
    for(auto cursor:{0u,1u,0xfffffffeu,0xffffffffu}) {
        for(unsigned bits=0;bits<64;++bits) {
            const bool actual=guwange::performance::RenderQueueIdle(
                bits&1 ? 0 : 0x100, bits&2 ? 0x200 : 0,
                cursor, cursor+(bits&4 ? 1u : 0u),
                cursor, cursor+(bits&8 ? 1u : 0u),
                bits&16, bits&32);
            ++checks;
            if(actual!=(bits==0)) return 1;
        }
    }
    std::cout<<checks<<" render-queue predicate checks passed\n";
}
