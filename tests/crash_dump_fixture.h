#pragma once
#include <Windows.h>
#include <DbgHelp.h>
#include <cstring>
#include <vector>
#include <string>

inline std::vector<char> AuthoredCrashDump() {
    std::vector<char> bytes(2048);
    const auto put=[&](size_t at,const auto& value){std::memcpy(bytes.data()+at,&value,sizeof(value));};
    MINIDUMP_HEADER header{};header.Signature=MINIDUMP_SIGNATURE;header.Version=MINIDUMP_VERSION;
    header.NumberOfStreams=3;header.StreamDirectoryRva=32;put(0,header);
    put(32,MINIDUMP_DIRECTORY{ModuleListStream,{112,128}});
    put(44,MINIDUMP_DIRECTORY{ExceptionStream,{168,256}});
    put(56,MINIDUMP_DIRECTORY{SystemInfoStream,{56,448}});
    put(128,uint32_t{1});MINIDUMP_MODULE module{};
    module.BaseOfImage=0x7ffd12340000;module.SizeOfImage=65536;module.ModuleNameRva=600;
    module.VersionInfo.dwFileVersionMS=32u<<16;module.VersionInfo.dwFileVersionLS=(101u<<16)|9000u;
    put(132,module);
    const std::wstring name=L"C:\\Windows\\System32\\authored-driver.dll";
    put(600,uint32_t(name.size()*2));std::memcpy(bytes.data()+604,name.data(),name.size()*2);
    MINIDUMP_SYSTEM_INFO system{};system.ProcessorArchitecture=PROCESSOR_ARCHITECTURE_AMD64;
    system.MajorVersion=10;system.BuildNumber=26100;put(448,system);
    MINIDUMP_EXCEPTION_STREAM exception{};exception.ThreadId=1234;
    exception.ExceptionRecord.ExceptionCode=EXCEPTION_ACCESS_VIOLATION;
    exception.ExceptionRecord.ExceptionAddress=module.BaseOfImage+0x1234;
    exception.ExceptionRecord.NumberParameters=2;exception.ExceptionRecord.ExceptionInformation[0]=1;
    exception.ThreadContext={256,768};put(256,exception);
    put(768+248,exception.ExceptionRecord.ExceptionAddress);
    return bytes;
}
