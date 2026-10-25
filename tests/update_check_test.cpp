#include "launcher/update_check.h"
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
}
int main(int argc,char** argv) {
  try {
    if(argc==2 && std::string_view(argv[1])=="--live") {
      const auto result=update::Fetch();Require(result.okay && result.release.has_value(),"Live GitHub request failed");
      std::cout<<result.release->tag<<" "<<result.release->url<<'\n';return 0;
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
