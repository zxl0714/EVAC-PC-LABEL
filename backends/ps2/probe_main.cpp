// Internal static-recompilation bring-up host. The player-facing launch contract
// is connected only after ISO, input, graphics and save behaviour are verified.
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include <filesystem>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
template<uint32_t Address> struct Observe {
  static inline PS2Runtime::RecompiledFunction original{};
  static inline uint64_t calls{};
  static inline uint32_t lastState=~0u;
  static void Call(uint8_t* memory,R5900Context* context,PS2Runtime* runtime) {
    const auto argument=static_cast<uint32_t>(_mm_cvtsi128_si32(context->r[4]));
    const auto physical=argument&0x1fffffffu;
    uint32_t state=~0u;
    if(physical<PS2_RAM_SIZE-48)std::memcpy(&state,memory+physical,4);
    const bool log=++calls<=(Address==0x107be8u?128u:4u) || (Address==0x27f310u && state!=lastState);
    lastState=state;
    if(log) {
      std::cerr<<"[ps2-observe] entry=0x"<<std::hex<<Address<<" pc=0x"<<context->pc
               <<" ra=0x"<<_mm_cvtsi128_si32(context->r[31]);
      for(unsigned reg=4;reg<=11;++reg)std::cerr<<" a"<<(reg-4)<<"=0x"<<_mm_cvtsi128_si32(context->r[reg]);
      if(physical<PS2_RAM_SIZE-48) {
        std::cerr<<" object=";
        for(unsigned i=0;i<12;++i) {
          uint32_t word{};std::memcpy(&word,memory+physical+i*4,4);
          std::cerr<<word<<',';
        }
      }
      if constexpr(Address==0x11d530u) {
        for(uint32_t address:{0x2e03f0u,0x2e3c64u,0x2e3d6cu,0x2e3d70u,
                             0x648da0u,0x648da8u,0x2fbc90u,0x2e3008u}) {
          uint32_t word{};std::memcpy(&word,memory+address,4);
          std::cerr<<" mem["<<address<<"]="<<word;
        }
      }
      std::cerr<<std::dec<<'\n';
    }
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
    if(argc!=5 && argc!=6)throw std::runtime_error("Expected: boot.elf disc.iso extracted-disc-root test-profile [--trace-boot]");
    const bool traceBoot=argc==6 && std::string_view(argv[5])=="--trace-boot";
    if(argc==6 && !traceBoot)throw std::runtime_error("Unknown development option");
    const auto elf=std::filesystem::canonical(argv[1]);
    const auto iso=std::filesystem::canonical(argv[2]);
    const auto data=std::filesystem::canonical(argv[3]);
    const auto profile=std::filesystem::absolute(argv[4]);
    if(!std::filesystem::is_regular_file(iso) || !std::filesystem::is_directory(data))
      throw std::runtime_error("Expected an ISO and an independent extracted-disc directory");
    std::filesystem::create_directories(profile/"mc0");
    PS2Runtime runtime;
    uint64_t frames{};
    runtime.setDebugUiCallbacks([](PS2Runtime&,void*){},
      [](PS2Runtime& state,void* user) {
        auto& count=*static_cast<uint64_t*>(user);
        if(++count%120)return;
        // snapshot() owns its synchronization; do not read the live EE context
        // concurrently from the host rendering thread.
        const auto snapshot=state.eeScheduler().snapshot();
        std::cerr<<"[ps2-probe] frame="<<count<<" sequence="<<snapshot.sequence
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
      },[](PS2Runtime&,void*){},&frames);
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
    }
    runtime.run();
    return 0;
  }catch(const std::exception& error) {
    std::cerr<<"PS2 development host: "<<error.what()<<'\n';return 1;
  }
}
