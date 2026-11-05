#include "gs_crash_trace.h"
#include "crash_unwind.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace cave::ps2;
namespace fs=std::filesystem;

static LONG Capture(HANDLE file,const EXCEPTION_POINTERS* fault) {
    gs_crash::Write(file);
    WriteFaultingCallstack(file,*fault->ContextRecord);
    return EXCEPTION_EXECUTE_HANDLER;
}
__declspec(noinline) static void Fault(HANDLE file) {
    __try { RaiseException(0xe0000001,0,0,nullptr); }
    __except(Capture(file,GetExceptionInformation())) {}
}
int main() {
    const auto path=fs::temp_directory_path()/("evac-ps2-crash-test-"+std::to_string(GetCurrentProcessId())+".txt");
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return 1;
    gs_crash::Start();
    for(unsigned frame=0;frame<40;++frame)gs_crash::Mark(gs_crash::Stage::Presented,frame);
    gs_crash::Mark(gs_crash::Stage::FenceSignaled,39);
    gs_crash::Mark(gs_crash::Stage::Draw,40);
    std::thread worker([&]{Fault(file);});
    worker.join();
    Fault(file);
    CloseHandle(file);
    std::ifstream in(path,std::ios::binary);
    const std::string report{std::istreambuf_iterator<char>(in),{}};
    in.close();fs::remove(path);
    for(auto expected:{"fault_on_render_thread=0","fault_on_render_thread=1",
        "last_signaled_output_fence_frame=39","event=11 frame=10 stage=present-returned",
        "event=42 frame=40 stage=draw","unwind[1]=","=ps2_gs_crash_contract.exe+0x"}) {
        if(report.find(expected)==report.npos){std::cerr<<report;return 2;}
    }
    if(report.find("event=10 ")!=report.npos)return 3;
    std::cout<<"Render-thread and worker exception reports passed.\n";
}
