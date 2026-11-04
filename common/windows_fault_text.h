#pragma once
#include <Windows.h>
#include <Psapi.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cstdint>

namespace cave {
// Crash-only, bounded writes; no allocation or symbol loading. Preserve useful
// location metadata before the more expensive/best-effort minidump operation.
inline void WriteWindowsFaultText(HANDLE file,const EXCEPTION_POINTERS* fault) noexcept {
    if(file==INVALID_HANDLE_VALUE || !fault || !fault->ExceptionRecord)return;
    const auto write=[&](const char* text){DWORD written{};WriteFile(file,text,DWORD(std::strlen(text)),&written,nullptr);};
    const auto& record=*fault->ExceptionRecord;char line[512]{};
    std::snprintf(line,sizeof(line),"exception=0x%08lx\naddress=%p\nthread=%lu\n",
        record.ExceptionCode,record.ExceptionAddress,GetCurrentThreadId());write(line);
    MEMORY_BASIC_INFORMATION memory{};wchar_t path[4096]{};char name[4096]{};
    if(VirtualQuery(record.ExceptionAddress,&memory,sizeof(memory))) {
        std::snprintf(line,sizeof(line),"fault_memory_state=0x%lx\nfault_memory_type=0x%lx\nfault_memory_protection=0x%lx\n",
            memory.State,memory.Type,memory.Protect);write(line);
        if(memory.Type==MEM_IMAGE && K32GetMappedFileNameW(GetCurrentProcess(),memory.AllocationBase,path,4096)) {
            const auto* leaf=std::wcsrchr(path,L'\\');leaf=leaf?leaf+1:path;
            if(WideCharToMultiByte(CP_UTF8,0,leaf,-1,name,sizeof(name),nullptr,nullptr)) {
                write("fault_module=");write(name);write("\n");
            }
            std::snprintf(line,sizeof(line),"fault_module_base=%p\nfault_module_rva=0x%llx\n",memory.AllocationBase,
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(record.ExceptionAddress)-reinterpret_cast<uintptr_t>(memory.AllocationBase)));write(line);
        }
    }
    if((record.ExceptionCode==EXCEPTION_ACCESS_VIOLATION || record.ExceptionCode==EXCEPTION_IN_PAGE_ERROR) && record.NumberParameters>=2) {
        const auto op=record.ExceptionInformation[0];
        std::snprintf(line,sizeof(line),"access=%s\naccess_address=0x%llx\n",op==0?"read":op==1?"write":op==8?"execute":"unknown",
            static_cast<unsigned long long>(record.ExceptionInformation[1]));write(line);
    }
    if(fault->ContextRecord) {
        const auto& c=*fault->ContextRecord;
        std::snprintf(line,sizeof(line),"rip=0x%llx\nrsp=0x%llx\nrbp=0x%llx\nrax=0x%llx\nrcx=0x%llx\nrdx=0x%llx\n",
            c.Rip,c.Rsp,c.Rbp,c.Rax,c.Rcx,c.Rdx);write(line);
        // Keep module-relative leads even if the subsequent minidump is truncated.
        // A stack word pointing at code is a candidate, not an unwound frame.
        uintptr_t words[128]{};
        SIZE_T received{};
        if (!c.Rsp || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(c.Rsp),
                                         words, sizeof(words), &received)) return;
        unsigned emitted=0;
        for (size_t i=0;i<received/sizeof(words[0]) && emitted<16;++i) {
            const auto pc=words[i];
            MEMORY_BASIC_INFORMATION code{};
            if (!pc || !VirtualQuery(reinterpret_cast<const void*>(pc),&code,sizeof(code)) ||
                code.Type!=MEM_IMAGE || code.State!=MEM_COMMIT ||
                !(code.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) continue;
            if (!K32GetMappedFileNameW(GetCurrentProcess(),code.AllocationBase,path,4096)) continue;
            path[4095]=0;
            const auto* leaf=std::wcsrchr(path,L'\\');
            if (!WideCharToMultiByte(CP_UTF8,0,leaf?leaf+1:path,-1,name,sizeof(name),nullptr,nullptr)) continue;
            std::snprintf(line,sizeof(line),"stack_candidate[%zu]=%s+0x%llx\n",i,name,
                static_cast<unsigned long long>(pc-reinterpret_cast<uintptr_t>(code.AllocationBase)));
            write(line);++emitted;
        }
    }
}
}
