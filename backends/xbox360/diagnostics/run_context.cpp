#include "run_context.h"
#include "build_metadata.h"
#include "community/http_client.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <nlohmann/json.hpp>

namespace espgaluda2 {
namespace fs=std::filesystem;
using Json=nlohmann::json;
namespace {
class Hash {
public:
    Hash() {
        if (BCryptOpenAlgorithmProvider(&algorithm_,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
            throw std::runtime_error("Cannot open SHA-256 provider");
        if (BCryptCreateHash(algorithm_,&hash_,nullptr,0,nullptr,0,0)<0) {
            BCryptCloseAlgorithmProvider(algorithm_,0);
            throw std::runtime_error("Cannot initialize SHA-256");
        }
    }
    ~Hash() {BCryptDestroyHash(hash_); BCryptCloseAlgorithmProvider(algorithm_,0);}
    void Add(const void* data,std::size_t size) {
        if (size>0xffffffff || BCryptHashData(hash_,reinterpret_cast<PUCHAR>(const_cast<void*>(data)),ULONG(size),0)<0)
            throw std::runtime_error("Cannot hash input");
    }
    std::string Finish() {
        std::array<unsigned char,32> bytes{};
        if (BCryptFinishHash(hash_,bytes.data(),ULONG(bytes.size()),0)<0) throw std::runtime_error("Cannot finish SHA-256");
        constexpr char digits[]="0123456789abcdef";
        std::string result; result.reserve(64);
        for (auto byte:bytes) {result+=digits[byte>>4];result+=digits[byte&15];}
        return result;
    }
private:BCRYPT_ALG_HANDLE algorithm_{};BCRYPT_HASH_HANDLE hash_{};
};
bool Within(const fs::path& path,const fs::path& parent) {
    auto part=path.begin();
    for (auto p=parent.begin(); p!=parent.end();++p,++part)
        if (part==path.end() || CompareStringOrdinal(part->c_str(),-1,p->c_str(),-1,TRUE)!=CSTR_EQUAL) return false;
    return true;
}
void WriteNew(const fs::path& path,std::string_view text) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create new run file: "+path.filename().string());
    DWORD written{};
    const bool success=WriteFile(file,text.data(),DWORD(text.size()),&written,nullptr) && written==text.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!success) throw std::runtime_error("Cannot write run file: "+path.filename().string());
}
std::string PathUtf8(const fs::path& path) {
    const auto text=path.u8string();
    return {reinterpret_cast<const char*>(text.data()),text.size()};
}
fs::path NewReportDirectory(const fs::path& root,const wchar_t* prefix) {
    const auto logs=fs::weakly_canonical(root/"logs");
    if (!Within(logs,root) || Within(logs,fs::weakly_canonical(root/"game-data")))
        throw std::runtime_error("Log directory must stay inside the project and outside game inputs");
    SYSTEMTIME now{};GetSystemTime(&now);
    wchar_t name[128]{};
    swprintf_s(name,L"%s-%04u%02u%02uT%02u%02u%02u%03uZ-%lu",prefix,now.wYear,now.wMonth,now.wDay,
        now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,GetCurrentProcessId());
    fs::create_directories(logs);
    const auto directory=logs/name;
    if (!fs::create_directory(directory)) throw std::runtime_error("Report directory already exists; refusing overwrite");
    return directory;
}
}
std::string Sha256Text(std::string_view text) {Hash hash;hash.Add(text.data(),text.size());return hash.Finish();}
std::string Sha256File(const fs::path& file) {
    std::ifstream input(file,std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open input: "+file.filename().string());
    Hash hash;
    std::vector<char> buffer(1024*1024);
    while (input) {input.read(buffer.data(),buffer.size());hash.Add(buffer.data(),input.gcount());}
    if (input.bad()) throw std::runtime_error("Cannot read input: "+file.filename().string());
    return hash.Finish();
}
RunContext PrepareRun(const fs::path& requestedRoot,const fs::path& requestedProfile) {
    RunContext run;
    run.root=fs::canonical(requestedRoot);
    const auto game=fs::canonical(run.root/"game-data");
    run.profile=fs::weakly_canonical(requestedProfile.empty()?run.root/"user-data":fs::absolute(requestedProfile));
    if (Within(run.profile,game) || Within(game,run.profile) || run.profile==run.root ||
        fs::exists(run.profile/"default.xex")) throw std::runtime_error("User data must be separate from game inputs");
    const auto metadata=Json::parse(kXexMetadata);
    Json inputs=Json::array();
    for (const auto& item:metadata.at("xex")) {
        const auto name=item.at("path").get<std::string>();
        const auto path=game/fs::path(name); // Manifest module names are ASCII.
        if (fs::file_size(path)!=item.at("bytes").get<std::uint64_t>())
            throw std::runtime_error("Unsupported XEX size: "+name);
        const auto hash=Sha256File(path);
        if (hash!=item.at("sha256").get<std::string>()) throw std::runtime_error("Unsupported or modified XEX: "+name);
        inputs.push_back({{"path",name},{"sha256",hash},{"bytes",fs::file_size(path)}});
    }
    const auto configuration=run.root/"config.ini";
    if (!fs::exists(configuration)) WriteNew(configuration,kDefaultIni);
    const auto snapshot=ReadSettingsText(configuration);
    run.settings=ParseSettings(snapshot);
    if (run.settings.community_enabled) {
        if (!cave::community::ValidEndpoint(run.settings.endpoint))
            throw std::runtime_error("[community].endpoint requires HTTPS or loopback HTTP and an API root without query/fragment");
        run.settings.nickname=cave::community::NormalizeNickname(run.settings.nickname);
        if (!cave::community::ValidNickname(run.settings.nickname))
            throw std::runtime_error("[community].nickname requires 1..48 UTF-8 bytes after normalization, without invisible controls");
    }
    run.source_build=std::string(kSourceCommit)+(kSourceDirty=="true"?"+dirty":"");
    run.directory=NewReportDirectory(run.root,L"run");
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    Json record{{"format",1},{"sourceCommit",kSourceCommit},{"sourceDirty",kSourceDirty=="true"},
        {"root",PathUtf8(run.root)},{"profile",PathUtf8(run.profile)},{"qpcFrequency",frequency.QuadPart},
        {"pid",GetCurrentProcessId()},{"processCodePage",GetACP()},
        {"gameInputs",inputs},{"configSha256",Sha256Text(snapshot)},
        {"communityEnabled",run.settings.community_enabled},{"keyboardMouseCapture",false},{"binaries",Json::array()}};
    std::array<wchar_t,32768> executable{};
    auto length=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
    if (!length || length>=executable.size()) throw std::runtime_error("Cannot determine executable path");
    const auto binaryRoot=fs::path(executable.data()).parent_path();
    for (const auto* file:{"espgaluda2.exe","espgaluda2.pdb","espgaluda2_crash_writer.exe","espgaluda2_crash_writer.pdb",
        "rexruntime.dll","rexruntime.pdb","rexgpu-xenos.dll",
        "rexgpu-xenos.pdb","espgaluda2_dat00.dll","espgaluda2_dat11.dll","espgaluda2_dat12.dll",
        "espgaluda2_dat15.dll","espgaluda2_dat162.dll","espgaluda2_dat163.dll","espgaluda2_dat164.dll",
        "espgaluda2_dat00.pdb","espgaluda2_dat11.pdb","espgaluda2_dat12.pdb","espgaluda2_dat15.pdb",
        "espgaluda2_dat162.pdb","espgaluda2_dat163.pdb","espgaluda2_dat164.pdb"}) {
        const auto path=binaryRoot/file;
        if (fs::is_regular_file(path)) record["binaries"].push_back({{"path",file},{"bytes",fs::file_size(path)},{"sha256",Sha256File(path)}});
    }
    WriteNew(run.directory/"run.json",record.dump(2)+'\n');
    WriteNew(run.directory/"config.ini",snapshot);
    return run;
}
void RecordStartupError(const RunContext& run,std::string_view error) {
    if (!run.directory.empty()) WriteNew(run.directory/"startup-error.txt",error);
}
void RecordEarlyStartupError(const fs::path& root,std::string_view error) {
    const auto directory=NewReportDirectory(fs::canonical(root),L"startup-error");
    WriteNew(directory/"startup-error.txt",std::string(error.substr(0,16384))+'\n');
}
}
