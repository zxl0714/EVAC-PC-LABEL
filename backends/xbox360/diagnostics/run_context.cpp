#include "run_context.h"
#include "community/http_client.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <nlohmann/json.hpp>

namespace cave::xbox360 {
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
fs::path NewReportDirectory(const fs::path& root,std::string_view game,const wchar_t* prefix,const fs::path& data={}) {
    const auto paths=cave::Paths(root,game,data.empty()?root/"game-data"/cave::FromUtf8(game):data);
    cave::ValidateOutputs(paths);
    const auto& logs=paths.logs;
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
RunContext PrepareRun(const fs::path& requestedRoot,const BuildIdentity& build,const fs::path& requestedProfile,const fs::path& dataOverride) {
    RunContext run;
    run.game=build.game;
    run.root=fs::canonical(requestedRoot);
    const auto& identity=cave::FindGame(build.game);
    const auto configuration=run.root/"config.ini";
    const auto snapshot=fs::exists(configuration)?cave::Ini::ReadText(configuration):std::string(cave::DefaultConfig());
    const auto ini=cave::Ini::Parse(snapshot);
    run.settings=cave::ResolveSettings(ini,build.game);
    run.paths=cave::Paths(run.root,build.game,dataOverride.empty()?ini.DataDirectory(identity):dataOverride);
    if (!requestedProfile.empty()) run.paths.profile=cave::Resolve(run.root,requestedProfile);
    cave::ValidateOutputs(run.paths);
    const auto& game=run.paths.data;
    run.profile=run.paths.profile;
    const auto inspection=cave::Inspect(identity,run.root,game,cave::CheckLevel::Full);
    if (inspection.data_state!=cave::DataState::Verified)
        throw std::runtime_error(run.game+" data check failed: "+std::string(cave::Name(inspection.data_state))+": "+
            (inspection.problems.empty()?inspection.reason:inspection.problems.front()));
    const auto metadata=Json::parse(build.program_metadata);
    Json inputs=Json::array();
    for (const auto& item:metadata.at("xex")) {
        const auto name=item.at("path").get<std::string>();
        const auto path=cave::DataChild(game,name);
        if (fs::file_size(path)!=item.at("bytes").get<std::uint64_t>())
            throw std::runtime_error("Unsupported XEX size: "+name);
        const auto hash=Sha256File(path);
        if (hash!=item.at("sha256").get<std::string>()) throw std::runtime_error("Unsupported or modified XEX: "+name);
        inputs.push_back({{"path",name},{"sha256",hash},{"bytes",fs::file_size(path)}});
    }
    cave::EnsureConfig(run.root);
    if (cave::Ini::ReadText(configuration)!=snapshot)
        throw std::runtime_error("config.ini changed during startup; restart with the saved configuration");
    if (run.settings.community_enabled) {
        if (!cave::community::ValidEndpoint(run.settings.endpoint))
            throw std::runtime_error("[community].endpoint requires HTTPS or loopback HTTP and an API root without query/fragment");
        run.settings.nickname=cave::community::NormalizeNickname(run.settings.nickname);
        if (!cave::community::ValidNickname(run.settings.nickname))
            throw std::runtime_error("[community].nickname requires 1..48 UTF-8 bytes after normalization, without invisible controls");
    }
    if (const auto* dlc=ini.Get("game."+run.game,"dlc_dir");dlc && !dlc->text.empty()) {
        const auto source=cave::Resolve(run.root,cave::FromUtf8(dlc->text));
        if (fs::exists(source)) {
            if (!fs::is_directory(source)) throw std::runtime_error("DLC source must be a directory");
            auto protected_paths=run.paths;protected_paths.data=source;cave::ValidateOutputs(protected_paths);
            run.content_source=source;
        }
    }
    run.source_build=std::string(build.commit)+(build.dirty=="true"?"+dirty":"");
    run.game_session=std::make_shared<cave::GameSession>(run.root);
    fs::create_directories(run.profile);
    const auto lock=CreateFileW((run.profile/".profile.lock").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,
        nullptr,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    if (lock==INVALID_HANDLE_VALUE) throw std::runtime_error("Profile is already in use or cannot be locked");
    run.profile_lock=std::shared_ptr<void>(lock,[](void* handle) {CloseHandle(handle);});
    run.directory=NewReportDirectory(run.root,build.game,L"run",game);
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    Json record{{"format",1},{"game",build.game},{"launchContract",1},
        {"displayName",identity.name},{"sourceCommit",build.commit},{"sourceDirty",build.dirty=="true"},
        {"root",PathUtf8(run.root)},{"profile",PathUtf8(run.profile)},{"qpcFrequency",frequency.QuadPart},
        {"dataDirectory",PathUtf8(game)},{"contentSource",PathUtf8(run.content_source)},{"cacheDirectory",PathUtf8(run.paths.cache)},
        {"pid",GetCurrentProcessId()},{"processCodePage",GetACP()},
        {"gameInputs",inputs},{"configSha256",Sha256Text(snapshot)},
        {"communityEnabled",run.settings.community_enabled},{"keyboardMouseCapture",false},{"binaries",Json::array()}};
    std::array<wchar_t,32768> executable{};
    auto length=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
    if (!length || length>=executable.size()) throw std::runtime_error("Cannot determine executable path");
    const auto binaryRoot=fs::path(executable.data()).parent_path();
    for (const auto file:identity.runtime_files) {
        const auto relative=fs::path(file);
        const auto executablePath=(relative.parent_path()=="runtime/xbox360")?binaryRoot/relative.filename():run.root/relative;
        for (const auto& path:std::array{executablePath,fs::path(executablePath).replace_extension(".pdb")}) {
            if (fs::is_regular_file(path)) record["binaries"].push_back({{"path",PathUtf8(path)},
                {"bytes",fs::file_size(path)},{"sha256",Sha256File(path)}});
        }
    }
    WriteNew(run.directory/"run.json",record.dump(2)+'\n');
    WriteNew(run.directory/"config.ini",snapshot);
    return run;
}
void RecordStartupError(const RunContext& run,std::string_view error) {
    if (!run.directory.empty()) WriteNew(run.directory/"startup-error.txt",error);
}
void RecordEarlyStartupError(const fs::path& root,std::string_view game,std::string_view error) {
    const auto directory=NewReportDirectory(fs::canonical(root),game,L"startup-error");
    WriteNew(directory/"startup-error.txt",std::string(error.substr(0,16384))+'\n');
}
}
