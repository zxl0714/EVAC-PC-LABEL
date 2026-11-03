#include "crash_details.h"
#include "paths.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace cave {
namespace {
class Dump {
    std::ifstream file;
    uint64_t size;
public:
    explicit Dump(const fs::path& path):file(path,std::ios::binary),size(fs::file_size(path)) {
        if(!file)throw std::runtime_error("Cannot open minidump");
    }
    void Range(uint64_t at,uint64_t length)const {
        if(at>size || length>size-at)throw std::runtime_error("Truncated minidump");
    }
    void Read(uint64_t at,void* data,size_t length) {
        Range(at,length);file.seekg(std::streamoff(at));
        if(!file.read(static_cast<char*>(data),std::streamsize(length)))throw std::runtime_error("Cannot read minidump");
    }
    template<class T>T Get(uint64_t at) {T result{};Read(at,&result,sizeof(result));return result;}
    std::wstring String(uint64_t at) {
        const auto bytes=Get<uint32_t>(at);
        if(bytes>65536 || bytes%2)throw std::runtime_error("Invalid minidump string");
        std::wstring value(bytes/2,L'\0');Read(at+4,value.data(),bytes);return value;
    }
};
struct Module {MINIDUMP_MODULE data;std::string name;};
std::string Location(uint64_t address,const std::vector<Module>& modules) {
    for(const auto& m:modules)if(address>=m.data.BaseOfImage && address-m.data.BaseOfImage<m.data.SizeOfImage) {
        std::ostringstream out;out<<m.name<<"+0x"<<std::hex<<address-m.data.BaseOfImage;return out.str();
    }
    return "unmapped";
}
}
std::string SummarizeCrashDump(const fs::path& path) {
    Dump dump(path);const auto header=dump.Get<MINIDUMP_HEADER>(0);
    if(header.Signature!=MINIDUMP_SIGNATURE || header.NumberOfStreams>4096)
        throw std::runtime_error("Invalid minidump header");
    std::map<uint32_t,MINIDUMP_LOCATION_DESCRIPTOR> streams;
    for(uint32_t i=0;i<header.NumberOfStreams;++i) {
        const auto entry=dump.Get<MINIDUMP_DIRECTORY>(uint64_t(header.StreamDirectoryRva)+i*sizeof(MINIDUMP_DIRECTORY));
        dump.Range(entry.Location.Rva,entry.Location.DataSize);streams[entry.StreamType]=entry.Location;
    }
    const auto stream=[&](uint32_t kind,size_t minimum) {
        const auto it=streams.find(kind);
        if(it==streams.end() || it->second.DataSize<minimum)throw std::runtime_error("Missing or incomplete minidump stream");
        return it->second;
    };
    const auto moduleList=stream(ModuleListStream,4);
    const auto moduleCount=dump.Get<uint32_t>(moduleList.Rva);
    if(moduleCount>4096 || 4+uint64_t(moduleCount)*sizeof(MINIDUMP_MODULE)>moduleList.DataSize)
        throw std::runtime_error("Invalid minidump module count");
    std::vector<Module> modules;modules.reserve(moduleCount);
    for(uint32_t i=0;i<moduleCount;++i) {
        auto data=dump.Get<MINIDUMP_MODULE>(uint64_t(moduleList.Rva)+4+i*sizeof(MINIDUMP_MODULE));
        auto name=Utf8(fs::path(dump.String(data.ModuleNameRva)).filename());
        if(name.size()>1024)throw std::runtime_error("Invalid minidump module name");
        modules.push_back({data,std::move(name)});
    }
    const auto exception=dump.Get<MINIDUMP_EXCEPTION_STREAM>(stream(ExceptionStream,sizeof(MINIDUMP_EXCEPTION_STREAM)).Rva);
    const auto system=dump.Get<MINIDUMP_SYSTEM_INFO>(stream(SystemInfoStream,sizeof(MINIDUMP_SYSTEM_INFO)).Rva);
    const auto& record=exception.ExceptionRecord;
    if(record.NumberParameters>EXCEPTION_MAXIMUM_PARAMETERS)throw std::runtime_error("Invalid exception parameters");
    std::ostringstream out;
    out<<"schema=1\nsource=existing_minidump\nwindows="<<system.MajorVersion<<'.'<<system.MinorVersion<<'.'<<system.BuildNumber
       <<"\nprocessor_architecture="<<system.ProcessorArchitecture<<"\nthread="<<exception.ThreadId
       <<std::hex<<"\nexception=0x"<<record.ExceptionCode<<"\naddress=0x"<<record.ExceptionAddress
       <<"\nfault_module="<<Location(record.ExceptionAddress,modules)<<'\n';
    if((record.ExceptionCode==EXCEPTION_ACCESS_VIOLATION || record.ExceptionCode==EXCEPTION_IN_PAGE_ERROR) && record.NumberParameters>=2) {
        const auto op=record.ExceptionInformation[0];
        out<<"access="<<(op==0?"read":op==1?"write":op==8?"execute":"unknown")
           <<"\naccess_address=0x"<<record.ExceptionInformation[1]<<'\n';
    }
    for(uint32_t i=0;i<record.NumberParameters;++i)out<<"exception_parameter_"<<i<<"=0x"<<record.ExceptionInformation[i]<<'\n';
    if(system.ProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64 && exception.ThreadContext.DataSize>=256) {
        dump.Range(exception.ThreadContext.Rva,exception.ThreadContext.DataSize);
        for(const auto& [name,offset]:{std::pair{"rip",248}, {"rsp",152}, {"rbp",160}, {"rax",120}, {"rcx",128}, {"rdx",136}})
            out<<name<<"=0x"<<dump.Get<uint64_t>(uint64_t(exception.ThreadContext.Rva)+offset)<<'\n';
    }
    out<<"\n[loaded_modules]\n";
    for(const auto& m:modules) {
        const auto& v=m.data.VersionInfo;
        out<<m.name<<" base=0x"<<m.data.BaseOfImage<<" size=0x"<<m.data.SizeOfImage<<" timestamp=0x"<<m.data.TimeDateStamp
           <<std::dec<<" version="<<HIWORD(v.dwFileVersionMS)<<'.'<<LOWORD(v.dwFileVersionMS)<<'.'
           <<HIWORD(v.dwFileVersionLS)<<'.'<<LOWORD(v.dwFileVersionLS)<<std::hex<<'\n';
    }
    return out.str();
}
}
