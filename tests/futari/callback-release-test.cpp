#include <rex/system/function_dispatcher.h>
#include <rex/ppc/context.h>
#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
struct Registrar:rex::runtime::IModuleRegistrar {
  PPCFunc* callback{};
  PPCFunc* zero{};
  PPCFunc* tail{};
  bool SetFunction(uint32_t address,PPCFunc* function) override {
    if(address==0x881b0038)callback=function;
    if(address==0x881e4e80)zero=function;
    if(address==0x8817d190)tail=function;
    return true;
  }
};
void Write(uint8_t* base,uint32_t at,uint32_t value) {
  for(unsigned i=0;i<4;++i)base[at+i]=uint8_t(value>>(24-8*i));
}
uint32_t Read(uint8_t* base,uint32_t at) {
  uint32_t value=0;for(unsigned i=0;i<4;++i)value=(value<<8)|base[at+i];return value;
}
void Require(bool value){if(!value)throw std::runtime_error("Original release callback state mismatch");}
}
int wmain(int argc,wchar_t** argv) {
  try {
    if(argc!=2)throw std::runtime_error("Expected the recompiled reco_normal DLL");
    const auto path=std::filesystem::absolute(argv[1]);
    const auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module)throw std::runtime_error("Cannot load recompiled module");
    const auto registration=reinterpret_cast<void(*)(rex::runtime::IModuleRegistrar*)>(GetProcAddress(module,"ReXModule_Register"));
    if(!registration)throw std::runtime_error("No module registration export");
    Registrar registrar;registration(&registrar);
    if(!registrar.callback)throw std::runtime_error("Missing original object-release callback 0x881B0038");
    if(!registrar.zero || !registrar.tail)throw std::runtime_error("Missing reviewed leaf callback counterpart");
    auto* base=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x100000000ull,MEM_RESERVE,PAGE_READWRITE));
    if(!base)throw std::runtime_error("Cannot reserve guest address space");
    for(uint32_t address:{0x10000u,0x20000u,0x30000u,0x883e0000u})
      if(!VirtualAlloc(base+address,0x10000,MEM_COMMIT,PAGE_READWRITE))throw std::runtime_error("Cannot commit fixture page");
    constexpr uint32_t Object=0x10000,State=0x20000,Pending=0x30000,Queue=0x883e0240;
    for(unsigned scenario=0;scenario<4;++scenario) {
      std::memset(base+Object,0,0x10000);std::memset(base+State,0,0x10000);
      std::memset(base+Pending,0xa5,0x10000);std::memset(base+0x883e0000,0xa5,0x10000);
      Write(base,Object+0x900,State);Write(base,State+0x84,0x12345678);
      Write(base,State+0x80,scenario==3?0:Pending);
      const unsigned count=scenario==1?2999:scenario==2?3000:0;
      Write(base,Queue-104,count);
      for(unsigned i=0;i<10;++i) {
        Write(base,Object+0x710+i*0x30,1);
        Write(base,Object+0x720+i*0x30,(i&1)?0x12345678:0x87654321);
      }
      PPCContext context{};context.r3.u64=Object;registrar.callback(context,base);
      for(unsigned i=0;i<10;++i)Require(Read(base,Object+0x710+i*0x30)==((i&1)?0:1));
      const bool queued=scenario<2;
      Require(Read(base,Queue-104)==count+unsigned(queued));
      Require(Read(base,Pending+8)==(queued?0:0xa5a5a5a5));
      if(queued)Require(Read(base,Queue+count*4)==Pending);
      context.r3.u64=0x12345678;registrar.zero(context,base);Require(context.r3.u64==0);
    }
    VirtualFree(base,0,MEM_RELEASE);FreeLibrary(module);
    std::cout<<"Futari stored object-release callback: 4 original-body cases: PASS\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
