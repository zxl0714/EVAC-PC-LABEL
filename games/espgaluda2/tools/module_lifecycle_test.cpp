// Exercises generated native registration, never calls a translated game function.
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <array>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/kernel_module.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>
#include <rex/system/xmemory.h>
using rex::X_STATUS;
namespace fs=std::filesystem;
void Require(bool value,const char* reason) {if (!value) throw std::runtime_error(reason);}
void AuthoredSentinel(PPCContext&,std::uint8_t*) {throw std::runtime_error("Test must not execute PPC functions");}
bool Within(const fs::path& path,const fs::path& parent) {
    auto part=path.begin();
    for (auto p=parent.begin();p!=parent.end();++p,++part)
        if (part==path.end() || CompareStringOrdinal(part->c_str(),-1,p->c_str(),-1,TRUE)!=CSTR_EQUAL) return false;
    return true;
}
int wmain(int argc,wchar_t** argv) {
    try {
        Require(argc==3,"Usage: module_lifecycle_test <game-data> <NEW-output>");
        const auto game=fs::canonical(argv[1]);
        const auto output=fs::weakly_canonical(fs::absolute(argv[2]));
        Require(!fs::exists(output),"Output must be new");
        Require(!Within(output,game) && !Within(game,output),"Output must be separate from game inputs");
        fs::create_directories(output);
        rex::InitLoggingEarly();
        auto logging=rex::BuildLogConfig(nullptr,"warn",{});rex::InitLogging(logging);
        rex::Runtime runtime(game,output/"user-data",{},output/"cache",output/"metadata");
        Require(runtime.Setup({.kernel_init=rex::kernel::InitializeKernel,.tool_mode=true})==X_STATUS_SUCCESS,"Tool runtime setup");
        Require(!runtime.graphics_system() && !runtime.audio_system() && !runtime.input_system(),"Interactive subsystem created");
        Require(runtime.LoadXexImage("game:\\default.xex")==X_STATUS_SUCCESS,"Main image load");
        auto* kernel=runtime.kernel_state();auto* dispatcher=runtime.function_dispatcher();
        Require(dispatcher->InitializeFunctionTable(0x82090000,0x2EBDDC,0x82000000,0xA00000,true),"Resident dispatch table setup");
        dispatcher->RegisterModule("authored-main",0x82090000,[](rex::runtime::IModuleRegistrar* registrar){
            registrar->SetFunction(0x820DD9A0,AuthoredSentinel);
        });
        const auto mainThunk=dispatcher->AllocateThunk(AuthoredSentinel,0x820DD9A0);
        Require(mainThunk && dispatcher->GetFunction(mainThunk)==AuthoredSentinel,"Resident thunk setup");
        auto krnl=kernel->GetKernelModule("xboxkrnl.exe");
        Require(bool(krnl),"Kernel export module unavailable");
        const auto residentKernelThunk=krnl->GetProcAddressByOrdinal(207,0x820DD9A0);
        Require(residentKernelThunk && dispatcher->GetFunction(residentKernelThunk),"Resident kernel thunk setup");
        constexpr std::array<const char*,7> names{"dat00","dat11","dat12","dat15","dat162","dat163","dat164"};
        for (auto name:names) {
            const auto file=std::string(name)+".xex",library=std::string("espgaluda2_")+name;
            kernel->RegisterRecompiledModule(file.c_str(),file.c_str(),library.c_str());
        }
        for (unsigned cycle=0;cycle<3;++cycle) for (auto name:names) {
            const auto path=std::string("game:\\")+name+".xex";
            auto module=kernel->LoadUserModule(path,false);
            Require(bool(module),"Native overlay load/registration failed");
            Require(bool(kernel->FindRecompiledModule(module->path())),"Resolved device path lost native module identity");
            {
                const auto alias=std::string("d:\\")+name+".xex";
                auto repeated=kernel->LoadUserModule(alias,false);
                Require(repeated.get()==module.get(),"Device aliases loaded the same image twice");
            }
            const auto entry=module->entry_point();
            const auto factory=module->xex_module()->GetProcAddress(2);
            Require(dispatcher->GetFunction(entry)!=nullptr,"DLL entrypoint missing from native dispatch");
            Require(factory && dispatcher->GetFunction(factory)!=nullptr,"Factory export missing from native dispatch");
            Require(dispatcher->FindCallerModuleBase(entry)==0x88020000,"Wrong overlay code region");
            const auto thunk=krnl->GetProcAddressByOrdinal(207,entry); // NtClose; resolve, never invoke.
            Require(thunk && dispatcher->GetFunction(thunk) && dispatcher->FindCallerModuleBase(thunk)==0x88020000,
                "Kernel thunk cache returned an obsolete overlay address");
            kernel->UnloadUserModule(module,false);module.reset();
            Require(dispatcher->GetFunction(entry)==nullptr && dispatcher->FindCallerModuleBase(entry)==0,"Stale native dispatch survived unload");
            Require(dispatcher->GetFunction(thunk)==nullptr,"Kernel thunk survived overlay unload");
            Require(dispatcher->GetFunction(mainThunk)==AuthoredSentinel && dispatcher->GetFunction(0x820DD9A0)==AuthoredSentinel,
                "Overlay unload removed resident functions or thunks");
            Require(krnl->GetProcAddressByOrdinal(207,0x820DD9A0)==residentKernelThunk && dispatcher->GetFunction(residentKernelThunk),
                "Overlay unload invalidated the resident kernel thunk cache");
            rex::memory::HeapAllocationInfo allocation{};
            auto* heap=runtime.memory()->LookupHeap(0x82000000);
            Require(heap->QueryRegionInfo(0x82000000,&allocation) && allocation.allocation_base==0x82000000 && allocation.state,
                "Main image reservation lost");
            Require(heap->QueryRegionInfo(0x88000000,&allocation) && !allocation.state,"Overlay image not released");
            std::cout<<"Cycle "<<cycle+1<<" "<<name<<": native registration/unload passed; guest execution=false\n";
        }
        krnl.reset();dispatcher->UnregisterModule("authored-main");runtime.Shutdown();
        std::cout<<"All 21 native overlay cycles passed; no DllMain or game functions called\n";
        return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
