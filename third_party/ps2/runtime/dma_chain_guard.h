#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>

// DMA chains may contain thousands of small drawing packets. A packet-count
// cutoff can leave the GIF halfway through a tag, poisoning later frames.
// Detect repeated traversal state instead (Brent's constant-space algorithm).
class DmaChainGuard {
public:
    void Visit(uint32_t address,uint32_t asp,uint32_t asr0,uint32_t asr1) {
        const Cursor current{address,asp,asp?asr0:0u,asp>1?asr1:0u};
        if(!visited)anchor=current;
        else {
            if(current==anchor)throw std::runtime_error("DMA chain contains a cycle");
            if(++distance==power){anchor=current;power*=2;distance=0;}
        }
        if(++visited>8u*1024u*1024u)
            throw std::runtime_error("DMA chain traversal exceeds safety budget");
    }
    void Append(size_t count) {
        constexpr size_t MaxBytes=256u*1024u*1024u;
        if(count>MaxBytes-bytes)throw std::runtime_error("DMA chain payload exceeds safety budget");
        bytes+=count;
    }
private:
    struct Cursor {
        uint32_t address,asp,asr0,asr1;
        bool operator==(const Cursor&)const=default;
    };
    Cursor anchor{};
    uint32_t visited=0,distance=0,power=1;
    size_t bytes=0;
};
