// Reuse the original-library ABI checks and private generated inputs. No game
// instructions or resources are embedded in this authored benchmark source.
#define main RunOriginalLibraryContract
#include "../../tests/ps2_libc_probe.cpp"
#undef main
#include <iomanip>
#include <string_view>

int main(int argc,char** argv) {
  try {
    unsigned batches=2000;
    if(argc==3 && std::string_view(argv[1])=="--batches")batches=std::stoul(argv[2]);
    else if(argc!=1)throw std::invalid_argument("Usage: ps2_library_bench [--batches 2000]");
    if(!batches || batches>10000)throw std::invalid_argument("Batches must be 1..10000");
    if(RunOriginalLibraryContract())throw std::runtime_error("Original-library ABI contract failed");
    PS2Runtime runtime;
    if(!runtime.memory().initialize())throw std::runtime_error("Cannot initialize benchmark RAM");
    auto* memory=runtime.memory().getRDRAM();
    constexpr uint32_t Source=0x400000,Destination=0x500000,Stack=0x1ee9f90;
    constexpr unsigned callsPerBatch=16;
    std::array<R5900Context,callsPerBatch> contexts{};
    std::cout<<std::fixed<<std::setprecision(6);
    for(unsigned length:{64u,256u,1024u,4096u})for(bool aligned:{true,false}) {
      const auto source=Source+(aligned?0u:3u),destination=Destination+(aligned?0u:7u);
      for(unsigned i=0;i<length;++i)memory[source+i]=uint8_t((i*37u+17u)&255u);
      std::memset(memory+Destination-16,0xa5,length+64);
      uint64_t elapsed=0,checksum=0;
      for(unsigned batch=0;batch<batches;++batch) {
        for(auto& context:contexts) {
          context={};context.pc=libraries[0].begin;
          SET_GPR_U32(&context,4,destination);SET_GPR_U32(&context,5,source);
          SET_GPR_U32(&context,6,length);SET_GPR_U32(&context,29,Stack);
        }
        // Reset deadlines outside the timed region. Each batch stays below
        // one virtual field; no window, wall-clock pacing or firmware is used.
        runtime.eeScheduler().reset(memory,contexts[0]);
        const auto start=std::chrono::steady_clock::now();
        for(auto& context:contexts)Execute(memory,context,runtime);
        elapsed+=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-start).count();
        for(const auto& context:contexts) {
          if(getRegU32(&context,2)!=destination || getRegU32(&context,29)!=Stack)
            throw std::runtime_error("Memcpy benchmark changed its ABI result");
          checksum+=getRegU32(&context,2);
        }
      }
      if(std::memcmp(memory+source,memory+destination,length) ||
          memory[destination-1]!=0xa5 || memory[destination+length]!=0xa5)
        throw std::runtime_error("Memcpy benchmark changed bytes or wrote past its range");
      const auto calls=uint64_t(batches)*callsPerBatch;
      std::cout<<"{\"benchmark\":\"original-ee-memcpy\",\"length\":"<<length
        <<",\"aligned\":"<<(aligned?"true":"false")<<",\"batches\":"<<batches
        <<",\"calls\":"<<calls<<",\"timed_ns\":"<<elapsed
        <<",\"ns_per_call\":"<<double(elapsed)/calls
        <<",\"ns_per_byte\":"<<double(elapsed)/calls/length
        <<",\"checksum\":"<<checksum<<"}\n";
    }
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
