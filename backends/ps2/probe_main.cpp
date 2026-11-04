#include "native_host.h"
#include "collection_launch.h"
// Internal static-recompilation bring-up host. The player-facing launch contract
// is connected only after ISO, input, graphics and save behaviour are verified.
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include <filesystem>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <vector>
#include <fstream>
#include <chrono>
#include <cmath>
#include "frame_trace.h"
#include "audio_capture.h"
#include "diagnostic_input.h"
#include "audio_trace.h"
#include "input_trace.h"
#ifdef EVAC_PS2_IBARA
#include "games/ibara/disc_files.h"
#include "games/ibara/input_timing.h"
#else
#include "games/espgaluda/disc_files.h"
#endif

#ifdef EVAC_PS2_ESPGALUDA_MODULES
extern const uint32_t g_ps2ArcadeFunctionTableBase, g_ps2ArcadeFunctionTableSlotCount;
extern PS2Runtime::RecompiledFunction g_ps2ArcadeFunctionTable[];
extern const uint32_t g_ps2ArrangeFunctionTableBase, g_ps2ArrangeFunctionTableSlotCount;
extern PS2Runtime::RecompiledFunction g_ps2ArrangeFunctionTable[];
#endif

namespace {
#ifdef EVAC_PS2_IBARA
int ProbeIbaraTextGrid(const char* elf,const char* snapshot,bool sprites=false) {
  PS2Runtime runtime;
  if(!runtime.memory().initialize() || !runtime.loadELF(elf))
    throw std::runtime_error("Cannot initialize original Ibara text-grid probe");
  auto* ram=runtime.memory().getRDRAM();
  std::ifstream input(snapshot,std::ios::binary);
  if(!input.read(reinterpret_cast<char*>(ram),PS2_RAM_SIZE) || input.peek()!=std::char_traits<char>::eof())
    throw std::runtime_error("Expected one complete EE RAM snapshot");
  const auto word=[&](uint32_t address){uint32_t v;std::memcpy(&v,ram+address,4);return v;};
  struct Call {float x,y;uint32_t u,v,color;};
  std::vector<Call> expected;
  for(unsigned row=0;row<41;++row)for(unsigned column=0;column<32;++column) {
    const auto cell=word(0x3469b0+(row*32+column)*4);
    if((cell&0xf0000000u)!=0xf0000000u && (!(cell&0x3ffu) || (cell&0x3ffu)==0x20u))continue;
    uint32_t u,v,color;
    if((cell&0xf0000000u)==0xf0000000u) {
      const auto origin=word(0x44b160+((cell&0x3c00u)>>10)*4);
      u=(origin>>16)+248-((cell&0x3e0u)>>2);v=origin+(cell&31u)*8;color=0x808080;
    }else if((cell&0x3000000u)==0x3000000u) {
      u=0x338-((cell&0x3e0u)>>2);v=(cell&31u)*8+0x100;color=(cell&0xfcfcfc00u)>>8;
    }else {
      const auto origin=word(0x338cb0);
      u=(origin>>16)+248-((cell&0x3e0u)>>2);v=(origin&65535)+(cell&31u)*8;color=(cell&0xfcfcfc00u)>>8;
    }
    expected.push_back({float(int32_t(word(0x338d08)-word(0x338d2c))+int(row*8)),
        float(int32_t(word(0x338d10)-word(0x338d30))+int(column*8)),u,v,color});
  }
  static std::vector<Call>* calls;calls=&expected;
  static size_t count;count=0;
  static PS2Runtime::RecompiledFunction original;original=sprites?runtime.lookupFunction(0x110b70):nullptr;
  runtime.replaceFunction(0x110b70,[](uint8_t* memory,R5900Context* ctx,PS2Runtime* state) {
    if(count>=calls->size())throw std::runtime_error("Text-grid emitted an extra sprite");
    const auto& e=(*calls)[count++];
    const auto reg=[&](unsigned index){return uint32_t(_mm_cvtsi128_si32(ctx->r[index]));};
    if(ctx->f[12]!=e.x || ctx->f[13]!=e.y || ctx->f[14]!=8 || ctx->f[15]!=8 || reg(5)!=e.u || reg(6)!=e.v || reg(7)!=e.color) {
      std::cerr<<"[ibara-text-grid] sprite="<<count<<" actual="<<ctx->f[12]<<','<<ctx->f[13]<<','<<ctx->f[14]<<','<<ctx->f[15]
        <<','<<reg(5)<<','<<reg(6)<<','<<reg(7)<<" expected="<<e.x<<','<<e.y<<",8,8,"<<e.u<<','<<e.v<<','<<e.color<<'\n';
      throw std::runtime_error("Original text-grid coordinate mismatch");
    }
    if(original) {
      const auto word=[&](uint32_t a){uint32_t v;std::memcpy(&v,memory+a,4);return v;};
      const auto object=word(0x3421b0+word(0x338afc)*4);
      original(memory,ctx,state);
      const auto coordinates=word(object+0x44);
      std::array<float,4> actual{};std::memcpy(actual.data(),memory+coordinates+16,16);
      const std::array<float,4> expected{float(e.v&255)/256.f,(256.f-float(e.u&255)-8.f)/256.f,
          (float(e.v&255)+8.f)/256.f,(256.f-float(e.u&255))/256.f};
      if(actual!=expected) {
        std::cerr<<"[ibara-text-uv] object="<<std::hex<<object<<" coordinates="<<coordinates<<std::dec<<" actual=";
        for(auto x:actual)std::cerr<<x<<',';std::cerr<<" expected=";for(auto x:expected)std::cerr<<x<<',';std::cerr<<'\n';
        throw std::runtime_error("Original sprite texture-coordinate mismatch");
      }
    }else ctx->pc=reg(31);
  });
  static bool completed;completed=false;
  runtime.replaceFunction(0x100010,[](uint8_t*,R5900Context*,PS2Runtime* state){completed=true;state->eeScheduler().requestStop();});
  R5900Context context{};context.pc=0x112670;
  context.r[4]=context.r[5]=_mm_cvtsi32_si128(7);
  context.r[28]=_mm_cvtsi32_si128(0x33fcf0);context.r[29]=_mm_cvtsi32_si128(0x1fff000);context.r[31]=_mm_cvtsi32_si128(0x100010);
  runtime.setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::Stop);
  runtime.eeScheduler().reset(ram,context);runtime.eeScheduler().run();
  if(!completed || count!=expected.size())throw std::runtime_error("Original text-grid probe did not complete");
  std::cout<<"Original Ibara text grid: "<<count<<" sprite coordinates passed\n";
  return 0;
}
#endif
#ifdef EVAC_PS2_ESPGALUDA_MODULES
int ProbeBootTransformCallbacks(const char* disc) {
  PS2Runtime runtime;
  if(!runtime.memory().initialize() ||
     !runtime.loadELF((std::filesystem::path(disc)/"boot.elf").string()))
    throw std::runtime_error("Cannot load original boot callback table");
  runtime.setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::Stop);
  constexpr uint32_t Table=0x2d5900,Gp=0x35fcf0,Stop=0x100010;
  const std::array<uint32_t,4> expected{0x1c28d0,0x1c28e0,0x1c29f0,0x1c2c00};
  auto* ram=runtime.memory().getRDRAM();
  for(unsigned i=0;i<12;++i) {
    uint32_t pc{};std::memcpy(&pc,ram+Table+i*4,4);
    if(!runtime.lookupFunction(pc) || (i<4 && pc!=expected[i]))
      throw std::runtime_error("Original boot callback table has an unlinked target");
  }
  bool completed=false;static bool* completion;completion=&completed;
  runtime.replaceFunction(Stop,[](uint8_t*,R5900Context*,PS2Runtime* state){
    *completion=true;state->eeScheduler().requestStop();
  });
  for(uint32_t pc=0x154e70;pc<=0x154ee0;pc+=16) {
    const auto callback=runtime.lookupFunction(pc);
    if(!callback)throw std::runtime_error("Original empty stage callback is not linked");
    R5900Context context{};context.pc=pc;context.r[31]=_mm_cvtsi32_si128(Stop);
    callback(ram,&context,&runtime);
    if(context.pc!=Stop)throw std::runtime_error("Original empty stage callback did not return");
  }
  for(unsigned kind=0;kind<4;++kind)for(uint16_t value:{uint16_t(0),uint16_t(1),uint16_t(0x7fff),uint16_t(0x8000),uint16_t(0xffff)}) {
    const uint16_t y=uint16_t(value+7),flags=0xa055;
    std::memcpy(ram+0x481108,&value,2);std::memcpy(ram+0x481100,&y,2);
    std::memcpy(ram+Gp-0x7ee4,&flags,2);
    R5900Context context{};context.pc=expected[kind];
    context.r[28]=_mm_cvtsi32_si128(Gp);context.r[31]=_mm_cvtsi32_si128(Stop);
    completed=false;runtime.eeScheduler().reset(ram,context);runtime.eeScheduler().run();
    uint16_t actualX{},actualY{},actualFlags{};
    std::memcpy(&actualX,ram+0x481108,2);std::memcpy(&actualY,ram+0x481100,2);
    std::memcpy(&actualFlags,ram+Gp-0x7ee4,2);
    const bool flipX=kind==1 || kind==2,flipY=kind==2 || kind==3;
    if(!completed || actualX!=uint16_t(flipX?-value:value) || actualY!=uint16_t(flipY?-y:y) ||
       (actualFlags&~0x1f00)!=(flags&~0x1f00))
      throw std::runtime_error("Original boot transform callback produced incorrect state");
  }
  std::cout<<"Boot callback tables: 12 transform entries linked, 4 bodies x 5 states, 8 empty stage callbacks: PASS\n";
  return 0;
}
int ProbeArcadeResetCallbacks(const char* disc) {
  struct Family {unsigned module;uint32_t first,base,slots,scratch,ports;PS2Runtime::RecompiledFunction* table;};
  const std::array families{
    Family{16,0x8bd8d0,g_ps2ArcadeFunctionTableBase,g_ps2ArcadeFunctionTableSlotCount,0x78d8,0xd05200,g_ps2ArcadeFunctionTable},
    Family{17,0x7d7fd0,g_ps2ArrangeFunctionTableBase,g_ps2ArrangeFunctionTableSlotCount,0x75e0,0x61f160,g_ps2ArrangeFunctionTable}};
  for(const auto& family:families) {
  PS2Runtime runtime;
  const auto elf=std::filesystem::path(disc)/("module-"+std::to_string(family.module))/"analysis.elf";
  if(!runtime.memory().initialize() ||
     !runtime.loadELF((std::filesystem::path(disc)/"boot.elf").string()) || !runtime.loadELF(elf.string()))
    throw std::runtime_error("Cannot load the original mode image");
  auto* ram=runtime.memory().getRDRAM();
  constexpr uint32_t Gp=0x35fcf0,Stop=0x100010;
  if(family.module==16 && !family.table[(0x733170-family.base)/4])
    throw std::runtime_error("Practice final-boss tail entry is not linked");
  // The original arcade-core reset callbacks differ only in the selected bank.
  // Execute every member, including the one reached by the final-boss script.
  for(unsigned bank=0;bank<4;++bank) {
    const uint32_t pc=family.first+bank*0x90;
    const auto slot=(pc-family.base)/4;
    if(pc<family.base || slot>=family.slots || !family.table[slot])
      throw std::runtime_error("Original ARCADE reset callback is not linked");
    for(uint16_t seed:{uint16_t(0),uint16_t(0xffff),uint16_t(0x1234)}) {
      R5900Context context{};context.pc=pc;
      context.r[28]=_mm_cvtsi32_si128(Gp);context.r[31]=_mm_cvtsi32_si128(Stop);
      std::memset(ram+Gp-family.scratch,0xa5,6);
      std::memset(ram+family.ports,0xa5,4);
      std::memcpy(ram+Gp-0x7ee4,&seed,2);
      family.table[slot](ram,&context,&runtime);
      uint16_t flags{};std::memcpy(&flags,ram+Gp-0x7ee4,2);
      if(context.pc!=Stop || ram[Gp-family.scratch+4]!=0 || ram[Gp-family.scratch+5]!=bank+2 ||
         ram[Gp-family.scratch]!=0 || ram[Gp-family.scratch+1]!=0 ||
         ram[family.ports]!=0 || ram[family.ports+1]!=0 || ram[family.ports+2]!=0 || ram[family.ports+3]!=0 ||
         flags!=uint16_t((seed&0xf0ff)|0x400)) {
        std::cerr<<"[ps2-reset-probe] module="<<family.module<<" bank="<<bank<<" seed="<<std::hex<<seed
          <<" pc="<<context.pc<<" flags="<<flags<<" scratch=";
        for(unsigned i=0;i<6;++i)std::cerr<<unsigned(ram[Gp-family.scratch+i])<<',';
        std::cerr<<" ports=";for(unsigned i=0;i<4;++i)std::cerr<<unsigned(ram[family.ports+i])<<',';std::cerr<<'\n';
        throw std::runtime_error("Original ARCADE callback produced unexpected state");
      }
    }
  }
  }
  std::cout<<"ARCADE/ARRANGE reset callbacks: 8 original bodies x 3 initial states: PASS\n";
  return 0;
}
#endif
#ifdef EVAC_PS2_IBARA
constexpr bool IsIbara=true;
constexpr const char* WindowTitle="EVAC PC LABEL | Ibara";
const cave::ps2::CollectionGame CollectionDefinition{"ibara","6e58b52aa5c03d45e413e8473be52a39205095e34a1bd58dae6bd852eb4fb1db",cave::ibara::DiscFiles};
#else
constexpr bool IsIbara=false;
constexpr const char* WindowTitle="EVAC PC LABEL | ESPGaluda";
const cave::ps2::CollectionGame CollectionDefinition{"espgaluda","d1547e943a26df22bbf5471e6cc89c57a2208b757d406ae0b41bf90bd3c51a0f",cave::espgaluda::DiscFiles};
#endif
int ProbeIopHeapLifecycle(const char* elf) {
  PS2Runtime runtime;
  if(!runtime.memory().initialize() || !runtime.loadELF(elf))
    throw std::runtime_error("Cannot initialize headless IOP heap probe");
  runtime.setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::Stop);
  constexpr uint32_t Stack=0x1ee9f90,Stop=0x100010,Bytes=0x40000;
  bool completed=false;
  static bool* completion;completion=&completed;
  runtime.replaceFunction(Stop,[](uint8_t*,R5900Context*,PS2Runtime* state){
    *completion=true;state->eeScheduler().requestStop();
  });
  const auto call=[&](uint32_t entry,uint32_t a0,uint32_t a1=0u) {
    completed=false;
    R5900Context context{};context.pc=entry;
    context.r[4]=_mm_cvtsi32_si128(a0);context.r[5]=_mm_cvtsi32_si128(a1);
    context.r[29]=_mm_cvtsi32_si128(Stack);context.r[31]=_mm_cvtsi32_si128(Stop);
    runtime.eeScheduler().reset(runtime.memory().getRDRAM(),context);
    runtime.eeScheduler().run();
    if(!completed)throw std::runtime_error("IOP heap probe did not return");
    return uint32_t(_mm_cvtsi128_si32(runtime.cpu().r[2]));
  };
  uint32_t first{};
  for(unsigned cycle=0;cycle<128;++cycle) {
    const auto address=(cycle&1)?call(0x10c048,0,Bytes):call(0x10bfd8,Bytes);
    if(!address || (first && address!=first))
      throw std::runtime_error("IOP sample-buffer allocation leaked across a sound-bank reload");
    first=address;
    if(call((cycle&1)?0x10c0c8:0x10c140,address)!=0)
      throw std::runtime_error("IOP sample-buffer release failed");
  }
  std::cout<<"ESPGaluda IOP heap: 128 original allocation/release cycles, 256 KiB reused: PASS\n";
  return 0;
}
int ProbeAdxCoefficients(const char* elf) {
  PS2Runtime runtime;
  if(!runtime.memory().initialize() || !runtime.loadELF(elf))
    throw std::runtime_error("Cannot initialize headless ADX coefficient probe");
  runtime.setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::Stop);
  constexpr uint32_t Destination=0x400000,Stack=0x1ee9f90,Stop=0x100010;
  bool completed=false,passed=true;
  static bool* completion;completion=&completed;
  runtime.replaceFunction(Stop,[](uint8_t*,R5900Context*,PS2Runtime* state){
    *completion=true;state->eeScheduler().requestStop();
  });
  auto* memory=runtime.memory().getRDRAM();
  std::cout<<"sample_rate,cutoff,coefficient1,coefficient2,expected1,expected2,passed\n";
  for(uint32_t rate:{22050u,44100u,48000u}) {
    completed=false;
    R5900Context context{};context.pc=0x27bb20;context.f[0]=100.0f;
    context.r[4]=_mm_cvtsi32_si128(500);context.r[5]=_mm_cvtsi32_si128(rate);
    context.r[6]=_mm_cvtsi32_si128(Destination);context.r[7]=_mm_cvtsi32_si128(Destination+2);
    context.r[29]=_mm_cvtsi32_si128(Stack);context.r[31]=_mm_cvtsi32_si128(Stop);
    runtime.eeScheduler().reset(memory,context);runtime.eeScheduler().run();
    int16_t actual[2]{};std::memcpy(actual,memory+Destination,sizeof(actual));
    const float root=std::sqrt(2.0f),a=root-std::cos(6.283185f*500.0f/float(rate)),b=root-1.0f;
    const float c=(a-std::sqrt(a*a-b*b))/b;
    const int expected1=int(c*8192.0f),expected2=int(-c*c*4096.0f);
    const bool ok=completed && std::abs(int(actual[0])-expected1)<=4 && std::abs(int(actual[1])-expected2)<=4;
    passed&=ok;
    std::cout<<rate<<",500,"<<actual[0]<<','<<actual[1]<<','<<expected1<<','<<expected2<<','<<ok<<'\n';
  }
  return passed?0:1;
}
int ProbeDiscIndex(const char* elf) {
  PS2Runtime runtime;
  if(!runtime.memory().initialize() || !runtime.loadELF(elf))
    throw std::runtime_error("Cannot initialize headless library probe");
  runtime.setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::Stop);
  constexpr uint32_t Destination=0x400000,Source=0x646980,Stack=0x1ee9f90;
  constexpr uint32_t Capacity=195,Stride=12,Stop=0x100010;
  bool completed=false;
  static bool* completion;
  static PS2Runtime::RecompiledFunction originalLength;
  static bool forceCheckpoint;
  completion=&completed;
  runtime.replaceFunction(Stop,[](uint8_t*,R5900Context*,PS2Runtime* state) {
    *completion=true;state->eeScheduler().requestStop();
  });
  originalLength=runtime.lookupFunction(0x11a000);
  runtime.replaceFunction(0x11a000,[](uint8_t* ram,R5900Context* context,PS2Runtime* state) {
    originalLength(ram,context,state);
    if(forceCheckpoint)state->eeScheduler().postEvent({EeEventType::Dmac,0,0});
  });
  auto* memory=runtime.memory().getRDRAM();
  for(bool forced:{false,true}) {
  forceCheckpoint=forced;
  for(unsigned count:{1u,10u,50u,130u}) {
    completed=false;
    std::string input;
    std::vector<std::string> expected;
    for(unsigned i=0;i<count;++i) {
      std::ostringstream line;line<<"A000\\A"<<std::uppercase<<std::hex;
      line.width(3);line.fill('0');line<<i<<".;1";
      expected.push_back(line.str());input+=line.str()+"\r\n";
    }
    std::memset(memory+Source,0,4096);
    std::memcpy(memory+Source,input.c_str(),input.size()+1);
    std::memset(memory+Destination-16,0xa5,4096+32);
    std::memset(memory+Destination,0,4096);
    std::memset(memory+0x2eef70,0,16);
    R5900Context context{};context.pc=0x28ca28;
    context.r[4]=_mm_cvtsi32_si128(Destination);
    context.r[5]=_mm_cvtsi32_si128(Source);
    context.r[6]=_mm_cvtsi32_si128(Capacity);
    context.r[7]=_mm_cvtsi32_si128(Stride);
    context.r[29]=_mm_cvtsi32_si128(Stack);
    context.r[31]=_mm_cvtsi32_si128(Stop);
    runtime.eeScheduler().reset(memory,context);
    runtime.eeScheduler().run();
    const auto& result=runtime.cpu();
    if(!completed || _mm_cvtsi128_si32(result.r[2])!=count ||
       _mm_cvtsi128_si32(result.r[29])!=Stack)
      throw std::runtime_error("Index probe failed to return with the expected row count and stack");
    for(unsigned i=0;i<count;++i) {
      const auto* actual=reinterpret_cast<const char*>(memory+Destination+Capacity*8+i*(Stride+1));
      if(std::memcmp(actual,expected[i].c_str(),expected[i].size()+1)!=0)
        throw std::runtime_error("Index probe row mismatch at "+std::to_string(i));
    }
    if(memory[Destination-1]!=0xa5 || memory[Destination+4096]!=0xa5)
      throw std::runtime_error("Index probe overwrote destination guards");
    std::cerr<<"[ps2-index-probe] forced="<<forced<<" rows="<<count<<" passed\n";
  }
  }
  return 0;
}
// Development-only history of the CRI remote output queues. Keep the last
// operations in memory and emit them only when live chunks overlap or escape
// their physical IOP buffer; normal audio rendering does not use this hook.
struct AudioRingWatch {
  static inline std::array<std::string,128> history{};
  static inline uint64_t written{};
  static inline bool failed=false;
  static uint32_t Word(const uint8_t* memory,uint32_t address) {
    uint32_t value{};std::memcpy(&value,memory+address,4);return value;
  }
  static void Record(uint8_t* memory,R5900Context* context,PS2Runtime* runtime,uint32_t entry) {
    const auto ring=uint32_t(_mm_cvtsi128_si32(context->r[4]));
    if(ring!=0x2fa338u && ring!=0x2fa368u)return;
    const auto channel=ring==0x2fa338u?0u:1u;
    const auto base=Word(memory,0x2f2b30u+channel*0x18u);
    const auto capacity=Word(memory,0x2f2b34u+channel*0x18u);
    std::ostringstream line;
    line<<"[ps2-ring] ms="<<cave::ps2::audio_trace::Milliseconds()<<" entry="<<std::hex<<entry
        <<" ra="<<_mm_cvtsi128_si32(context->r[31])<<" ring="<<ring
        <<" thread="<<std::dec<<runtime->eeScheduler().currentThreadId();
    for(unsigned reg=5;reg<8;++reg)line<<" a"<<reg-4<<'='<<std::hex<<_mm_cvtsi128_si32(context->r[reg]);
    const auto block=uint32_t(_mm_cvtsi128_si32(context->r[6]));
    if(entry==0x2928d0u && block<PS2_RAM_SIZE-8)
      line<<" block="<<Word(memory,block)<<','<<Word(memory,block+4);
    struct Chunk {uint32_t begin,end;};std::array<Chunk,64> chunks{};unsigned count=0;
    bool bad=false;
    for(unsigned queue=0;queue<4;++queue) {
      line<<" q"<<queue<<'=';
      auto node=Word(memory,ring+0x18+queue*4);
      for(unsigned depth=0;node && depth<16;++depth) {
        if(node>PS2_RAM_SIZE-16){bad=true;break;}
        const auto start=Word(memory,node+8),size=Word(memory,node+12);
        line<<node<<':'<<start<<'+'<<size<<',';
        if(size) {
          if(start<base || size>capacity || uint64_t(start)+size>uint64_t(base)+capacity)bad=true;
          const auto end=start+size;
          for(unsigned i=0;i<count;++i)if(start<chunks[i].end && chunks[i].begin<end)bad=true;
          chunks[count++]={start,end};
        }
        node=Word(memory,node);
        if(depth==15 && node)bad=true;
      }
    }
    history[written++%history.size()]=line.str();
    if(bad && !failed) {
      failed=true;
      std::cerr<<"[ps2-ring-invalid] live remote chunks overlap or exceed their buffer\n";
      for(uint64_t i=written>history.size()?written-history.size():0;i<written;++i)
        std::cerr<<history[i%history.size()]<<'\n';
      runtime->requestStop();
    }
  }
  template<uint32_t Address> struct Hook {
    static inline PS2Runtime::RecompiledFunction original{};
    static void Call(uint8_t* memory,R5900Context* context,PS2Runtime* runtime) {
      if(context->pc==Address)Record(memory,context,runtime,Address);
      original(memory,context,runtime);
    }
    static void Bind(PS2Runtime& runtime) {
      original=runtime.lookupFunction(Address);
      if(!original || !runtime.replaceFunction(Address,&Call))
        throw std::runtime_error("Cannot attach CRI remote queue diagnostic");
    }
  };
  static void Bind(PS2Runtime& runtime) {
    Hook<0x292798u>::Bind(runtime);Hook<0x2928d0u>::Bind(runtime);Hook<0x292720u>::Bind(runtime);
  }
};
struct MusicCycle {
  static inline PS2Runtime::RecompiledFunction original{};
  static inline std::chrono::steady_clock::time_point started{};
  static inline unsigned next{};
  static inline bool stopQueued=false;
  static inline bool cycleEnabled=false;
  static inline int64_t lastSnapshot=-2000;
  static void Snapshot(uint8_t* memory,const char* phase) {
    std::ostringstream line;
    line<<"[ps2-music-cycle] ms="<<cave::ps2::audio_trace::Milliseconds()<<" phase="<<phase;
    for(const uint32_t address:{0x5505d0u,0x51e6c0u,0x2e3018u,0x2eedf8u,0x2f4f20u,0x2ebf70u}) {
      line<<" memory=0x"<<std::hex<<address<<':';
      const unsigned words=address==0x2e3018u?49u:16u;
      for(unsigned i=0;i<words;++i) {
        uint32_t value{};std::memcpy(&value,memory+address+i*4,4);
        line<<value<<',';
      }
    }
    line<<std::dec<<'\n';
    std::cerr<<line.str();
  }
  static void Call(uint8_t* memory,R5900Context* context,PS2Runtime* runtime) {
    // Queue ordinary requests at the EE sound-dispatch boundary. The original
    // game performs stop, cleanup, file opening and decoder startup.
    // This diagnostic sends no Windows input and replaces no audio routine.
    if(context->pc==0x12d830u) {
      const auto now=std::chrono::steady_clock::now();
      if(started==std::chrono::steady_clock::time_point{})started=now;
      const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(now-started).count();
      if(elapsed-lastSnapshot>=2000 && elapsed<600000) {
        lastSnapshot=elapsed;Snapshot(memory,"watch");
      }
      constexpr unsigned TrackCount=20;
      const uint32_t track=(next&1u)?24u:18u;
      if(cycleEnabled && next>0 && next<TrackCount && !stopQueued && elapsed>=13000+int64_t(next)*4709) {
        Snapshot(memory,"fade");
        // Exercise the actual stage->boss fade, not only its final stop.
        // 220 / 4 takes 55 fields; allow cleanup before the next request.
        memory[0x5505d0]|=0x10u;
        stopQueued=true;
      }
      if(cycleEnabled && next<TrackCount && elapsed>=15000+int64_t(next)*4709) {
        Snapshot(memory,"before");
        auto* queue=memory+0x5505d0;
        queue[0]=0xc0u;
        queue[1]=1;
        const uint16_t volume=220;
        const uint32_t command=0x09800000u|track;
        std::memcpy(queue+2,&volume,2);
        std::memcpy(queue+8,&command,4);
        std::cerr<<"[ps2-music-cycle] request="<<track<<" step="<<next
                 <<" ms="<<cave::ps2::audio_trace::Milliseconds()<<'\n';
        ++next;
        stopQueued=false;
      }
    }
    original(memory,context,runtime);
  }
  static void Bind(PS2Runtime& runtime,bool cycle) {
    cycleEnabled=cycle;
    original=runtime.lookupFunction(0x12d830u);
    if(!original || !runtime.replaceFunction(0x12d830u,&Call))
      throw std::runtime_error("Cannot attach original music lifecycle diagnostic");
  }
};

template<uint32_t Address> struct Observe {
  static inline PS2Runtime::RecompiledFunction original{};
  static inline uint64_t calls{};
  static inline uint32_t lastState=~0u;
  static void Call(uint8_t* memory,R5900Context* context,PS2Runtime* runtime) {
    const auto argument=static_cast<uint32_t>(_mm_cvtsi128_si32(context->r[4]));
    const auto physical=argument&0x1fffffffu;
    uint32_t state=~0u;
    if(physical<PS2_RAM_SIZE-48)std::memcpy(&state,memory+physical,4);
    const auto source=static_cast<uint32_t>(_mm_cvtsi128_si32(context->r[5]));
    const bool indexCopy=Address==0x118048u && source>=0x646980 && source<0x647980;
    const bool soundEntry=Address==0x124a40u || Address==0x124dc0u || Address==0x124ff0u ||
      Address==0x285ab0u || Address==0x27f3e0u || Address==0x27d6e0u || Address==0x27d738u;
    const bool log=++calls<=((soundEntry || Address==0x27e6c0u || Address==0x107be8u || Address==0x1043a0u || Address==0x127480u)?128u:4u) || (Address==0x27f310u && state!=lastState);
    lastState=state;
    if(log) {
      std::ostringstream line;
      line<<"[ps2-observe] ms="<<cave::ps2::audio_trace::Milliseconds()<<" entry=0x"<<std::hex<<Address<<" pc=0x"<<context->pc
               <<" ra=0x"<<_mm_cvtsi128_si32(context->r[31])
               <<" sp=0x"<<_mm_cvtsi128_si32(context->r[29]);
      for(unsigned reg=4;reg<=11;++reg)line<<" a"<<(reg-4)<<"=0x"<<_mm_cvtsi128_si32(context->r[reg]);
      if constexpr(IsIbara && (Address==0x1be620u || Address==0x1aa6d0u)) {
        const auto path=uint32_t(_mm_cvtsi128_si32(context->r[Address==0x1be620u?6:5]))&0x1fffffffu;
        line<<" source=";
        for(unsigned i=0;path+i<PS2_RAM_SIZE && i<128 && memory[path+i];++i)
          line<<char(memory[path+i]>=32 && memory[path+i]<127?memory[path+i]:'?');
      }
      if(indexCopy) {
        line<<" sp=0x"<<_mm_cvtsi128_si32(context->r[29]);
        for(unsigned reg=16;reg<=23;++reg)line<<" s"<<(reg-16)<<"=0x"<<_mm_cvtsi128_si32(context->r[reg]);
      }
      if constexpr(Address==0x1043a0u || Address==0x127480u) {
        uint32_t heapBreak{};std::memcpy(&heapBreak,memory+0x29544c,4);
        line<<" break=0x"<<heapBreak<<" nativeEnd=0x"<<runtime->guestHeapEnd()
                 <<" limit=0x"<<runtime->guestHeapLimit();
      }
      if(physical<PS2_RAM_SIZE-48) {
        line<<" object=";
        for(unsigned i=0;i<12;++i) {
          uint32_t word{};std::memcpy(&word,memory+physical+i*4,4);
          line<<word<<',';
        }
      }
      if constexpr(Address==0x27e6c0u || Address==0x27d6e0u || Address==0x27d738u) {
        if(physical<PS2_RAM_SIZE-256) {
          line<<" path=";
          for(unsigned i=0;i<256 && memory[physical+i];++i) {
            const char value=char(memory[physical+i]);
            line<<(value>=32 && value<127?value:'?');
          }
        }
      }
      if constexpr(Address==0x11d530u) {
        for(uint32_t address:{0x2e03f0u,0x2e3c64u,0x2e3d6cu,0x2e3d70u,
                             0x648da0u,0x648da8u,0x2fbc90u,0x2e3008u}) {
          uint32_t word{};std::memcpy(&word,memory+address,4);
          line<<" mem["<<address<<"]="<<word;
        }
      }
      if constexpr(Address==0x124600u) {
        for(uint32_t address:{0x649800u,0x730800u,0x83ffd0u}) {
          line<<" region["<<address<<"]=";
          for(unsigned i=0;i<8;++i) {
            uint32_t word{};std::memcpy(&word,memory+address+i*4,4);
            line<<word<<',';
          }
        }
        line<<" compiledTarget="<<runtime->hasFunction(0x83ffd0u);
      }
      line<<std::dec<<'\n';
      std::cerr<<line.str();
    }
    if(indexCopy && (argument<0x100000 || _mm_cvtsi128_si32(context->r[6])>13))
      throw std::runtime_error("Invalid disc index copy observed before memory overwrite");
    original(memory,context,runtime);
    if(log)std::cerr<<"[ps2-observe] leave=0x"<<std::hex<<Address<<" v0=0x"
                    <<_mm_cvtsi128_si32(context->r[2])<<" pc=0x"<<context->pc<<std::dec<<'\n';
  }
  static void Bind(PS2Runtime& runtime) {
    original=runtime.lookupFunction(Address);
    if(!original || !runtime.replaceFunction(Address,&Call))
      throw std::runtime_error("Cannot attach development observer");
  }
};
}

int main(int argc,char** argv) {
  std::filesystem::path frameTimes;
  std::unique_ptr<cave::ps2::CollectionLaunch> collection;
  try {
    collection=cave::ps2::CollectionLaunch::Parse(argc,argv,CollectionDefinition);
#ifdef EVAC_PS2_IBARA
    if(argc==4 && (std::string_view(argv[1])=="--probe-text-grid" || std::string_view(argv[1])=="--probe-text-sprites"))
      return ProbeIbaraTextGrid(argv[2],argv[3],std::string_view(argv[1])=="--probe-text-sprites");
#endif
#ifdef EVAC_PS2_ESPGALUDA_MODULES
    if(argc==3 && std::string_view(argv[1])=="--probe-boot-transform-callbacks")
      return ProbeBootTransformCallbacks(argv[2]);
    if(argc==3 && std::string_view(argv[1])=="--probe-arcade-reset-callbacks")
      return ProbeArcadeResetCallbacks(argv[2]);
#endif
    if(argc==3 && std::string_view(argv[1])=="--probe-iop-heap") {
      if(IsIbara)throw std::runtime_error("The IOP heap entry probe requires the ESPGaluda host");
      return ProbeIopHeapLifecycle(argv[2]);
    }
    if(argc==3 && std::string_view(argv[1])=="--probe-adx-coefficients") {
      if(IsIbara)throw std::runtime_error("The ADX coefficient probe requires the ESPGaluda host");
      return ProbeAdxCoefficients(argv[2]);
    }
    if(argc==3 && std::string_view(argv[1])=="--probe-disc-index") {
      if(IsIbara)throw std::runtime_error("The disc-index probe requires the ESPGaluda host");
      return ProbeDiscIndex(argv[2]);
    }
    if(!collection && argc<5)throw std::runtime_error("Expected: boot.elf disc.iso extracted-disc-root test-profile [--trace-boot] [--frames count]");
    bool traceBoot=false,traceAudio=false,musicCycle=false;
    uint64_t frameLimit{};
    uint32_t diagnosticTapMs{};
    size_t inputTraceEvents=200000,audioCaptureSeconds=90;
    std::filesystem::path audioCapture,inputTrace;
    cave::ps2::NativeHostOptions hostOptions;
    if(collection){hostOptions=collection->HostOptions();frameTimes=collection->FrameTimes();}
    for(int i=collection?argc:5;i<argc;++i) {
      const std::string_view option=argv[i];
      if(option=="--trace-boot")traceBoot=true;
      else if(option=="--trace-audio")traceAudio=true;
      else if(option=="--music-cycle")musicCycle=traceAudio=true;
      else if(option=="--capture-audio" && i+1<argc)audioCapture=std::filesystem::absolute(argv[++i]);
      else if(option=="--capture-audio-seconds" && i+1<argc) {
        audioCaptureSeconds=std::stoull(argv[++i]);
        if(audioCaptureSeconds<1 || audioCaptureSeconds>600)throw std::runtime_error("Audio capture duration must be 1..600 seconds");
      }
      else if(option=="--input-trace" && i+1<argc)inputTrace=std::filesystem::absolute(argv[++i]);
      else if(option=="--input-trace-events" && i+1<argc) {
        inputTraceEvents=std::stoull(argv[++i]);
        if(!inputTraceEvents || inputTraceEvents>1000000)throw std::runtime_error("Input trace events must be 1..1000000");
      }
      else if(option=="--coarse-timer")hostOptions.timerPrecision=false;
      else if(option=="--frame-times" && i+1<argc) {
        frameTimes=std::filesystem::absolute(argv[++i]);
        if(std::filesystem::exists(frameTimes))throw std::runtime_error("Frame timing output already exists");
      }
      else if(option=="--frames" && i+1<argc) {
        frameLimit=std::stoull(argv[++i]);
        if(frameLimit<1 || frameLimit>36000)throw std::runtime_error("Frame limit must be 1..36000");
      } else if(option=="--diagnostic-tap-ms" && i+1<argc) {
        const auto value=std::stoul(argv[++i]);
        if(value>500)throw std::runtime_error("Diagnostic tap duration must be 0..500 ms");
        diagnosticTapMs=static_cast<uint32_t>(value);
      } else if(option=="--fullscreen")hostOptions.fullscreen=true;
      else if(option=="--vsync")hostOptions.vsync=true;
      else if(option=="--config" && i+1<argc)hostOptions.keyboardConfig=std::filesystem::absolute(argv[++i]);
      else if(option=="--capture-gs" && i+1<argc)hostOptions.capture=std::filesystem::absolute(argv[++i]);
      else if(option=="--capture-gs-mib" && i+1<argc) {
        const auto mib=std::stoull(argv[++i]);
        if(mib<8 || mib>4096)throw std::runtime_error("GS capture limit must be 8..4096 MiB");
        hostOptions.captureBytes=mib*1024ull*1024u;
      }
      else if(option=="--window-size" && i+2<argc) {
        hostOptions.width=std::stoul(argv[++i]);hostOptions.height=std::stoul(argv[++i]);
      } else throw std::runtime_error("Unknown development option");
    }
    const auto elf=collection?collection->Elf():std::filesystem::canonical(argv[1]);
    const auto iso=collection?collection->Disc():std::filesystem::canonical(argv[2]);
    const auto data=collection?collection->Data():std::filesystem::canonical(argv[3]);
    const auto profile=collection?collection->Profile():std::filesystem::absolute(argv[4]);
    if(!std::filesystem::is_regular_file(iso) || !std::filesystem::is_directory(data))
      throw std::runtime_error("Expected an ISO and an independent extracted-disc directory");
    std::filesystem::create_directories(profile/"mc0");
    cave::ps2::ConfigureDiagnosticInput(diagnosticTapMs);
    cave::ps2::input_trace::Scope inputTraceScope(inputTrace,inputTraceEvents);
    cave::ps2::audio_trace::Configure(traceAudio);
    cave::ps2::frame_trace::Configure(!frameTimes.empty());
    cave::ps2::audio_capture::Configure(audioCapture,audioCaptureSeconds);
    // IOP services may initialize memory-card roots during construction.
    PS2Runtime::IoPaths configured;
    configured.elfPath=elf;configured.elfDirectory=elf.parent_path();
    configured.hostRoot=data;configured.cdRoot=data;configured.cdImage=iso;configured.mcRoot=profile/"mc0";
    PS2Runtime::setIoPaths(configured);
    PS2Runtime runtime;
    // Ibara switches DISPFB in response to the start-of-blank interrupt.
    // Latch the buffer it selected for the upcoming visible field.
    runtime.eeScheduler().setScanoutAfterDisplayFlip(CollectionDefinition.id=="ibara");
    runtime.nativeHost().Configure(std::move(hostOptions));
    struct Progress {
      uint64_t frames{},limit{};
      std::chrono::steady_clock::time_point started{};
      bool trace=false;
    } progress{0,frameLimit};
    progress.trace=traceBoot;
    runtime.setDebugUiCallbacks([](PS2Runtime&,void*){},
      [](PS2Runtime& state,void* user) {
        auto& progress=*static_cast<Progress*>(user);
        const auto count=++progress.frames;
        const auto now=std::chrono::steady_clock::now();
        if(count==1)progress.started=now;
        if(progress.limit && count>=progress.limit)state.requestStop();
        if(count%120 || !progress.trace)return;
        // snapshot() owns its synchronization; do not read the live EE context
        // concurrently from the host rendering thread.
        const auto snapshot=state.eeScheduler().snapshot();
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now()-progress.started).count();
        std::cerr<<"[ps2-probe] frame="<<count<<" elapsed_ms="<<elapsed
                 <<" vsync="<<state.memory().gs().vsyncTick.load(std::memory_order_acquire)
                 <<" sequence="<<snapshot.sequence
                 <<" cycle="<<snapshot.eeCycle<<" running="<<snapshot.runningThreadId;
        for(const auto& thread:snapshot.threads)
          std::cerr<<" thread="<<thread.id<<" pc=0x"<<std::hex<<thread.pc
                   <<" ra=0x"<<thread.ra<<" sp=0x"<<thread.sp<<std::dec<<" status="<<int(thread.status)
                   <<" wait="<<int(thread.waitReason)<<" waitId="<<thread.waitId
                   <<" invocations="<<thread.invocationDepth;
        std::cerr<<'\n';
        const auto iop=state.iopDebugSnapshot();
        std::cerr<<"[ps2-iop] cycles="<<iop.emulatorCycles<<" instructions="<<iop.emulatorInstructions
                 <<" modules="<<iop.emulatorLoadedModules<<" threads="<<iop.emulatorThreads
                 <<" rpcServers="<<iop.emulatorRpcServers<<'\n';
        for(const auto& diagnostic:iop.diagnostics)std::cerr<<"[ps2-iop] "<<diagnostic<<'\n';
      },[](PS2Runtime&,void*){},&progress);
    runtime.setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::Stop);
    if(!runtime.initialize(WindowTitle))throw std::runtime_error("PS2 host initialization failed");
    if(!runtime.loadELF(elf.string()))throw std::runtime_error("Boot ELF loading failed");
#ifdef EVAC_PS2_IBARA
    if (!std::getenv("EVAC_PS2_TEST_ORIGINAL_PAD_TIMING"))
      cave::ibara::InputTiming::Bind(runtime);
#endif
    auto paths=PS2Runtime::getIoPaths();
    paths.elfPath=elf;paths.elfDirectory=elf.parent_path();
    paths.hostRoot=data;paths.cdRoot=data;paths.cdImage=iso;paths.mcRoot=profile/"mc0";
    PS2Runtime::setIoPaths(paths);
    if(musicCycle) {
      if(IsIbara)throw std::runtime_error("The original music lifecycle diagnostic requires the ESPGaluda host");
      AudioRingWatch::Bind(runtime);
    }
    if(traceAudio && !IsIbara)MusicCycle::Bind(runtime,musicCycle);
    if(traceBoot && IsIbara) {
      Observe<0x1be620u>::Bind(runtime);
      Observe<0x148b80u>::Bind(runtime);
      Observe<0x148be0u>::Bind(runtime);
      Observe<0x1bd230u>::Bind(runtime);
      Observe<0x1aaa70u>::Bind(runtime);
      Observe<0x1aaa90u>::Bind(runtime);
      Observe<0x1aa6d0u>::Bind(runtime);
      Observe<0x1bcd20u>::Bind(runtime);
    }
    if(traceBoot && !IsIbara) {
      Observe<0x27e6c0u>::Bind(runtime);
      Observe<0x27ed68u>::Bind(runtime);
      Observe<0x27f310u>::Bind(runtime);
      Observe<0x102718u>::Bind(runtime);
      Observe<0x107be8u>::Bind(runtime);
      Observe<0x11d530u>::Bind(runtime);
      Observe<0x280f70u>::Bind(runtime);
      Observe<0x103aa0u>::Bind(runtime);
      Observe<0x103ba0u>::Bind(runtime);
      Observe<0x2806c8u>::Bind(runtime);
      Observe<0x118048u>::Bind(runtime);
      Observe<0x2805c0u>::Bind(runtime);
      Observe<0x2800c8u>::Bind(runtime);
      Observe<0x280edcu>::Bind(runtime);
      Observe<0x1043a0u>::Bind(runtime);
      Observe<0x127480u>::Bind(runtime);
      Observe<0x124600u>::Bind(runtime);
    }
    if(traceAudio && !traceBoot && !IsIbara)Observe<0x27e6c0u>::Bind(runtime);
    if(traceAudio && !IsIbara) {
      Observe<0x124a40u>::Bind(runtime);
      Observe<0x124dc0u>::Bind(runtime);
      Observe<0x124ff0u>::Bind(runtime);
      Observe<0x285ab0u>::Bind(runtime);
      Observe<0x27f3e0u>::Bind(runtime);
      Observe<0x27d6e0u>::Bind(runtime);
      Observe<0x27d738u>::Bind(runtime);
    }
    runtime.run();
    cave::ps2::audio_capture::Save();
    if(!frameTimes.empty())cave::ps2::frame_trace::Save(frameTimes);
    if(collection)collection->Complete(0);
    return 0;
  }catch(const std::exception& error) {
    try{cave::ps2::audio_capture::Save();}catch(...){}
    try{if(!frameTimes.empty())cave::ps2::frame_trace::Save(frameTimes);}catch(...){}
    try{if(collection)collection->Complete(1);}catch(...){}
    std::cerr<<"PS2 development host: "<<error.what()<<'\n';return 1;
  }
}
