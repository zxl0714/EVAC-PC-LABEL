#pragma once
#include "runtime/ps2_memory.h"
#include <array>
#include <cstring>
#include <vector>

namespace {
void VifImageContinuation(PS2Memory& memory) {
    std::vector<std::vector<uint8_t>> received;
    GifArbiter arbiter([&](GifPathId path,const uint8_t* data,uint32_t bytes){
        Require(path==GifPathId::Path2,"VIF DIRECT lost GIF path2");
        received.emplace_back(data,data+bytes);
    });
    memory.setGifArbiter(&arbiter);
    const auto begin=[&] {
        std::array<uint8_t,20> command{};
        const uint32_t direct=0x50000001;
        const uint64_t tag=2u|(1u<<15)|(uint64_t(2)<<58);
        std::memcpy(command.data(),&direct,4);std::memcpy(command.data()+4,&tag,8);
        memory.processVIF1Data(command.data(),uint32_t(command.size()));
    };
    const std::array<uint8_t,32> image{0x42,0x24,0x66,0x99};
    memory.setRawGifTransfers(true);
    begin();memory.processVIF1Data(image.data(),uint32_t(image.size()));
    Require(received.size()==2 && received[0].size()==16 && received[1]==std::vector<uint8_t>(image.begin(),image.end()),
            "Raw VIF IMAGE continuation gained a synthetic GIF tag");
    received.clear();memory.setRawGifTransfers(false);
    begin();memory.processVIF1Data(image.data(),uint32_t(image.size()));
    Require(received.size()==2 && received[1].size()==48 && std::memcmp(received[1].data()+16,image.data(),32)==0,
            "CPU reference VIF IMAGE compatibility changed");
    memory.setGifArbiter(nullptr);
}
}
