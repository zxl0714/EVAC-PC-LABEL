#pragma once
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {
void DmaLongChain() {
    PS2Memory memory;
    Require(memory.initialize(),"DMA fixture memory initialization failed");
    constexpr uint32_t first=0x1000,count=6000;
    auto tag=[&](uint32_t address,uint64_t value){memory.write64(address,value);memory.write64(address+8,0);};
    tag(first,(uint64_t(1)<<28)|1);
    memory.write64(first+16,count|(uint64_t(1)<<60)|(1u<<15));memory.write64(first+24,0xe);
    std::vector<uint8_t> expected(16u*(count+1));
    std::memcpy(expected.data(),memory.getRDRAM()+first+16,16);
    for(uint32_t i=0;i<count;++i) {
        const auto address=first+32+i*32;
        tag(address,(uint64_t(1)<<28)|1);
        memory.write64(address+16,i);memory.write64(address+24,0x7f);
        std::memcpy(expected.data()+16u*(i+1),memory.getRDRAM()+address+16,16);
    }
    const auto last=first+32+count*32;tag(last,uint64_t(7)<<28);
    uint32_t calls=0;
    memory.setGifPacketCallback([&](const uint8_t* data,uint32_t size){
        ++calls;Require(size==expected.size() && !std::memcmp(data,expected.data(),size),
            "Long DMA chain truncated a partial GIF tag");
    });
    memory.writeIORegister(0x1000a030,first);memory.writeIORegister(0x1000a000,0x185);
    Require(calls==1 && (memory.read32(0x1000a000)>>28)==7 && !(memory.read32(0x1000a000)&0x100),
        "Long DMA chain did not reach its END tag");

    // Re-entering the same CALL/RET subchain is valid, not a NEXT cycle.
    constexpr uint32_t subchain=0x80000;
    tag(first,(uint64_t(subchain)<<32)|(uint64_t(5)<<28));
    tag(first+16,(uint64_t(subchain)<<32)|(uint64_t(5)<<28));
    tag(first+32,uint64_t(7)<<28);tag(subchain,(uint64_t(6)<<28)|1);
    memory.write64(subchain+16,0x1234);memory.write64(subchain+24,0x7f);
    expected.resize(32);
    std::memcpy(expected.data(),memory.getRDRAM()+subchain+16,16);
    std::memcpy(expected.data()+16,expected.data(),16);calls=0;
    memory.writeIORegister(0x1000a030,first);memory.writeIORegister(0x1000a000,0x185);
    Require(calls==1 && (memory.read32(0x1000a000)>>28)==7,
        "Repeated CALL/RET subchain was mistaken for a cycle");
    // A real NEXT loop must fail before submitting a truncated command stream.
    calls=0;tag(first,(uint64_t(first+16)<<32)|(uint64_t(2)<<28));
    tag(first+16,(uint64_t(first)<<32)|(uint64_t(2)<<28));
    memory.writeIORegister(0x1000a030,first);
    bool rejected=false;
    try{memory.writeIORegister(0x1000a000,0x185);}catch(const std::runtime_error&){rejected=true;}
    Require(rejected && !calls,"Cyclic DMA submitted an incomplete GIF stream");
}
std::vector<uint8_t> DmaFixtureFile(const char* path) {
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot open private DMA fixture");
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void ReplayPrivateDma(const char* ramPath,const char* expectedPath,const char* start) {
    const auto ram=DmaFixtureFile(ramPath),expected=DmaFixtureFile(expectedPath);
    Require(ram.size()==PS2_RAM_SIZE && !expected.empty(),"Invalid private DMA fixture size");
    PS2Memory memory;Require(memory.initialize(),"Private DMA fixture memory initialization failed");
    std::memcpy(memory.getRDRAM(),ram.data(),ram.size());
    uint32_t calls=0;
    memory.setGifPacketCallback([&](const uint8_t* data,uint32_t size){
        ++calls;Require(size==expected.size() && !std::memcmp(data,expected.data(),size),
            "Private DMA chain differs from independently decoded payload");
    });
    memory.writeIORegister(0x1000a030,std::stoul(start,nullptr,0));memory.writeIORegister(0x1000a000,0x185);
    Require(calls==1 && (memory.read32(0x1000a000)>>28)==7,"Private DMA chain did not reach END");
    std::cout<<"Private DMA chain exact payload match: PASS, bytes="<<expected.size()<<'\n';
}
}
