#include "crash_reporter.h"
#include "crash_shared.h"
#include <csignal>
#include <cstdio>
#include <cstring>
#include <array>

namespace espgaluda2 {
namespace {
G2CrashRecord* record{};
HANDLE request_event{},done_event{};
LPTOP_LEVEL_EXCEPTION_FILTER previous_filter{};
bool RecordCrash(EXCEPTION_POINTERS* pointers,const char* reason) {
    if (!record || InterlockedCompareExchange(&record->claimed,1,0)) return false;
    record->thread_id=GetCurrentThreadId();
    record->exception=*pointers->ExceptionRecord;
    record->exception.ExceptionRecord=nullptr;
    record->context=*pointers->ContextRecord;
    strcpy_s(record->reason,reason);
    MemoryBarrier();SetEvent(request_event);
    return WaitForSingleObject(done_event,20000)==WAIT_OBJECT_0;
}
LONG WINAPI CrashFilter(EXCEPTION_POINTERS* pointers) {
    RecordCrash(pointers,"unhandled-exception");
    if (previous_filter) return previous_filter(pointers);
    return EXCEPTION_EXECUTE_HANDLER;
}
void AbortHandler(int) {
    CONTEXT context{};RtlCaptureContext(&context);
    EXCEPTION_RECORD exception{};
    exception.ExceptionCode=0x40000015;
    exception.ExceptionAddress=reinterpret_cast<void*>(context.Rip);
    EXCEPTION_POINTERS pointers{&exception,&context};
    RecordCrash(&pointers,"SIGABRT-synthetic-context");
}
}
bool StartCrashReporter(const std::filesystem::path& run_directory) {
    if (record) return true;
    const auto prefix=std::filesystem::absolute(run_directory)/"game";
    if (prefix.wstring().size()>=4096 || !std::filesystem::is_directory(run_directory)) return false;
    std::array<wchar_t,32768> executable{};
    const auto length=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
    if (!length || length>=executable.size()) return false;
    const auto directory=std::filesystem::path(executable.data()).parent_path();
    const auto helper=directory/"espgaluda2_crash_writer.exe";
    if (!std::filesystem::is_regular_file(helper)) return false;
    wchar_t unique[128]{};
    swprintf_s(unique,L"Local\\Espgaluda2Diag-%lu-%llu",GetCurrentProcessId(),GetTickCount64());
    const std::wstring stem=unique;
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(G2CrashRecord),(stem+L"-memory").c_str());
    if (!mapping) return false;
    auto* shared=static_cast<G2CrashRecord*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(G2CrashRecord)));
    request_event=CreateEventW(nullptr,TRUE,FALSE,(stem+L"-request").c_str());
    done_event=CreateEventW(nullptr,TRUE,FALSE,(stem+L"-done").c_str());
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,(stem+L"-ready").c_str());
    bool started{};
    if (shared && request_event && done_event && ready) {
        std::memset(shared,0,sizeof(*shared));
        wcscpy_s(shared->report_prefix,prefix.c_str());strcpy_s(shared->phase,"startup");
        std::wstring command=L"\""+helper.wstring()+L"\" "+std::to_wstring(GetCurrentProcessId())+L" \""+stem+L"\"";
        STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
        if (CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,
            directory.c_str(),&startup,&process)) {
            CloseHandle(process.hThread);
            HANDLE waits[]{ready,process.hProcess};
            started=WaitForMultipleObjects(2,waits,FALSE,5000)==WAIT_OBJECT_0;
            CloseHandle(process.hProcess);
        }
    }
    if (ready) CloseHandle(ready);
    CloseHandle(mapping);
    if (!started) {
        if (shared) UnmapViewOfFile(shared);
        if (request_event) CloseHandle(request_event);
        if (done_event) CloseHandle(done_event);
        request_event=done_event=nullptr;
        return false;
    }
    record=shared;
    previous_filter=SetUnhandledExceptionFilter(CrashFilter);
    std::signal(SIGABRT,AbortHandler);
    return true;
}
void CrashModule(const char* path,const char* phase,uint64_t memory_base,uint32_t image_base,uint32_t image_size) {
    if (!record) return;
    strncpy_s(record->module_path,path?path:"",_TRUNCATE);
    strncpy_s(record->phase,phase?phase:"",_TRUNCATE);
    record->guest_memory_base=memory_base;record->guest_image_base=image_base;record->guest_image_size=image_size;
}
bool CrashReporterSelfTest() {
    // Capture a synthetic context without raising an OS exception or showing WER.
    CONTEXT context{};RtlCaptureContext(&context);
    EXCEPTION_RECORD exception{};
    exception.ExceptionCode=0xE042F201;
    exception.ExceptionAddress=reinterpret_cast<void*>(context.Rip);
    EXCEPTION_POINTERS pointers{&exception,&context};
    return RecordCrash(&pointers,"authored-self-test");
}
}
