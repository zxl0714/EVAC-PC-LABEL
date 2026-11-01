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
#include "diagnostic_input.h"
#include "audio_trace.h"

namespace {
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
    const bool log=++calls<=((Address==0x107be8u || Address==0x1043a0u || Address==0x127480u)?128u:4u) || (Address==0x27f310u && state!=lastState);
    lastState=state;
    if(log) {
      std::ostringstream line;
      line<<"[ps2-observe] entry=0x"<<std::hex<<Address<<" pc=0x"<<context->pc
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
  try {
    if(argc==3 && std::string_view(argv[1])=="--probe-disc-index")return ProbeDiscIndex(argv[2]);
    if(argc<5)throw std::runtime_error("Expected: boot.elf disc.iso extracted-disc-root test-profile [--trace-boot] [--frames count]");
    bool traceBoot=false,traceAudio=false;
    uint64_t frameLimit{};
    uint32_t diagnosticTapMs{};
    cave::ps2::NativeHostOptions hostOptions;
    for(int i=5;i<argc;++i) {
      const std::string_view option=argv[i];
      if(option=="--trace-boot")traceBoot=true;
      else if(option=="--trace-audio")traceAudio=true;
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
    PS2Runtime runtime;
    runtime.nativeHost().Configure(std::move(hostOptions));
    struct Progress {uint64_t frames{},limit{};std::chrono::steady_clock::time_point started{};} progress{0,frameLimit};
    runtime.setDebugUiCallbacks([](PS2Runtime&,void*){},
      [](PS2Runtime& state,void* user) {
        auto& progress=*static_cast<Progress*>(user);
        const auto count=++progress.frames;
        if(count==1)progress.started=std::chrono::steady_clock::now();
        if(progress.limit && count>=progress.limit)state.requestStop();
        if(count%120)return;
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
    if(!runtime.initialize("EVAC PC LABEL | PS2 development"))throw std::runtime_error("PS2 host initialization failed");
    if(!runtime.loadELF(elf.string()))throw std::runtime_error("Boot ELF loading failed");
    auto paths=PS2Runtime::getIoPaths();
    paths.elfPath=elf;paths.elfDirectory=elf.parent_path();
    paths.hostRoot=data;paths.cdRoot=data;paths.cdImage=iso;paths.mcRoot=profile/"mc0";
    PS2Runtime::setIoPaths(paths);
    if(traceBoot) {
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
    runtime.run();
    return 0;
  }catch(const std::exception& error) {
    std::cerr<<"PS2 development host: "<<error.what()<<'\n';return 1;
  }
}
