#include "native_host.h"
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
#include <chrono>
#include <cmath>
#include "frame_trace.h"
#include "audio_capture.h"
#include "diagnostic_input.h"
#include "audio_trace.h"

namespace {
#ifdef EVAC_PS2_IBARA
constexpr bool IsIbara=true;
constexpr const char* WindowTitle="EVAC PC LABEL | Ibara";
#else
constexpr bool IsIbara=false;
constexpr const char* WindowTitle="EVAC PC LABEL | ESPGaluda";
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
  try {
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
    if(argc<5)throw std::runtime_error("Expected: boot.elf disc.iso extracted-disc-root test-profile [--trace-boot] [--frames count]");
    bool traceBoot=false,traceAudio=false,musicCycle=false;
    uint64_t frameLimit{};
    uint32_t diagnosticTapMs{};
    std::filesystem::path audioCapture;
    cave::ps2::NativeHostOptions hostOptions;
    for(int i=5;i<argc;++i) {
      const std::string_view option=argv[i];
      if(option=="--trace-boot")traceBoot=true;
      else if(option=="--trace-audio")traceAudio=true;
      else if(option=="--music-cycle")musicCycle=traceAudio=true;
      else if(option=="--capture-audio" && i+1<argc)audioCapture=std::filesystem::absolute(argv[++i]);
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
      else if(option=="--window-size" && i+2<argc) {
        hostOptions.width=std::stoul(argv[++i]);hostOptions.height=std::stoul(argv[++i]);
      } else throw std::runtime_error("Unknown development option");
    }
    const auto elf=std::filesystem::canonical(argv[1]);
    const auto iso=std::filesystem::canonical(argv[2]);
    const auto data=std::filesystem::canonical(argv[3]);
    const auto profile=std::filesystem::absolute(argv[4]);
    if(!std::filesystem::is_regular_file(iso) || !std::filesystem::is_directory(data))
      throw std::runtime_error("Expected an ISO and an independent extracted-disc directory");
    std::filesystem::create_directories(profile/"mc0");
    cave::ps2::ConfigureDiagnosticInput(diagnosticTapMs);
    cave::ps2::audio_trace::Configure(traceAudio);
    cave::ps2::frame_trace::Configure(!frameTimes.empty());
    cave::ps2::audio_capture::Configure(audioCapture);
    PS2Runtime runtime;
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
                   <<" ra=0x"<<thread.ra<<std::dec<<" status="<<int(thread.status)
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
    auto paths=PS2Runtime::getIoPaths();
    paths.elfPath=elf;paths.elfDirectory=elf.parent_path();
    paths.hostRoot=data;paths.cdRoot=data;paths.cdImage=iso;paths.mcRoot=profile/"mc0";
    PS2Runtime::setIoPaths(paths);
    if(musicCycle) {
      if(IsIbara)throw std::runtime_error("The original music lifecycle diagnostic requires the ESPGaluda host");
      AudioRingWatch::Bind(runtime);
    }
    if(traceAudio && !IsIbara)MusicCycle::Bind(runtime,musicCycle);
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
    return 0;
  }catch(const std::exception& error) {
    try{cave::ps2::audio_capture::Save();}catch(...){}
    try{if(!frameTimes.empty())cave::ps2::frame_trace::Save(frameTimes);}catch(...){}
    std::cerr<<"PS2 development host: "<<error.what()<<'\n';return 1;
  }
}
