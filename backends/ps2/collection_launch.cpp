#include "collection_launch.h"
#include "startup_trace.h"
#include "gs_crash_trace.h"
#include "crash_unwind.h"
#include "common/launch.h"
#include "common/ps2_prepare.h"
#include "common/verification_cache.h"
#include "common/settings.h"
#include "common/windows_fault_text.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <array>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <io.h>
#include <map>
#include <share.h>
#include <stdexcept>

namespace cave::ps2 {
namespace {
std::wstring crashText,crashDump;
LONG WINAPI Crash(EXCEPTION_POINTERS* exception) {
    const auto file=CreateFileW(crashText.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE) {
        gs_crash::Write(file);
        WriteWindowsFaultText(file,exception);FlushFileBuffers(file);
    }
    const auto dump=CreateFileW(crashDump.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    bool dumpOkay=false;DWORD dumpError=dump==INVALID_HANDLE_VALUE?GetLastError():0;
    if(dump!=INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),exception,FALSE};
        dumpOkay=MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),dump,
            MINIDUMP_TYPE(MiniDumpWithThreadInfo|MiniDumpWithUnloadedModules|MiniDumpWithProcessThreadData),&info,nullptr,nullptr)!=FALSE;
        if(!dumpOkay)dumpError=GetLastError();
        CloseHandle(dump);
    }
    if(file!=INVALID_HANDLE_VALUE) {
        char line[100]{};const auto n=std::snprintf(line,sizeof(line),"dump_ok=%u\ndump_error=%lu\n",unsigned(dumpOkay),dumpError);
        DWORD written{};if(n>0)WriteFile(file,line,DWORD(n),&written,nullptr);
        if(gs_crash::owner.load() && exception && exception->ContextRecord)
            WriteFaultingCallstack(file,*exception->ContextRecord);
        CloseHandle(file);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
void Require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void WriteNew(const fs::path& path,std::string_view bytes) {
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Require(file!=INVALID_HANDLE_VALUE,"Cannot create PS2 run log");
    DWORD written{};const bool okay=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr) && written==bytes.size();
    CloseHandle(file);Require(okay,"Cannot write PS2 run log");
}
void Redirect(FILE* stream,const fs::path& path) {
    WriteNew(path,{});FILE* reopened{};
    Require(_wfreopen_s(&reopened,L"NUL",L"wb",stream)==0 && reopened,"Cannot initialize PS2 logging");
    auto* file=_wfsopen(path.c_str(),L"wb",_SH_DENYWR);
    Require(file!=nullptr,"Cannot open PS2 log stream");
    const auto result=_dup2(_fileno(file),_fileno(stream));std::fclose(file);
    Require(result==0,"Cannot redirect PS2 log stream");
    std::setvbuf(stream,nullptr,_IONBF,0);
}
}
class CollectionLaunch::Impl {
public:
    LaunchSpec spec;
    Settings settings;
    std::string game;
    fs::path report,elf,disc,data,timings;
    std::unique_ptr<GameSession> session;
    HANDLE profileLock=INVALID_HANDLE_VALUE;
    LPTOP_LEVEL_EXCEPTION_FILTER previousFilter{};
    bool filterInstalled=false,completed=false;
    ~Impl() {
        if(filterInstalled)SetUnhandledExceptionFilter(previousFilter);
        if(profileLock!=INVALID_HANDLE_VALUE)CloseHandle(profileLock);
    }
};
CollectionLaunch::CollectionLaunch(std::unique_ptr<Impl> value):impl(std::move(value)){}
CollectionLaunch::~CollectionLaunch()=default;
std::unique_ptr<CollectionLaunch> CollectionLaunch::Parse(int argc,char** argv,const CollectionGame& game) {
    bool requested=false;
    for(int i=1;i<argc;++i)if(std::string_view(argv[i]).starts_with("--collection_root"))requested=true;
    if(!requested)return {};
    constexpr std::array<std::string_view,5> names{"--collection_root","--game_id","--launch_contract","--game_data_root","--user_data_root"};
    Require(argc==6,"Collection launch needs five contract arguments");
    std::map<std::string,std::string> options;
    for(int i=1;i<argc;++i) {
        const std::string argument=argv[i];const auto equal=argument.find('=');
        Require(equal!=argument.npos,"Invalid PS2 collection launch argument");
        const auto key=argument.substr(0,equal),value=argument.substr(equal+1);
        Require(std::find(names.begin(),names.end(),key)!=names.end() && !value.empty() && options.emplace(key,value).second,
                "Unknown or repeated PS2 launch argument");
    }
    Require(options.at("--game_id")==game.id && options.at("--launch_contract")=="1","Unsupported PS2 game or contract");
    const auto requestedRoot=FromUtf8(options.at("--collection_root"));
    const auto requestedData=FromUtf8(options.at("--game_data_root"));
    const auto requestedProfile=FromUtf8(options.at("--user_data_root"));
    Require(requestedRoot.is_absolute() && requestedData.is_absolute() && requestedProfile.is_absolute(),"PS2 collection paths must be absolute");
    const auto root=fs::canonical(requestedRoot);
    Require(Resolve(root,requestedProfile.parent_path())==Resolve(root,root/"user-data"/game.id),"PS2 profile must stay under its game directory");
    const auto snapshot=WithPackageDisplayDefaults(root,Ini::ReadText(EnsureConfig(root)));
    const auto ini=Ini::Parse(snapshot);
    auto value=std::make_unique<Impl>();
    value->game=game.id;
    value->settings=ResolveSettings(ini,game.id);
    value->spec=PrepareLaunch(FindGame(game.id),root,ini,Utf8(requestedProfile.filename()),requestedData);
    Require(value->spec.paths.profile==Resolve(root,requestedProfile),"PS2 profile differs from launch contract");
    const auto& paths=value->spec.paths;
    value->session=std::make_unique<GameSession>(root);
    fs::create_directories(paths.profile);
    value->profileLock=CreateFileW((paths.profile/".profile.lock").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,
        nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    Require(value->profileLock!=INVALID_HANDLE_VALUE,"PS2 profile is already in use");
    fs::create_directories(paths.logs);
    SYSTEMTIME now{};GetSystemTime(&now);wchar_t name[128]{};
    swprintf_s(name,L"run-%04u%02u%02uT%02u%02u%02u%03uZ-%lu",now.wYear,now.wMonth,now.wDay,
        now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,GetCurrentProcessId());
    value->report=paths.logs/name;
    Require(fs::create_directory(value->report),"PS2 run log already exists");
    WriteNew(value->report/"config.snapshot.ini",snapshot);
    WriteNew(value->report/"verification.txt",value->spec.verification+'\n');
    Redirect(stdout,value->report/"stdout.log");Redirect(stderr,value->report/"stderr.log");
    FILETIME created{},exited{},kernel{},user{};
    Require(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=FALSE,"Cannot record PS2 process identity");
    const auto creation=(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    WriteNew(value->report/"run.ini","[run]\ngame="+value->game+"\ncontract=1\npid="+std::to_string(GetCurrentProcessId())+
        "\nprocess_created="+std::to_string(creation)+
        "\ndata="+Utf8(paths.data)+"\nprofile="+Utf8(paths.profile)+"\n");
    crashText=(value->report/"crash.txt").native();crashDump=(value->report/"crash.dmp").native();
    value->previousFilter=SetUnhandledExceptionFilter(Crash);value->filterInstalled=true;
    StartupTrace("disc-prepare-begin");
    VerificationCache verification(root);
    const auto inspection=Inspect(FindGame(game.id),root,paths.data,CheckLevel::Programs,&verification);
    Require(inspection.data_state==DataState::Unverified && !inspection.disc_image.empty(),"Supported PS2 ISO is missing");
    value->disc=inspection.disc_image;
    value->data=PreparePs2Disc(paths,value->disc,game.disc_sha256,game.files);
    StartupTrace("disc-prepare-complete");
    value->elf=value->data/"boot.elf";
    value->timings=value->report/"frame-times.csv";
    return std::unique_ptr<CollectionLaunch>(new CollectionLaunch(std::move(value)));
}
const fs::path& CollectionLaunch::Elf()const{return impl->elf;}
const fs::path& CollectionLaunch::Disc()const{return impl->disc;}
const fs::path& CollectionLaunch::Data()const{return impl->data;}
const fs::path& CollectionLaunch::Profile()const{return impl->spec.paths.profile;}
const fs::path& CollectionLaunch::FrameTimes()const{return impl->timings;}
NativeHostOptions CollectionLaunch::HostOptions()const {
    NativeHostOptions result;
    result.width=impl->settings.window_width;result.height=impl->settings.window_height;
    result.fullscreen=impl->settings.fullscreen;result.vsync=impl->settings.vsync;
    result.amdAntiLag=impl->settings.amd_anti_lag;
    result.reflex=impl->settings.xbox360_reflex;
    result.gameId=impl->game;result.keyboardConfig=impl->report/"config.snapshot.ini";
    result.gamepadEnabled=impl->settings.gamepad_enabled;result.deadzone=float(impl->settings.deadzone);
    result.volume=float(impl->settings.volume);return result;
}
void CollectionLaunch::Complete(int code) {
    if(impl->completed)return;
    WriteNew(impl->report/"exit.ini","[exit]\ngame="+impl->game+"\ncode="+std::to_string(code)+
        "\nreason="+(code?"exception":"normal_exit")+"\n");impl->completed=true;
}
}
