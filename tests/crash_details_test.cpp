#include "common/crash_details.h"
#include "common/windows_fault_text.h"
#include "crash_dump_fixture.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
void Require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc==3 && std::wstring_view(argv[1])==L"--dump"){std::cout<<cave::SummarizeCrashDump(argv[2]);return 0;}
        const auto root=fs::temp_directory_path()/(L"evac-crash-测试-"+std::to_wstring(GetCurrentProcessId()));
        fs::create_directories(root);const auto path=root/L"crash.dmp";
        auto bytes=AuthoredCrashDump();
        const auto save=[&]{std::ofstream f(path,std::ios::binary);f.write(bytes.data(),bytes.size());};save();
        const auto summary=cave::SummarizeCrashDump(path);
        Require(summary.find("fault_module=authored-driver.dll+0x1234")!=summary.npos,"Missing module/RVA");
        Require(summary.find("access=write")!=summary.npos && summary.find("access_address=0x0")!=summary.npos,"Missing access type/address");
        Require(summary.find("version=32.0.101.9000")!=summary.npos,"Incorrect module version");
        Require(summary.find("C:\\Windows")==summary.npos,"Full module path should not be needed");
        bytes.resize(500);save();bool rejected=false;
        try{(void)cave::SummarizeCrashDump(path);}catch(const std::exception&){rejected=true;}
        Require(rejected,"Truncated dump accepted");
        bytes=AuthoredCrashDump();uint32_t badCount=0xffffffff;std::memcpy(bytes.data()+128,&badCount,4);save();rejected=false;
        try{(void)cave::SummarizeCrashDump(path);}catch(const std::exception&){rejected=true;}
        Require(rejected,"Invalid module count accepted");
        EXCEPTION_RECORD record{};record.ExceptionCode=EXCEPTION_ACCESS_VIOLATION;
        record.ExceptionAddress=reinterpret_cast<void*>(&wmain);record.NumberParameters=2;record.ExceptionInformation[0]=8;
        CONTEXT context{};RtlCaptureContext(&context);EXCEPTION_POINTERS fault{&record,&context};
        const auto report=root/L"crash.txt";
        auto file=CreateFileW(report.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(file!=INVALID_HANDLE_VALUE,"Cannot create fault report");cave::WriteWindowsFaultText(file,&fault);CloseHandle(file);
        std::ifstream input(report);const std::string text{std::istreambuf_iterator<char>(input),{}};input.close();
        Require(text.find("fault_module=cave_crash_tests.exe")!=text.npos,"Live module lookup failed");
        Require(text.find("fault_module_rva=0x")!=text.npos && text.find("access=execute")!=text.npos,"Incomplete live fault report");
        fs::remove_all(root);std::cout<<"Crash metadata and dump parser: PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
