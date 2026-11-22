#pragma once
#include "vif1_dma_worker.h"
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {
void VifDmaWorkerOrdering() {
    PS2Memory memory;
    Require(memory.initialize(), "VIF worker fixture memory initialization failed");
    memory.setRawGifTransfers(true);
    constexpr uint32_t tag = 0x10000, qwc = 0x4000;
    memory.write64(tag, (uint64_t(7) << 28) | qwc);
    memory.write32(tag + 16 + qwc * 16 - 4, 0x14000000); // MSCAL after 256 KiB of NOPs.
    const auto caller = std::this_thread::get_id();
    std::thread::id executed;
    bool reject = false;
    memory.setVu1MscalCallback([&](uint32_t, uint32_t, uint32_t) {
        executed = std::this_thread::get_id();
        if (reject) throw std::runtime_error("VIF worker test exception");
    });
    const auto run = [&](uint32_t tagAddress) {
        memory.writeIORegister(0x10009030, tagAddress);
        memory.writeIORegister(0x10009000, 0x105);
        Require(memory.consumeCompletedDmacCauses() == std::vector<uint32_t>{1},
                "VIF DMA returned before completion");
    };
    run(tag);
    Require(executed == caller, "Unscoped VIF DMA did not run on the EE");
    {
        cave::ps2::Vif1DmaWorker worker(memory);
        constexpr uint32_t smallTag = 0x9000;
        memory.write64(smallTag, (uint64_t(7) << 28) | 1u);
        memory.write32(smallTag + 16, 0x14000000);
        run(tag);
        const auto deviceThread = executed;
        Require(deviceThread != caller, "First VIF chain ran on the EE");
        run(smallTag);
        Require(executed == deviceThread, "Small VIF chain switched threads");
        run(tag);
        Require(executed == deviceThread, "Large VIF chain after a small transfer ran on the EE");
    }
    run(tag);
    Require(executed == caller, "VIF worker survived the game session");
    cave::ps2::Vif1DmaWorker worker(memory);
    reject = true;
    try { run(tag); }
    catch (const std::runtime_error& error) {
        Require(std::string_view(error.what()) == "VIF worker test exception", "Wrong VIF exception");
        return;
    }
    throw std::runtime_error("VIF worker lost a rendering error");
}

void VifTagPayloads() {
    PS2Memory memory;
    Require(memory.initialize(),"VIF tag fixture memory initialization failed");
    const uint32_t payload[] = {0x12345678, 0xabcd6789, 0x80000000, 0xffffffff};
    constexpr uint32_t destination = 0x12;
    constexpr uint64_t unpack = uint64_t(0x6c010000u | destination) << 32;
    const auto check = [&](uint32_t tag, uint64_t command, uint32_t source) {
        memory.write64(tag, command);
        memory.write64(tag + 8, unpack); // NOP, then UNPACK V4-32 of one vector.
        for (unsigned i = 0; i < 4; ++i)
            memory.write32(source + 4 * i, payload[i]);
        auto *result = memory.getVU1Data() + 16 * destination;
        std::memset(result, 0, sizeof(payload));
        memory.writeIORegister(0x10009030, tag);
        memory.writeIORegister(0x10009000, 0x145); // VIF1 source chain with TTE.
        Require(!std::memcmp(result, payload, sizeof(payload)), "VIF tag or its payload changed");
    };
    constexpr uint64_t end = (uint64_t(7) << 28) | 1u;
    check(0x1000, end, 0x1010);
    check(0x20300000, end, 0x20300010); // EE uncached RAM mirror.
    check(0x21fffff0, end, 0);          // Inline data wraps to the first RAM quadword.
    check(0x70000100, end, 0x70000110); // Scratchpad.
    check(0x2000, (uint64_t(0x2400) << 32) | 1u, 0x2400); // REFE.

    // A shorter chain must end before the previous chain's second UNPACK.
    constexpr uint32_t tag = 0x3000, second = destination + 1;
    memory.write64(tag, (uint64_t(7) << 28) | 3u);
    memory.write64(tag + 8, unpack);
    for (unsigned i = 0; i < 4; ++i)
        memory.write32(tag + 16 + 4 * i, payload[i]);
    memory.write32(tag + 32, 0x6c010000u | second);
    for (unsigned i = 0; i < 4; ++i)
        memory.write32(tag + 36 + 4 * i, payload[i]);
    memory.writeIORegister(0x10009030, tag);
    memory.writeIORegister(0x10009000, 0x145);
    auto *secondResult = memory.getVU1Data() + 16 * second;
    Require(!std::memcmp(secondResult, payload, sizeof(payload)), "Long VIF chain lost its tail");
    std::memset(secondResult, 0, sizeof(payload));
    check(tag, end, tag + 16);
    std::array<uint8_t, sizeof(payload)> zero{};
    Require(!std::memcmp(secondResult, zero.data(), zero.size()), "Short VIF chain replayed the previous tail");
}

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
