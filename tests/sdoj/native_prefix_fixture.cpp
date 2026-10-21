// Optional local-data test: compile with MODULE=1/2 and a prepared prefix.inc.
// Runs no game loop, GPU, audio, files or real input; getters use synthetic data.
#include "latency/native_prefix.h"
#include "latency/guest_call.h"
#include <windows.h>
#include <array>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
#include <tuple>

using sdoj::latency::NativePrefixResult;
constexpr std::uint32_t Renderer=0x10000, Control=0x20000, Engine=0x30000, Vtable=0x40000;
constexpr std::uint32_t ControlSlot=0x888791DC+(MODULE==1 ? 0 : 0x8528);
std::array<std::array<std::int32_t,64>,4> actions{};
NativePrefixResult frozen, cut;
bool late{};
std::vector<std::tuple<unsigned,unsigned,int>> reads;
unsigned properties{}, cases{}, checks{};
void Check(bool value,const char* message) {
    ++checks; if(!value) throw std::runtime_error(message);
}
#define REX_FUNC_PROLOGUE() ((void)0)
#define DEFINE_REX_FUNC(name) void name(PPCContext& ctx, std::uint8_t* base)
#define REX_LOAD_U8(x) (*(base+std::uint32_t(x)))
#define REX_LOAD_U32(x) __builtin_bswap32(*reinterpret_cast<std::uint32_t*>(base+std::uint32_t(x)))
#define REX_LOAD_U64(x) __builtin_bswap64(*reinterpret_cast<std::uint64_t*>(base+std::uint32_t(x)))
#define REX_STORE_U8(x,v) (*(base+std::uint32_t(x))=std::uint8_t(v))
#define REX_STORE_U32(x,v) (*reinterpret_cast<std::uint32_t*>(base+std::uint32_t(x))=__builtin_bswap32(std::uint32_t(v)))
#define REX_STORE_U64(x,v) (*reinterpret_cast<std::uint64_t*>(base+std::uint32_t(x))=__builtin_bswap64(std::uint64_t(v)))
void Getter(PPCContext& ctx,std::uint8_t* base) {
    if(ctx.ctr.u32==1) { ++properties; REX_STORE_U32(ctx.r4.u32,-1); ctx.r3.u64=1; return; }
    Check(ctx.ctr.u32==2 && ctx.r4.u32<4 && ctx.r5.u32<64,"unexpected native action getter");
    const auto value=actions[ctx.r4.u32][ctx.r5.u32];
    reads.emplace_back(ctx.r4.u32,ctx.r5.u32,value); ctx.r3.s64=value;
}
#define REX_CALL_INDIRECT_FUNC(x) Getter(ctx,base)
bool SdojDelayInputStart(PPCContext& ctx,std::uint8_t* base) {
    if(!late) return false;
    Check(frozen.Restore<MODULE>(ctx,base,ctx.r3.u32),"restore the actual production prefix receipt");
    return true;
}
bool SdojDelayInputDone(PPCContext& ctx,std::uint8_t* base) {
    cut.Capture<MODULE>(ctx,base);
    if(!late) frozen=cut;
    return true;
}
#include "prefix.inc"

using Controls=std::array<std::int32_t,128>;
std::array<std::uint8_t,7> Oracle(Controls& control) {
    std::array<std::uint8_t,7> ports; ports.fill(0xff);
    std::array<bool,2> joined{};
    if(control[328/4] && control[460/4]) {
        const unsigned player=unsigned(control[320/4]-1)&1;
        if(control[95+player]<0) {
            control[95+player]=control[456/4];
            joined[player]=control[93+player]==0;
            if(!joined[player])control[460/4]=0;
        } else control[460/4]=0;
    }
    for(unsigned player=0;player<2;++player) {
        const auto device=control[95+player]; if(device<0) continue;
        auto held=[&](int action) { return actions[device][action]; };
        auto key=[&](int action) { return actions[0][action+(player ? 13:0)]; };
        unsigned bits=0;
        if(held(20)||held(0)||held(13)>=9000||key(27)) bits|=1;
        if(held(1)||held(13)<=-9000||key(28)) bits|=2;
        if(held(2)||held(12)<=-9000||key(29)) bits|=4;
        if(held(21)||held(3)||held(12)>=9000||key(30)) bits|=8;
        for(unsigned button=0;button<4;++button) {
            const auto mapped=control[(152+player*64)/4+button];
            const auto alternate=control[(120+player*64)/4+button];
            if((mapped>=0 && (held(mapped)||key(31+button))) ||
               (alternate>=0 && held(alternate))) bits|=16u<<button;
        }
        if(held(26)||held(4)||key(35)||joined[player]) ports[0]&=~(16u<<player);
        ports[2+player]=std::uint8_t(~bits);
    }
    return ports;
}
PPCContext Context() {
    PPCContext ctx{}; ctx.r1.u64=0x180000; ctx.r3.u64=Renderer; ctx.lr=0x12345678;
    auto* gpr=&ctx.r20; for(unsigned i=0;i<12;++i)gpr[i].u64=0x1234567800000000ull+i;
    auto* fpr=&ctx.f28; for(unsigned i=0;i<4;++i)fpr[i].u64=0x4000123456789000ull+i;
    return ctx;
}
void CheckAbi(const PPCContext& before,const PPCContext& after) {
    Check(before.r1.u64==after.r1.u64 && before.lr==after.lr,"native stack and LR restored");
    Check(!std::memcmp(&before.r20,&after.r20,12*sizeof(PPCRegister)),"native nonvolatile GPR restore");
    Check(!std::memcmp(&before.f28,&after.f28,4*sizeof(PPCRegister)),"native nonvolatile FPR restore");
}
void Setup(std::uint8_t* base,const Controls& control) {
    std::memset(base+Renderer,0,4096); std::memset(base+Control,0,4096);
    REX_STORE_U32(ControlSlot,Control); REX_STORE_U32(Renderer+588,Engine);
    REX_STORE_U32(Engine,Vtable); REX_STORE_U32(Vtable+332,1); REX_STORE_U32(Vtable+472,2);
    for(unsigned i=0;i<control.size();++i)REX_STORE_U32(Control+i*4,control[i]);
    reads.clear(); properties=0; late=false; frozen={}; cut={};
}
void Test(std::uint8_t* base,const Controls& initial) {
    ++cases; auto expected_control=initial; const auto expected=Oracle(expected_control);
    Setup(base,initial); auto ctx=Context(); const auto caller=ctx;
    NativePrefix(ctx,base); CheckAbi(caller,ctx);
    Check(cut.ports==expected,"all seven native input ports match independent action oracle");
    const auto reference=cut; const auto reference_reads=reads;
    for(unsigned i=0;i<initial.size();++i)
        Check(std::int32_t(REX_LOAD_U32(Control+i*4))==expected_control[i],"native join/controller side effects match oracle");
    Setup(base,initial); ctx=Context(); const auto before_side=ctx;
    const auto before_csr=ctx.fpscr.getcsr();
    {
        sdoj::latency::GuestCall call(ctx);
        Check(ctx.r1.u32==before_side.r1.u32-0x200,"production side-call linkage is separate from its caller");
        const auto side_caller=ctx; NativePrefix(ctx,base); CheckAbi(side_caller,ctx);
        ctx.fpscr.setcsr(before_csr^0x8000); // Exercise host FTZ restoration too.
    }
    Check(!std::memcmp(&ctx,&before_side,sizeof(ctx)) && ctx.fpscr.getcsr()==before_csr,
          "production side call restores the full PPC context and host FPSCR");
    Check(cut.ports==reference.ports && reads==reference_reads && properties==1,
          "early native prefix has identical ports, getter sequence and one property read");
    // The real late call has its own prologue/epilogue, not the side-call stack.
    const auto before_reads=reads.size(); ctx=Context(); late=true;
    NativePrefix(ctx,base); CheckAbi(caller,ctx);
    Check(cut.ports==reference.ports && !std::memcmp(cut.live.data(),reference.live.data(),sizeof(cut.live)),
          "late parser entry receives exact native live-through values");
    Check(reads.size()==before_reads && properties==1 && !frozen.captured,
          "late skip performs no second input query or join update and consumes once");
    frozen=reference; base[Renderer+70]^=1;
    Check(!frozen.Restore<MODULE>(ctx,base,Renderer),"mutated frozen ports reject restoration");
    base[Renderer+70]^=1;
    Check(!frozen.Restore<MODULE>(ctx,base,Renderer+16),"foreign renderer rejects restoration");
}
int main() {
    auto* base=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,0x100000000ull,MEM_RESERVE,PAGE_READWRITE));
    try {
        Check(base && VirtualAlloc(base,0x200000,MEM_COMMIT,PAGE_READWRITE) &&
              VirtualAlloc(base+0x88870000,0x20000,MEM_COMMIT,PAGE_READWRITE),"sparse synthetic guest memory");
        Controls control{}; control[95]=2; control[96]=0;
        for(unsigned player=0;player<2;++player) for(unsigned button=0;button<4;++button) {
            control[(120+player*64)/4+button]=int(5+button);
            control[(152+player*64)/4+button]=int(15+button);
        }
        for(unsigned device: {0u,2u}) for(unsigned action=0;action<64;++action)
            for(int value: {0,1,-9001,-9000,-8999,8999,9000,9001}) {
                actions={}; actions[device][action]=value; Test(base,control);
            }
        std::mt19937 random(0x53444f4a);
        for(unsigned sample=0;sample<256;++sample) {
            auto custom=control;
            for(auto& device:actions)for(auto& action:device)action=int(random()%20003)-10001;
            for(unsigned player=0;player<2;++player)for(unsigned button=0;button<4;++button) {
                custom[(120+player*64)/4+button]=int(random()%65)-1;
                custom[(152+player*64)/4+button]=int(random()%65)-1;
            }
            Test(base,custom);
        }
        actions={};
        for(unsigned player=0;player<2;++player)for(int assigned: {-1,0,2})for(int flag: {0,1}) {
            auto join=control; join[95+player]=assigned; join[93+player]=flag;
            join[328/4]=1; join[460/4]=1; join[456/4]=3; join[320/4]=int(player+1);
            Test(base,join);
        }
        auto exception_ctx=Context(); const auto exception_before=exception_ctx;
        const auto exception_csr=exception_ctx.fpscr.getcsr();
        try {
            sdoj::latency::GuestCall call(exception_ctx);
            exception_ctx.r0.u64=0xdeadbeef; exception_ctx.fpscr.setcsr(exception_csr^0x8000);
            throw std::runtime_error("authored side-call exception");
        } catch(const std::runtime_error&) {}
        Check(!std::memcmp(&exception_ctx,&exception_before,sizeof(exception_ctx)) &&
              exception_ctx.fpscr.getcsr()==exception_csr,"side-call unwinding restores PPC and host floating state");
        std::cout<<"module="<<MODULE<<" cases="<<cases<<" checks="<<checks
                 <<" all_ports/action_thresholds/mappings/two_players/join/cut_abi passed\n";
        VirtualFree(base,0,MEM_RELEASE); return 0;
    } catch(const std::exception& error) {
        std::cerr<<"case="<<cases<<" check="<<checks<<" "<<error.what()<<'\n';
        if(base)VirtualFree(base,0,MEM_RELEASE); return 1;
    }
}
