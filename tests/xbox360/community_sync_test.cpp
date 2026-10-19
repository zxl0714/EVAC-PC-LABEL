// Authored Xbox ABI fixtures, without ROM, HTTP, GPU, audio or input.
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/community_service.h>
#include <rex/system/xenumerator.h>
#include <rex/system/xio.h>
#include <rex/system/xthread.h>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace rex::system;
using namespace rex::system::xam;
using namespace std::chrono_literals;
namespace {
void Check(bool ok,const char* text) {if(!ok)throw std::runtime_error(text);}
class Provider final : public CommunityService {
 public:
  uint32_t error=0;
  bool hold=false;
  std::mutex mutex;
  std::condition_variable wake;
  std::vector<RankCompletion> held;
  void Read(CommunityRead request,ReadCompletion done) override {
    CommunityResult result{error,{}};
    for(const auto& spec:request.specs)
      result.views.push_back({spec.view,1,{{0xB13EBABEBABEBABEull,1,12345,"Fixture",{{1,2,12345,0}}}}});
    done(std::move(result));
  }
  void Write(std::vector<CommunityWrite> views,WriteCompletion done) override {
    Check(views.size()==1 && views[0].view==7 && views[0].columns[0].integer==12345,"write ABI");
    done(error);
  }
  void Estimate(std::vector<CommunityRank> request,RankCompletion done) override {
    Check(request.size()==1 && request[0].view==7 && request[0].rating==12345,"rank ABI");
    if(hold) {
      {std::lock_guard lock(mutex);held.push_back(std::move(done));}
      wake.notify_all();
    } else done({error,{2}});
  }
  void Replay(CommunityReplay request,ReplayCompletion done) override {
    Check(request.view==7 && request.capacity==4,"replay ABI");
    if(request.upload)Check(request.record==std::vector<uint8_t>({1,2,3,4}),"upload bytes");
    done({error,request.upload?std::vector<uint8_t>{}:std::vector<uint8_t>{4,3,2,1}});
  }
  void Stop() override {}
  std::string_view ReplayNamespace() const override {return "abi-fixture";}
};
void Run(KernelState* kernel, uint32_t a,Provider& provider) {
  auto* b=kernel->memory()->TranslateVirtual(a);
  auto put=[&](uint32_t offset,uint32_t value){rex::memory::store_and_swap<uint32_t>(b+offset,value);};
  auto get=[&](uint32_t offset){return rex::memory::load_and_swap<uint32_t>(b+offset);};
  auto put64=[&](uint32_t offset,uint64_t value){rex::memory::store_and_swap<uint64_t>(b+offset,value);};
  // One XUSER_STATS_SPEC: view=7, one 64-bit score column.
  put(0,7);put(4,1);rex::memory::store_and_swap<uint16_t>(b+8,1);
  put64(160,0xB13EBABEBABEBABEull);
  const CommunitySpec spec{7,{1}};
  const uint32_t capacity=CommunityStatsBufferSize({&spec,1},1);
  // Native XUserReadStats with a null OVERLAPPED.
  put(192,0);put(196,1);put(200,a+160);put(204,1);put(208,a);
  put(212,capacity);put(216,a+1024);
  Check(DispatchCommunityMessage(kernel,0xfb,0xb0021,a+192,28,0)==0,"sync user read");
  Check(get(1024)==1 && get(1032)==7 && get(1040)==1,"sync read serialized stats");
  // Write and flush use native message layouts, not a title schema.
  put(256,7);put(260,1);put(264,a+288);
  put(288,0x20000001);b[296]=2;put64(304,12345);
  put(336,1);put(340,a+256);
  Check(DispatchCommunityMessage(kernel,0xfb,0xb0025,a+320,24,0)==0,"sync write");
  Check(DispatchCommunityMessage(kernel,0xfb,0xb0026,0,0,0)==0,"sync flush");
  provider.error=1231;
  Check(DispatchCommunityMessage(kernel,0xfb,0xb0025,a+320,24,0)==0x800704cfu,"sync write error propagation");
  provider.error=0;
  // Enumeration must validate capacity and return exactly one stats container.
  Check(CreateCommunityStatsEnumerator(kernel,0,1,1,1,1,a,a+360,a+364)==0,"create enum");
  auto enumerator=kernel->object_table()->LookupObject<XEnumerator>(get(364));
  uint32_t items=99;
  Check(EnumerateCommunityStats(kernel,enumerator.get(),a+1024,capacity-1,0,&items)==87 && items==0,"enum short buffer");
  Check(EnumerateCommunityStats(kernel,enumerator.get(),a+1024,capacity,0,&items)==0 && items==1,"sync enum result count");
  Check(EnumerateCommunityStats(kernel,enumerator.get(),a+1024,capacity,0,&items)==18 && items==0,"enum consumed once");
  enumerator->Delete();
  // Rank estimation, upload and download all accept synchronous calls.
  put(384,7);put64(388,12345);
  Check(StartCommunityRankIo(kernel,1,a+384,12,a+416,0)==0 && get(416)==1 && get(424)==2,"sync rank");
  const auto path=CommunityReplayPath(7,0xB13EBABEBABEBABEull);
  for(size_t i=0;i<path.size();++i)rex::memory::store_and_swap<uint16_t>(b+512+i*2,uint16_t(path[i]));
  rex::memory::store_and_swap<uint16_t>(b+512+path.size()*2,0);
  std::memcpy(b+800,"\1\2\3\4",4);
  Check(StartCommunityReplayIo(kernel,true,0,a+512,4,a+800,0,0,0)==0,"sync upload");
  Check(StartCommunityReplayIo(kernel,false,0,a+512,4,a+800,20,a+832,0)==0
      && get(800)==0x04030201 && get(832)==4,"sync download bytes/result");
  provider.error=1231;
  put(800,0xcccccccc);
  Check(StartCommunityReplayIo(kernel,false,0,a+512,4,a+800,20,a+832,0)==1231
      && get(800)==0xcccccccc,"failed sync download wrote data");
  provider.error=0;
  Check(StartCommunityRankIo(kernel,1,a+384,11,a+416,0)==87,"rank short buffer");
  // Existing OVERLAPPED calls retain IO_PENDING and native completion fields.
  std::memset(b+896,0,sizeof(XAM_OVERLAPPED));
  Check(StartCommunityRankIo(kernel,1,a+384,12,a+416,a+896)==997,"async rank contract");
  Check(XOverlappedGetResult(b+896)==0 && get(920)==0,"async completion");
  std::memset(b+896,0,sizeof(XAM_OVERLAPPED));
  Check(DispatchCommunityMessage(kernel,0xfb,0xb0025,a+320,24,a+896)==997
      && XOverlappedGetResult(b+896)==0,"async write retained");
}
void CancelTest(KernelState* kernel,uint32_t a,Provider& provider) {
  auto* b=kernel->memory()->TranslateVirtual(a);
  std::memset(b+416,0xcc,32);
  provider.hold=true;
  auto first=std::async(std::launch::async,[&]{return StartCommunityRankIo(kernel,1,a+384,12,a+416,0);});
  auto second=std::async(std::launch::async,[&]{return StartCommunityRankIo(kernel,1,a+384,12,a+432,0);});
  {
    std::unique_lock lock(provider.mutex);
    Check(provider.wake.wait_for(lock,5s,[&]{return provider.held.size()==2;}),"sync operations not queued independently");
  }
  CancelCommunityIo(kernel);
  Check(first.get()==995 && second.get()==995,"shutdown did not cancel both sync callers");
  for(auto& done:provider.held)done({0,{9}});
  Check(std::all_of(b+416,b+448,[](uint8_t value){return value==0xcc;}),"late completion wrote cancelled guest memory");
}
}
int wmain(int argc,wchar_t** argv) {
  try {
    Check(argc==2,"Usage: community_sync_test <isolated-fixture-directory>");
    const auto root=std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(root/"empty-data");
    rex::InitLoggingEarly();rex::InitLogging(rex::BuildLogConfig(nullptr,"off",{}));
    rex::Runtime runtime(root/"empty-data",root/"profile",{},root/"cache",{});
    rex::RuntimeConfig config;config.tool_mode=true;config.kernel_init=rex::kernel::InitializeKernel;
    Check(runtime.Setup(std::move(config))==0,"fixture runtime setup");
    auto* kernel=runtime.kernel_state();
    auto provider=std::make_shared<Provider>();SetCommunityService(provider);
    const auto address=runtime.memory()->SystemHeapAlloc(4096);
    Check(address!=0,"fixture allocation");
    std::memset(runtime.memory()->TranslateVirtual(address),0,4096);
    std::promise<void> done;auto future=done.get_future();
    auto thread=make_object<XHostThread>(kernel,0,0,[&]{
      try {Run(kernel,address,*provider);done.set_value();}
      catch(...) {done.set_exception(std::current_exception());}
      return 0;
    });
    Check(thread->Create()==0,"fixture kernel thread");
    future.get();
    Check(thread->Wait(0,0,0,nullptr)==0,"fixture kernel thread exit");
    thread->Delete();
    CancelTest(kernel,address,*provider);
    SetCommunityService({});runtime.memory()->SystemHeapFree(address);
    runtime.Shutdown();
    std::cout<<"Community sync/async ABI, cancellation and late-completion checks passed; no network or game executed\n";
    return 0;
  } catch(const std::exception& error) {SetCommunityService({});std::cerr<<error.what()<<'\n';return 2;}
}
