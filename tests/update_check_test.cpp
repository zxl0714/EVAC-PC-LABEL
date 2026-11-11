#include "launcher/program_update.h"
#include "common/catalogue.h"
#include "common/launch.h"
#include "common/config_update.h"
#include "common/settings.h"
#include <Windows.h>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
using namespace cave;
namespace update=cave::launcher::update;
namespace {
unsigned checks{};
void Require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F> void Reject(F call,const char* message){bool rejected=false;try{call();}catch(...){rejected=true;}Require(rejected,message);}
update::Release Release(std::string tag) {return {tag,std::string(update::kReleasePrefix)+tag,"Authored release notes"};}
std::string AssetUrl(std::string_view tag) {
  return "https://github.com/zxl0714/EVAC-PC-LABEL/releases/download/"+std::string(tag)+
         "/EVAC-PC-LABEL-"+std::string(tag)+"-win-x64.zip";
}
void Write(const fs::path& path,std::string_view text) {
  fs::create_directories(path.parent_path());
  std::ofstream file(path,std::ios::binary);
  if(!(file<<text))throw std::runtime_error("Cannot write update fixture");
}
void Package(const fs::path& root,std::string_view version,std::string_view value) {
  const std::pair<std::string_view,std::string_view> files[]{
    {"runtime/first.bin","runtime"},{"runtime/second.bin","runtime"},{"EVAC PC LABEL.exe","runtime"},
    {"game-data/futari/README.txt","documentation"}};
  std::string manifest=R"({"format":3,"flavor":"Public","platform":"win-x64","archiveLayout":"flat","sourceIncluded":false,"dataGames":[],"futariContent":[],"symbolsIncluded":false,"version":")";
  manifest+=std::string(version)+R"(","files":[)";
  std::string sums;
  for(const auto& [name,role]:files) {
    const auto file=root/name;
    Write(file,value);
    const auto digest=Sha256(file);
    if(!sums.empty())manifest+=",";
    sums+=digest+"  "+std::string(name)+"\n";
    manifest+=R"({"path":")"+std::string(name)+R"(","sha256":")"+digest+
              R"(","role":")"+std::string(role)+R"(","bytes":)"+std::to_string(value.size())+"}";
  }
  Write(root/"release.json",manifest+"]}");
  Write(root/"SHA256SUMS.txt",sums);
}
void Zip(const fs::path& archive,const fs::path& content) {
  wchar_t system[MAX_PATH]{};
  Require(GetSystemDirectoryW(system,MAX_PATH)>0,"Windows tar path");
  const auto tar=fs::path(system)/"tar.exe";
  auto arguments=QuoteArgument(tar.native())+L" -a -cf "+QuoteArgument(archive.native())+L" -C "+QuoteArgument(content.native())+L" .";
  STARTUPINFOW startup{};startup.cb=sizeof(startup);
  PROCESS_INFORMATION process{};
  Require(CreateProcessW(tar.c_str(),arguments.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"Start fixture tar");
  CloseHandle(process.hThread);
  DWORD code{};
  const auto okay=WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess,&code) && !code;
  CloseHandle(process.hProcess);
  Require(okay,"Create fixture ZIP");
}
void ProgramInstall(const fs::path& parent) {
  const auto installed=parent/"installed",incoming=parent/"incoming";
  Package(installed,"0.9.8","old");Package(incoming,"0.9.9","new");
  const auto config=EnsureConfig(installed);
  const auto config_before=Sha256(config);
  Write(installed/"game-data/futari/game.bin","game data");
  Write(installed/"user-data/futari/default/save.bin","player save");
  Write(installed/"runtime/custom.dll","custom file");
  auto release=Release("v0.9.9");
  const auto zip=parent/"program.zip";
  Zip(zip,incoming);
  release.program=update::ProgramAsset{AssetUrl(release.tag),Sha256(zip),fs::file_size(zip)};
  const auto cache=update::UpdateStorage(installed);
  fs::create_directories(cache);
  const auto cached=cache/(release.tag+"-"+release.program->sha256+".zip");
  fs::copy_file(zip,cached);
  Require(update::PackagedInstallation(installed,"0.9.8") && !update::PackagedInstallation(installed,"0.9.9"),"Installed program identity");
  update::DownloadProgram(installed,release);
  Require(Sha256(cached)==release.program->sha256,"Verified cache was replaced");
  {
    std::fstream tamper(cached,std::ios::in|std::ios::out|std::ios::binary);
    tamper.put('X');
  }
  Reject([&]{update::InstallProgram(installed,release);},"Tampered ZIP was accepted");
  Require(!update::RecoveryPending(installed) && Sha256(config)==config_before,"Tampered ZIP modified installation");
  fs::copy_file(zip,cached,fs::copy_options::overwrite_existing);
  {
    const auto path=installed/"runtime/second.bin";
    const auto locked=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Require(locked!=INVALID_HANDLE_VALUE,"Lock update destination");
    struct Unlock {HANDLE handle;~Unlock(){CloseHandle(handle);}} unlock{locked};
    bool failed_at_lock=false;
    try {update::InstallProgram(installed,release);}
    catch(const std::exception& error){failed_at_lock=std::string_view(error.what()).starts_with("Cannot replace second.bin");}
    Require(failed_at_lock,"Update did not reach the locked destination");
    Require(!update::RecoveryPending(installed) && Ini::ReadText(installed/"runtime/first.bin")=="old" &&
            Ini::ReadText(path)=="old","Rollback left files from a failed update");
  }
  const auto first=installed/"runtime/first.bin";
  const auto backup=cache/"transaction/backup/runtime/first.bin";
  Write(backup,"old");
  Write(first,"new");
  Write(cache/"pending.json",R"({"schema":1,"files":[{"name":"runtime/first.bin","role":"runtime","before":")"+
        Sha256(backup)+R"(","incoming":")"+Sha256(first)+R"("}]})");
  Require(update::RecoveryPending(installed),"Interrupted installation was not detected");
  update::RecoverProgram(installed);
  Require(!update::RecoveryPending(installed) && Ini::ReadText(first)=="old","Interrupted installation did not restore the old file");
  update::InstallProgram(installed,release);
  Require(update::PackagedInstallation(installed,"0.9.9") && !update::RecoveryPending(installed) &&
          Ini::ReadText(first)=="new" && Ini::ReadText(installed/"EVAC PC LABEL.exe")=="new","Retry did not install the updated program");
  Require(Sha256(config)==config_before && Ini::ReadText(installed/"game-data/futari/game.bin")=="game data" &&
          Ini::ReadText(installed/"user-data/futari/default/save.bin")=="player save" &&
          Ini::ReadText(installed/"runtime/custom.dll")=="custom file","Update modified player or unmanaged files");
}
}
int main(int argc,char** argv) {
  try {
    if(argc==2 && std::string_view(argv[1])=="--live") {
      const auto result=update::Fetch();Require(result.okay && result.release.has_value(),"Live GitHub request failed");
      std::cout<<result.release->tag<<" "<<result.release->url<<'\n';return 0;
    }
    if(argc==3 && std::string_view(argv[1])=="--live-download") {
      const auto result=update::Fetch();
      Require(result.okay && result.release && update::TrustedProgram(*result.release),"Live release has no verified Windows program");
      const auto root=fs::canonical(FromUtf8(argv[2]));
      update::DownloadProgram(root,*result.release);
      update::State state;
      update::Apply(state,result,update::Now());
      update::Save(root,state);
      std::cout<<result.release->tag<<" "<<result.release->program->bytes<<" "<<result.release->program->sha256<<'\n';
      return 0;
    }
    for(const auto* value:{"v0.7","0.7.0","v0.7.0.0"})Require(update::ParseVersion(value)==update::Version{0,7,0,0},"Numeric version normalization");
    for(const auto* value:{"","v","dev","v1.2-rc1","v1","1.2.","1.2.3.4.5","1.4294967296","1.-2","1. 2","1.2/x"})Require(!update::ParseVersion(value),"Malformed/development version accepted");
    Require(update::Newer("0.7.9",Release("v0.7.10")),"Version compared lexicographically");
    Require(!update::Newer("dev",Release("v9.0")) && !update::Newer("0.7.2",Release("v0.7.2")) && !update::Newer("0.8",Release("v0.7.9")),"Same/old/unknown version announced");
    for(const auto* url:{"http://github.com/zxl0714/EVAC-PC-LABEL/releases/tag/v1.0","https://github.com.evil.test/x","https://github.com/other/repo/releases/tag/v1.0","https://github.com/zxl0714/EVAC-PC-LABEL/releases/tag/v1.0?redirect=1"}) {
      auto release=Release("v1.0");release.url=url;Require(!update::Trusted(release),"Untrusted URL accepted");
    }
    const std::string response=R"({"draft":false,"prerelease":false,"tag_name":"v0.8","html_url":"https://github.com/zxl0714/EVAC-PC-LABEL/releases/tag/v0.8","body":"Release notes\nHello"})";
    Require(update::ParseRelease(response)->notes=="Release notes\nHello","Release response parse");
    const auto digest=std::string(64,'a');
    const auto asset_response=response.substr(0,response.size()-1)+
      R"(,"assets":[{"name":"EVAC-PC-LABEL-v0.8-win-x64.zip","state":"uploaded","size":123,"digest":"sha256:)"+digest+
      R"(","browser_download_url":")"+AssetUrl("v0.8")+R"("}]})";
    const auto program_release=update::ParseRelease(asset_response);
    Require(program_release && update::TrustedProgram(*program_release) && program_release->program->sha256==digest,
            "GitHub release ZIP digest was not recognized");
    const std::pair<std::string_view,std::string_view> invalid_assets[]{
      {"sha256:","sha1:"},{"size\":123","size\":1073741825"},{"win-x64.zip","win-x64-internal.zip"},
      {"/releases/download/","/elsewhere/"},{"uploaded","new"}};
    for(const auto& [from,to]:invalid_assets) {
      auto modified=asset_response;
      modified.replace(modified.find(from),from.size(),to);
      Require(!update::ParseRelease(modified)->program,"Invalid program asset accepted");
    }
    auto prerelease=response;prerelease.replace(prerelease.find("false",prerelease.find("prerelease")),5,"true");
    Require(!update::ParseRelease(prerelease),"Prerelease announced");
    auto draft=response;draft.replace(draft.find("false"),5,"true");Require(!update::ParseRelease(draft),"Draft announced");
    for(const auto& body:std::array<std::string,4>{"{","{}",std::string(129*1024,'x'),R"({"draft":"false","prerelease":false})"})Reject([&]{update::ParseRelease(body);},"Malformed response accepted");
    Require(update::Enabled(Ini::Parse("")) && update::Enabled(Ini::Parse("[community]\nenabled=false\n")),"Updates tied to community");
    Require(!update::Enabled(Ini::Parse("[collection]\ncheck_for_updates=false\n")),"Automatic disable ignored");
    update::State state;constexpr int64_t now=1800000000;
    Require(update::Due(state,now),"Initial check missing");
    update::Apply(state,{true,Release("v0.8"),{}},now);
    Require(!update::Due(state,now+86399) && update::Due(state,now+86400),"Successful cache TTL");
    Require(update::ShouldNotify(state,"0.7.3"),"Fresh version not announced");
    Require(update::ShouldNotify(state,"0.7.3"),"Polling marked an unseen reminder");
    state.notified.insert("v0.8");Require(!update::ShouldNotify(state,"0.7.3"),"Shown version repeated");
    update::Apply(state,{false,{},"offline"},now+86400);
    Require(!update::Due(state,now+86500) && update::Due(state,now+90000) && state.latest->tag=="v0.8","Failure backoff lost previous result");
    update::Apply(state,{true,Release("v0.9"),{}},now+90000);Require(update::ShouldNotify(state,"0.7.3"),"Next version suppressed");
    state.notified.insert("v0.9");update::Apply(state,{true,Release("v0.8"),{}},now+90001);
    Require(!update::ShouldNotify(state,"0.7.3"),"Reverted release was reannounced");
    Require(update::Due(state,now-5000),"Clock correction suppressed checks indefinitely");
    const auto root=fs::temp_directory_path()/("evac-update-check-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    Require(fs::create_directory(root),"Fresh fixture root");
    struct Cleanup {fs::path root;~Cleanup(){std::error_code error;fs::remove_all(root,error);}} cleanup{root};
    EnsureConfig(root);const auto original=Ini::ReadText(ConfigPath(root));
    update::Save(root,state);const auto loaded=update::Load(root);
    Require(loaded.notified==state.notified && loaded.latest->tag==state.latest->tag && loaded.checked==state.checked,"Cache cold restart");
    Require(!update::ShouldNotify(loaded,"0.7.3"),"Cold restart repeated reminder");
    Require(Ini::ReadText(ConfigPath(root))==original,"Cache modified player config");
    state.latest=program_release;
    update::Save(root,state);
    Require(update::Load(root).latest->program==program_release->program,"Cache lost program asset digest");
    ProgramInstall(root);
    {std::ofstream file(root/"user-data/update-check.json");file<<"not json";}
    Require(update::Load(root).notified.empty(),"Corrupt cache broke startup");
    {std::ofstream file(root/"user-data/update-check.json",std::ios::binary);file<<std::string(256*1024+1,' ');}
    Require(update::Load(root).notified.empty(),"Oversized cache accepted");
    std::promise<void> unblock,ended;auto gate=unblock.get_future().share();auto done=ended.get_future();
    const auto started=std::chrono::steady_clock::now();
    {update::Request pending([gate,&ended]{gate.wait();ended.set_value();return update::Result{false,{},"fixture"};});Require(!pending.Poll(),"Request completed before fixture allowed it");}
    Require(std::chrono::steady_clock::now()-started<std::chrono::milliseconds(200),"Closing launcher waited for network");
    unblock.set_value();Require(done.wait_for(std::chrono::seconds(2))==std::future_status::ready,"Detached worker did not finish");
    update::Request pending([]{return update::Result{true,Release("v0.8"),{}};});
    std::optional<update::Result> finished;
    for(unsigned n=0;n<200 && !finished;++n){finished=pending.Poll();if(!finished)std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    Require(finished && finished->okay && finished->release->tag=="v0.8" && !pending.Poll(),"Worker result delivery");
    std::cout<<checks<<" update-check checks passed; no network used\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
