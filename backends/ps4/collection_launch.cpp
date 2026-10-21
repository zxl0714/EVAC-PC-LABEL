#include "collection_launch.h"
#include "collection_config.h"
#include "native_async_log.h"
#include "loader/prepared_cache.h"
#include "common/launch.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <map>
#include <memory>
#include <io.h>
#include <share.h>
#include <stdexcept>

namespace cave::ps4 {
namespace {
using Json=nlohmann::json;
fs::path active_report;
std::string active_game;
std::atomic_flag close_recorded{};
constexpr std::array<std::wstring_view,5> options{L"--collection_root",L"--game_id",L"--launch_contract",
    L"--game_data_root",L"--user_data_root"};
void Require(bool okay,const char* message) {if (!okay) throw std::runtime_error(message);}
unsigned ParseDiagnosticTap(std::wstring_view value) {
    if (value.empty()) return 0;
    Require(value.size()<=3 && value.find_first_not_of(L"0123456789")==value.npos,
        "CAVE_DIAGNOSTIC_INPUT_TAP_MS requires an integer from 0 to 500");
    unsigned result{};for (const auto digit:value) result=result*10+(digit-L'0');
    Require(result<=500,"CAVE_DIAGNOSTIC_INPUT_TAP_MS requires an integer from 0 to 500");
    return result;
}
unsigned DiagnosticTap() {
    wchar_t value[8]{};SetLastError(ERROR_SUCCESS);
    const auto length=GetEnvironmentVariableW(L"CAVE_DIAGNOSTIC_INPUT_TAP_MS",value,static_cast<DWORD>(std::size(value)));
    Require(length<std::size(value),"CAVE_DIAGNOSTIC_INPUT_TAP_MS is too long");
    Require(length || GetLastError()==ERROR_ENVVAR_NOT_FOUND || GetLastError()==ERROR_SUCCESS,
        "Cannot read CAVE_DIAGNOSTIC_INPUT_TAP_MS");
    return ParseDiagnosticTap(std::wstring_view(value,length));
}
bool ParseDiagnosticUnpaced(std::wstring_view value) {
    Require(value.empty() || value==L"0" || value==L"1", "CAVE_DIAGNOSTIC_UNPACED requires 0 or 1");
    return value==L"1";
}
bool DiagnosticUnpaced() {
    wchar_t value[4]{};SetLastError(ERROR_SUCCESS);
    const auto length=GetEnvironmentVariableW(L"CAVE_DIAGNOSTIC_UNPACED",value,static_cast<DWORD>(std::size(value)));
    Require(length<std::size(value),"CAVE_DIAGNOSTIC_UNPACED is too long");
    Require(length || GetLastError()==ERROR_ENVVAR_NOT_FOUND || GetLastError()==ERROR_SUCCESS,
        "Cannot read CAVE_DIAGNOSTIC_UNPACED");
    return ParseDiagnosticUnpaced(std::wstring_view(value,length));
}
std::uint64_t CreationTime() {
    FILETIME created{},exited{},kernel{},user{};
    Require(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=0,"Cannot identify game process");
    return (std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
}
void WriteNew(const fs::path& path,std::string_view text) {
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Require(file!=INVALID_HANDLE_VALUE,"Cannot create run diagnostic file");
    DWORD written{};const bool okay=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr) &&
        written==text.size() && FlushFileBuffers(file);CloseHandle(file);
    Require(okay,"Cannot write run diagnostic file");
}
void Redirect(FILE* stream,const fs::path& path) {
    WriteNew(path,{});FILE* reopened{};
    // freopen_s uses exclusive sharing. Keep a real CRT stream, but duplicate
    // a shared-read file descriptor so logs can be tailed while the game runs.
    Require(_wfreopen_s(&reopened,L"NUL",L"wb",stream)==0 && reopened,"Cannot initialize game log stream");
    auto* file=_wfsopen(path.c_str(),L"wb",_SH_DENYWR);
    Require(file!=nullptr,"Cannot open game log file");
    const auto duplicated=_dup2(_fileno(file),_fileno(stream));std::fclose(file);
    Require(duplicated==0,"Cannot redirect game log stream");
    std::setvbuf(stream,nullptr,_IONBF,0);
}
struct ProfileLock {
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit ProfileLock(const fs::path& directory) {
        fs::create_directories(directory);
        value=CreateFileW((directory/".profile.lock").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,
            nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
        Require(value!=INVALID_HANDLE_VALUE,"This game profile is in use or cannot be locked");
    }
    ~ProfileLock() {if (value!=INVALID_HANDLE_VALUE) CloseHandle(value);}
};
}
bool IsCollectionLaunch(int argc,wchar_t** argv) {
    for (int i=1;i<argc;++i) for (const auto name:options) {
        const std::wstring_view argument(argv[i]);
        if (argument==name || (argument.starts_with(name) && argument.size()>name.size() && argument[name.size()]==L'=')) return true;
    }
    return false;
}
bool CollectionInputDiagnosticSelfTest() {
    if (ParseDiagnosticTap(L"")!=0 || ParseDiagnosticTap(L"0")!=0 || ParseDiagnosticTap(L"120")!=120 ||
        ParseDiagnosticTap(L"500")!=500) return false;
    for (const auto value:{L"501",L"-1",L" 1",L"1.5",L"0000",L"１２"}) {
        try { (void)ParseDiagnosticTap(value);return false; } catch (const std::runtime_error&) {}
    }
    if (ParseDiagnosticUnpaced(L"") || ParseDiagnosticUnpaced(L"0") || !ParseDiagnosticUnpaced(L"1")) return false;
    for (const auto value:{L"true",L"2",L"-1",L"01",L" 1",L"１"}) {
        try { (void)ParseDiagnosticUnpaced(value);return false; } catch (const std::runtime_error&) {}
    }
    std::puts("{\"event\":\"collection_input_diagnostic_self_test\",\"passed\":true}");return true;
}
void RecordCollectionWindowClose() noexcept {
    if (active_report.empty() || close_recorded.test_and_set()) return;
    try {
        WriteNew(active_report/"exit.ini","[exit]\ngame = "+active_game+"\npid = "+std::to_string(GetCurrentProcessId())+
            "\nprocess_created = "+std::to_string(CreationTime())+
            "\ncode = 27\nreason = window_close_requested\npresentation_cleanup_complete = true\n"
            "guest_finalizers_run = false\nhost_cleanup_complete = false\n");
    } catch (...) { /* Preserve the original shutdown path even if disk is full. */ }
}
int RunCollection(int argc,wchar_t** argv,int (*command)(int,wchar_t**)) {
    try {
        const auto diagnostic_tap=DiagnosticTap();
        const auto diagnostic_unpaced=DiagnosticUnpaced();
        std::map<std::wstring,std::wstring> values;
        Require(argc==6,"Collection launch requires exactly five contract arguments");
        for (int i=1;i<argc;++i) {
            const std::wstring argument(argv[i]);const auto equal=argument.find(L'=');
            Require(equal!=argument.npos,"Invalid collection launch argument");
            const auto key=argument.substr(0,equal),value=argument.substr(equal+1);
            Require(std::find(options.begin(),options.end(),key)!=options.end() && !value.empty() && values.emplace(key,value).second,
                "Unknown, empty or repeated collection launch argument");
        }
        const auto game=Utf8(fs::path(values.at(L"--game_id")));
        Require((game=="ketsui" || game=="daioujou") && values.at(L"--launch_contract")==L"1","Unsupported game or launch contract");
        const bool ketsui=game=="ketsui";
        const fs::path requested_root(values.at(L"--collection_root")),requested_data(values.at(L"--game_data_root")),
            requested_profile(values.at(L"--user_data_root"));
        Require(requested_root.is_absolute() && requested_data.is_absolute() && requested_profile.is_absolute(),"Collection paths must be absolute");
        const auto root=fs::canonical(requested_root);
        const auto profile_parent=Resolve(root,root/"user-data"/game);
        Require(Resolve(root,requested_profile.parent_path())==profile_parent,"Profile must be under the selected game's user-data directory");
        const auto profile=Utf8(requested_profile.filename());
        const auto config=EnsureConfig(root);
        const auto snapshot=Ini::ReadText(config);
        const auto ini=Ini::Parse(snapshot);
        const auto configuration=ResolveCollectionConfiguration(ini,game);
        Require(!diagnostic_unpaced || (!configuration.shared.community_enabled && !configuration.shared.vsync && profile.starts_with("benchmark-")),
            "Unpaced diagnostic requires community.enabled=false, display.vsync=false and a benchmark-* profile");
        const auto spec=PrepareLaunch(FindGame(game),root,ini,profile,requested_data);
        Require(spec.paths.profile==Resolve(root,requested_profile),"Collection profile differs from the launch contract");
        GameSession session(root);ProfileLock lock(spec.paths.profile);
        fs::create_directories(spec.paths.logs);
        SYSTEMTIME now{};GetSystemTime(&now);wchar_t name[128]{};
        swprintf_s(name,L"run-%04u%02u%02uT%02u%02u%02u%03uZ-%lu",now.wYear,now.wMonth,now.wDay,now.wHour,
            now.wMinute,now.wSecond,now.wMilliseconds,GetCurrentProcessId());
        const auto report=spec.paths.logs/name;Require(fs::create_directory(report),"Run log directory already exists");
        active_game=game;active_report=report;
        WriteNew(report/"config.snapshot.ini",snapshot);
        Redirect(stdout,report/"stdout.jsonl");Redirect(stderr,report/"stderr.txt");
        SYSTEM_INFO machine{};GetNativeSystemInfo(&machine);
        const auto manifest=Ini::Load(root/"runtime/games"/game/"runtime-manifest.ini");
        const auto commit=manifest.Get("runtime","source_commit");
        const Json metadata{{"schema",1},{"game",game},{"launchContract",1},{"pid",GetCurrentProcessId()},
            {"processCreated",CreationTime()},{"sourceCommit",commit?commit->text:"unknown"},
            {"root",Utf8(root)},{"data",Utf8(spec.paths.data)},{"profile",Utf8(spec.paths.profile)},
            {"executable",Utf8(spec.executable)},{"executableSha256",Sha256(spec.executable)},
            {"gpuSha256",Sha256(root/"runtime/ps4/KetsuiGpuRuntime.dll")},
            {"logicalProcessors",machine.dwNumberOfProcessors},{"configSnapshot","config.snapshot.ini"},
            {"diagnosticInputTapMs",diagnostic_tap},
            {"diagnosticUnpacedPresentation",diagnostic_unpaced},
            {"community",{{"provider","configured"},{"requested",configuration.shared.community_enabled},
                {"supported",configuration.community_supported},{"enabled",configuration.community.enabled},
                {"endpoint",configuration.community.endpoint},{"nickname",configuration.community.nickname}}}};
        WriteNew(report/"run.json",metadata.dump(2)+'\n');
        std::puts(Json{{"event","collection_run_begin"},{"game",game},{"launch_contract",1}}.dump().c_str());
        const auto prepared=PreparePs4Game(root,spec.paths.data,game);
        WriteNew(report/"preparation.json",prepared.cache.receipt.dump(2)+'\n');
        const Json prepared_event{{"event","collection_prepared"},{"cacheHit",prepared.cache.reused},
            {"recipeSha256",prepared.cache.key},{"directory",Utf8(prepared.cache.directory)}};
        std::puts(prepared_event.dump().c_str());
        Require(Ini::ReadText(config)==snapshot,"config.ini changed during startup; restart with the saved configuration");
        std::vector<std::wstring> arguments{argv[0],prepared.executable.native(),L"--enter",
            L"--module-dir",prepared.modules.native(),L"--app-dir",spec.paths.data.native(),
            L"--user-dir",spec.paths.profile.native(),L"--allocation-timing",L"counts",
            L"--collection-config",config.native(),L"--collection-game",FromUtf8(game).wstring(),
            L"--community-provider",L"configured",
            L"--gpu-runtime",(root/"runtime/ps4/KetsuiGpuRuntime.dll").native()};
        if (ketsui) {
            arguments.insert(arguments.end(),{L"--read-patches",prepared.read_fixes.native(),L"--community-rounds",L"on"});
        } else {
            // The local community replay cache must also work with networking
            // disabled. The ranking peer fallback separately checks readiness.
            arguments.insert(arguments.end(),{L"--read-patches",prepared.read_fixes.native()});
        }
        if(prepared.cache.receipt.at("recipe").at("mergedApp").get<bool>())arguments.insert(arguments.end(),{L"--merged-app",L"on"});
        // Development-only bridge to the existing pad diagnostic. Never save
        // this setting in the player INI or enable it in the launcher itself.
        if (diagnostic_tap) {arguments.push_back(L"--diagnostic-tap-ms");arguments.push_back(std::to_wstring(diagnostic_tap));}
        if (diagnostic_unpaced) {arguments.insert(arguments.end(),{L"--diagnostic-unpaced",L"on"});}
        std::vector<wchar_t*> pointers;for (auto& argument:arguments) pointers.push_back(argument.data());
        Require(StartNativeAsyncLog(),"Could not start background diagnostics");
        const auto result=command(static_cast<int>(pointers.size()),pointers.data());
        (void)StopNativeAsyncLog();
        PrintNativeAsyncLogDiagnostics();
        WriteNew(report/"result.json",Json{{"event","collection_run_return"},{"exitCode",result}}.dump(2)+'\n');
        active_report.clear();return result;
    } catch (const std::exception& error) {
        (void)StopNativeAsyncLog();
        PrintNativeAsyncLogDiagnostics();
        std::fprintf(stderr,"Collection startup: %s\n",error.what());
        if (!active_report.empty()) try {WriteNew(active_report/"startup-error.txt",error.what());} catch (...) {}
        return 2;
    }
}
}
