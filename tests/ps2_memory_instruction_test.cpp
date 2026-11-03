#include "ps2_memory_fixture.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

extern const uint32_t g_ps2RecompiledFunctionTableBase=0x100000u;
extern const uint32_t g_ps2RecompiledFunctionTableEnd=0x100004u;
extern const uint32_t g_ps2RecompiledFunctionTableSlotCount=1u;
PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[1]{};

int main(int argc,char** argv) {
    try {
        PS2Runtime state;
        if(!state.memory().initialize()) throw std::runtime_error("Fixture memory allocation failed");
        auto* rdram=state.memory().getRDRAM();
        if(argc==2 && std::string_view(argv[1])=="--bench") {
            for(const uint32_t address : {0x10000u,0x70000100u}) {
                std::array<double,9> original{},shared{};
                uint64_t checksum[2]{};
                for(unsigned round=0;round<9;++round) for(unsigned step=0;step<2;++step) {
                    const unsigned mode=(round+step)%2;
                    R5900Context ctx{};
                    SET_GPR_U64((&ctx),7,address); SET_GPR_U64((&ctx),29,1234567u);
                    auto start=std::chrono::steady_clock::now();
                    checksum[mode]=(mode?SharedMemoryBench:OriginalMemoryBench)(rdram,&ctx,&state,100000);
                    (mode?shared:original)[round]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                }
                if(checksum[0]!=checksum[1])throw std::runtime_error("Benchmark results differ");
                std::sort(original.begin(),original.end());std::sort(shared.begin(),shared.end());
                std::cout<<"{\"address\":"<<address<<",\"originalMs\":"<<original[4]<<",\"sharedMs\":"<<shared[4]
                         <<",\"checksum\":"<<checksum[0]<<"}\n";
            }
            return 0;
        }
        unsigned comparisons{};
        for(const auto& fixture:kMemoryFixtures)
            for(uint32_t address : {0x100u,0x80000100u,0xa0000100u,0x1fffffdu,0x1ffffffu,0x70000110u}) {
                R5900Context initial{};
                for(unsigned reg=1;reg<32;++reg)
                    initial.r[reg]=_mm_set_epi64x(0x456789abcdef0000ull+reg,0xf123456780000000ull+reg*17u);
                initial.pc=0x123454u;initial.in_delay_slot=true;initial.branch_pc=0x123450u;
                if(fixture.rs) SET_GPR_U64((&initial),fixture.rs,address-uint32_t(fixture.offset));
                else address=uint32_t(fixture.offset);
                auto restore=[&] {
                    for(unsigned i=0;i<32;++i) {
                        const auto at=address-8u+i;
                        const auto value=uint8_t(0x91u+i*19u);
                        if(PS2Runtime::isSpecialAddress(at)) state.Store8(rdram,&initial,at,value);
                        else rdram[at&PS2_RAM_MASK]=value;
                    }
                };
                auto bytes=[&](R5900Context& ctx) {
                    std::array<uint8_t,32> result{};
                    for(unsigned i=0;i<32;++i)result[i]=cave::ps2::Read8(rdram,&ctx,&state,address-8u+i);
                    return result;
                };
                restore();auto expected=initial;fixture.original(rdram,&expected,&state);const auto expectedBytes=bytes(expected);
                restore();auto actual=initial;fixture.shared(rdram,&actual,&state);const auto actualBytes=bytes(actual);
                if(std::memcmp(&actual,&expected,sizeof(actual)) || actualBytes!=expectedBytes)
                    throw std::runtime_error("Memory instruction differs: op="+std::to_string(fixture.op)+
                        " rs="+std::to_string(fixture.rs)+" rt="+std::to_string(fixture.rt)+" offset="+std::to_string(fixture.offset));
                ++comparisons;
            }
        std::cout<<"PS2 generated memory instructions: "<<comparisons<<" original/shared state and memory comparisons passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
