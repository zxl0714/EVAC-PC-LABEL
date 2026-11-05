#pragma once
#include <Windows.h>
#include <Psapi.h>
#include <cstdio>
#include <cwchar>

namespace cave::ps2 {
// Called on the faulting x64 thread, after writing the ordinary report and
// minidump. Use the original exception context and the loaded PE unwind tables.
inline void WriteFaultingCallstack(HANDLE file,const CONTEXT& fault) noexcept {
    CONTEXT ctx=fault;
    ULONG_PTR low{},high{};
    GetCurrentThreadStackLimits(&low,&high);
    char line[512]{};
    auto emit=[&](int length) {
        DWORD written{};
        if(length>0 && size_t(length)<sizeof(line))WriteFile(file,line,DWORD(length),&written,nullptr);
    };
    __try {
        for(unsigned i=0;i<32 && ctx.Rip && ctx.Rsp>=low && ctx.Rsp<high;++i) {
            MEMORY_BASIC_INFORMATION image{};
            if(!VirtualQuery(reinterpret_cast<void*>(ctx.Rip),&image,sizeof(image)) || image.Type!=MEM_IMAGE ||
               !(image.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))break;
            wchar_t path[1024]{};char module[256]{};
            const auto length=K32GetMappedFileNameW(GetCurrentProcess(),image.AllocationBase,path,1024);
            if(!length || length>=1024)break;
            const auto* sep=std::wcsrchr(path,L'\\');
            if(!WideCharToMultiByte(CP_UTF8,0,sep?sep+1:path,-1,module,sizeof(module),nullptr,nullptr))break;
            emit(std::snprintf(line,sizeof(line),"unwind[%u]=%s+0x%llx\n",i,module,
                               ctx.Rip-reinterpret_cast<DWORD64>(image.AllocationBase)));
            DWORD64 base{},establisher{};void* handlerData{};
            const auto sp=ctx.Rsp;
            if(const auto entry=RtlLookupFunctionEntry(ctx.Rip,&base,nullptr)) {
                RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,ctx.Rip,entry,&ctx,&handlerData,&establisher,nullptr);
            }else {
                // An x64 leaf function keeps its return address at RSP.
                SIZE_T bytes{};
                if(high-sp<sizeof(ctx.Rip) || !ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(sp),
                    &ctx.Rip,sizeof(ctx.Rip),&bytes) || bytes!=sizeof(ctx.Rip))break;
                ctx.Rsp+=sizeof(ctx.Rip);
            }
            if(ctx.Rsp<=sp)break;
        }
    }__except(EXCEPTION_EXECUTE_HANDLER) {
        emit(std::snprintf(line,sizeof(line),"unwind_stopped=unreadable\n"));
    }
}
}
