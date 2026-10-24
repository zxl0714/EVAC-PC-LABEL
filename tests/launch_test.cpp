#include "common/launch.h"
#include "common/data_import.h"
#include "common/settings.h"
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace cave;
namespace {
int checks{};
void Require(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void Reject(F action,const char* reason) {
  bool rejected{};try {action();}catch(const std::exception&) {rejected=true;}Require(rejected,reason);
}
void Write(const fs::path& path,std::string_view value) {
  fs::create_directories(path.parent_path());std::ofstream output(path,std::ios::binary);output<<value;
  if(!output)throw std::runtime_error("Cannot write synthetic fixture");
}
struct Fixture {
  fs::path temporary,root,data;
  std::wstring nonce;
  Fixture() {
    std::array<unsigned char,16> bytes{};
    if(BCryptGenRandom(nullptr,bytes.data(),ULONG(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)
      throw std::runtime_error("Cannot name fixture");
    for(auto byte:bytes)nonce+=std::to_wstring(byte)+L"-";
    temporary=fs::canonical(fs::temp_directory_path())/(L"cave-launch-test-"+nonce);
    if(!fs::create_directory(temporary))throw std::runtime_error("Fixture collision");
    root=temporary/FromUtf8("合集 空格");data=temporary/FromUtf8("只读 游戏数据");
    fs::create_directories(root);fs::create_directories(data);
  }
  ~Fixture() {
    // Remove only this exact, randomly named test directory, never a caller path.
    if(temporary.parent_path()==fs::canonical(fs::temp_directory_path()) &&
       temporary.filename().native().starts_with(L"cave-launch-test-")) {
      std::error_code error;fs::remove_all(temporary,error);
    }
  }
};
struct Event {
  HANDLE value{};
  explicit Event(const std::wstring& name):value(CreateEventW(nullptr,TRUE,FALSE,name.c_str())) {
    if(!value)throw std::runtime_error("Cannot create fixture event");
  }
  ~Event(){CloseHandle(value);}
};
int Child(int count,wchar_t** arguments) {
  if(count<8)return 2;
  GameSession session(arguments[2]);
  std::ofstream output(fs::path(arguments[5]),std::ios::binary);
  output<<Utf8(fs::current_path())<<'\n';
  for(int index=7;index<count;++index) {
    const auto text=Utf8(arguments[index]);output<<text.size()<<':'<<text<<'\n';
  }
  output.close();if(!output)return 3;
  HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,arguments[3]);
  HANDLE release=OpenEventW(SYNCHRONIZE,FALSE,arguments[4]);
  if(!ready || !release)return 4;
  SetEvent(ready);CloseHandle(ready);
  const auto waited=WaitForSingleObject(release,10000);CloseHandle(release);
  if(waited!=WAIT_OBJECT_0)return 5;
  return std::stoi(arguments[6]);
}
void ConfigChecks(Fixture& fixture) {
  const auto& game=FindGame("espgaluda2");
  const auto config=EnsureConfig(fixture.root);
  const std::string original="\xef\xbb\xbf; comments stay\r\n[Collection]\r\nconfig_version = 1\r\n"
      "[game.ESPGALUDA2]\r\n  DATA_DIR = old\r\n; section comment\r\n[future]\r\nkey = untouched\r\n";
  Write(config,original);SetDataDirectory(fixture.root,game,fixture.data);
  auto expected=original;expected.replace(expected.find("old"),3,Utf8(fixture.data));
  Require(Ini::ReadText(config)==expected,"Picker altered comments, BOM, case or line endings");
  Require(Ini::Load(config).DataDirectory(game)==fixture.data,"Picker path did not read back");
  for(const auto* text:{"[game.espgaluda2]","[game.espgaluda2]\n; keep\n[future]\nx=1\n","[future]\nx=1"}) {
    Write(config,text);SetDataDirectory(fixture.root,game,fixture.data);
    Require(Ini::Load(config).DataDirectory(game)==fixture.data,"Picker failed to insert directory key/section");
  }
  auto before=Ini::ReadText(config);
  Reject([&]{SetDataDirectory(fixture.root,game,fixture.root);},"Picker accepted read/write overlap");
  Require(Ini::ReadText(config)==before,"Rejected selection modified config.ini");
  Reject([&]{SetDataDirectory(fixture.root,game,fixture.data/"absent");},"Picker accepted nonexistent directory");
  const auto local=fixture.root/"local data";fs::create_directory(local);
  SetDataDirectory(fixture.root,game,local);
  Require(Ini::Load(config).DataDirectory(game)=="local data","Collection-local path was not portable");
  SetDataDirectory(fixture.root,game,fixture.data);
  for(const auto& item:fs::directory_iterator(config.parent_path()))
    Require(!item.path().filename().native().starts_with(L"config.ini.pending-"),"Configuration temporary leaked");
}
void LaunchChecks(Fixture& fixture,const fs::path& executable) {
  constexpr std::string_view hash="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  const std::array files{DataFile{"main.bin",3,hash,true}};
  const std::array<std::string_view,2> runtime{"runtime/fake.exe","runtime/games/espgaluda2/runtime-manifest.ini"};
  Game game{"espgaluda2","Synthetic fixture","xbox360","game-data/espgaluda2","runtime/fake.exe",true,false,files,runtime};
  Write(fixture.data/"main.bin","abc");Write(fixture.root/"runtime/fake.exe","synthetic; never executed");
  const auto certificate=fixture.root/runtime[1];
  const auto manifest="[runtime]\ncontract=1\ngame=espgaluda2\n[files]\nruntime/fake.exe="+Sha256(fixture.root/runtime[0])+"\n";
  Write(certificate,manifest);
  auto ini=Ini::Load(ConfigPath(fixture.root));
  auto spec=PrepareLaunch(game,fixture.root,ini,"launch-test");
  Require(spec.arguments.size()==5 && spec.configuration==fixture.root/"user-data/config.ini","Launch contract/config identity");
  Require(spec.paths.data==fixture.data && spec.paths.profile==fixture.root/"user-data/espgaluda2/launch-test","Resolved launch paths");
  Require(spec.arguments[0]==L"--collection_root="+fixture.root.native() && spec.arguments[2]==L"--launch_contract=1","Launch contract arguments");
  Reject([&]{PrepareLaunch(game,fixture.root,ini,"../bad");},"Profile traversal accepted");
  Write(certificate,manifest+"runtime/unexpected.dll=abc\n");
  Reject([&]{PrepareLaunch(game,fixture.root,ini);},"Extra runtime manifest file accepted");
  Write(certificate,manifest);Write(fixture.root/runtime[0],"modified");
  Reject([&]{PrepareLaunch(game,fixture.root,ini);},"Altered runtime accepted");
  Write(fixture.root/runtime[0],"synthetic; never executed");
  game.coming_soon=true;Reject([&]{PrepareLaunch(game,fixture.root,ini);},"Coming Soon launched");game.coming_soon=false;
  game.integrated=false;Reject([&]{PrepareLaunch(game,fixture.root,ini);},"Unintegrated game launched");game.integrated=true;
  game.backend="ps4";Require(PrepareLaunch(game,fixture.root,ini).arguments==PrepareLaunch(Game{"espgaluda2","Synthetic fixture","xbox360","game-data/espgaluda2","runtime/fake.exe",true,false,files,runtime},fixture.root,ini).arguments,"PS4 launch contract differs");
  game.backend="undetermined";Reject([&]{PrepareLaunch(game,fixture.root,ini);},"Unimplemented backend launched");game.backend="xbox360";
  const auto helper=fixture.root/FromUtf8("工具 空格/child helper.exe");
  fs::create_directories(helper.parent_path());fs::copy_file(executable,helper);
  const std::vector<std::wstring> payload{L"",L"simple",L"two words",L"中文 路径",L"\"quoted\"",L"trailing\\",L"before\\\"quote",L"& | ; < > %PATH%",L"a\tb"};
  for(const auto code:{0UL,27UL,37UL,259UL}) {
    const auto name=L"Local\\CaveCollection.Test."+fixture.nonce+std::to_wstring(code);
    const auto ready_name=name+L".ready",release_name=name+L".release";
    Event ready(ready_name),release(release_name);
    spec.executable=helper;const auto output=fixture.root/(std::to_string(code)+".txt");
    spec.arguments={L"--fixture-child",fixture.root.native(),ready_name,release_name,output.native(),std::to_wstring(code)};
    spec.arguments.insert(spec.arguments.end(),payload.begin(),payload.end());
    Require(!GameIsRunning(fixture.root),"Prior game lock leaked");
    auto child=StartGame(spec);
    if(WaitForSingleObject(ready.value,5000)!=WAIT_OBJECT_0){SetEvent(release.value);child.Wait();throw std::runtime_error("Fixture child did not initialize");}
    Require(!child.ExitCode() && child.pid()!=0,"Live child reported exited");
    Require(GameIsRunning(fixture.root/"."),"Game lock was not shared across processes/root aliases");
    Reject([&]{StartGame(spec);},"A second game was permitted");
    Reject([&]{GameSession another(fixture.root);},"Second process ignored game mutex");
    std::string expected=Utf8(fixture.root)+"\n";
    for(const auto& text:payload){const auto utf8=Utf8(text);expected+=std::to_string(utf8.size())+":"+utf8+"\n";}
    Require(Ini::ReadText(output)==expected,"Command quoting, Unicode or working directory changed");
    SetEvent(release.value);child.Wait();Require(child.ExitCode()==code,"Child exit code changed");
    Require(!child.RequestedWindowClose(fixture.root,"ketsui",code),"Non-close exit accepted as requested close");
    Require(!child.RequestedWindowClose(fixture.root,"daioujou",code),"Non-close DaiOuJou exit accepted as requested close");
    Require(!child.RequestedWindowClose(fixture.root,"esprade",code),"Non-close ESP Ra.De. exit accepted as requested close");
    if(code==27) {
      const auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,child.pid());
      FILETIME created{},ended{},kernel{},user{};
      Require(process && GetProcessTimes(process,&created,&ended,&kernel,&user),"Cannot identify fixture child birth");
      CloseHandle(process);
      const auto birth=std::to_string((std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime);
      for(const auto title:{"ketsui","daioujou","esprade"}) {
        const auto receipt=fixture.root/"logs"/title/("run-authoredZ-"+std::to_string(child.pid()))/"exit.ini";
        const auto text="[exit]\ngame="+std::string(title)+"\npid="+std::to_string(child.pid())+
            "\nprocess_created="+birth+"\ncode=27\nreason=window_close_requested\n"
            "presentation_cleanup_complete=true\nguest_finalizers_run=false\nhost_cleanup_complete=false\n";
        Write(receipt,text);
        Require(child.RequestedWindowClose(fixture.root,title,code),"Exact title close receipt not recognized");
        Write(receipt,text+"unrecognized=retained\n");
        Require(!child.RequestedWindowClose(fixture.root,"sdoj",code),"PS4 close receipt accepted for 360 title");
        auto wrong=text;wrong.replace(wrong.find(birth),birth.size(),"0");Write(receipt,wrong);
        Require(!child.RequestedWindowClose(fixture.root,title,code),"Close receipt from another process birth accepted");
      }
    }
    Require(!GameIsRunning(fixture.root),"Exited child kept game lock");
    ChildProcess moved(std::move(child));Require(moved.ExitCode()==code,"Moving process handle lost status");
  }
}
Game SyntheticCopyGame() {
  constexpr std::string_view hash="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  static const std::array files{DataFile{"main.bin",3,hash,true}};
  return {"fixture","Synthetic data","xbox360","game-data/fixture","unused.exe",true,false,files,{}};
}
void CopyChecks() {
  constexpr std::string_view hash="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  auto game=SyntheticCopyGame();
  for(int scenario=0;scenario<9;++scenario) {
    Fixture fixture;Write(fixture.data/"main.bin","abc");Write(fixture.data/"res/extra.dat","extra");
    Write(fixture.data/"zero.bin","");fs::create_directories(fixture.data/"empty directory");
    const auto target=fixture.root/"game-data/fixture",config=EnsureConfig(fixture.root);
    Write(config,"; preserved\n[future]\nvalue=keep\n");const auto config_hash=Sha256(config);
    bool cancel=false,changed=false;
    struct RestoreAttributes {
      fs::path file;DWORD attributes{};
      ~RestoreAttributes(){if(!file.empty())SetFileAttributesW(file.c_str(),attributes);}
    } restore;
    if(scenario==8) {
      restore.file=fixture.data/"main.bin";restore.attributes=GetFileAttributesW(restore.file.c_str());
      Require(SetFileAttributesW(restore.file.c_str(),restore.attributes|FILE_ATTRIBUTE_READONLY),"Cannot create readonly source fixture");
    }
    if(scenario==1)fs::create_directories(target);
    if(scenario==2)Write(target/"existing.txt","preserve this");
    if(scenario==3)Write(fixture.data/"main.bin","xyz");
    if(scenario==4)cancel=true;
    auto progress=[&](uint64_t done,uint64_t total) {
      if(!done || changed)return;
      if(scenario==5 || scenario==8){cancel=true;changed=true;}
      if(scenario==6 && done==total){Write(fixture.data/"res/extra.dat","edits");changed=true;}
      if(scenario==7 && done==total){fs::rename(fixture.data/"res/extra.dat",fixture.data/"res/renamed.dat");changed=true;}
    };
    auto copy=[&]{return CopyDataToCollection(fixture.root,game,fixture.data,progress,[&]{return cancel;});};
    if(scenario<2) {
      const auto result=copy();
      Require(result.directory==target && result.files==3 && result.bytes==8,"Copied inventory/byte count differs");
      Require(Ini::Load(config).DataDirectory(game)=="game-data/fixture" &&
              Resolve(fixture.root,Ini::Load(config).DataDirectory(game))==target,"Copied path is not portable/root-relative");
      Require(Ini::ReadText(config).starts_with("; preserved\n[future]\nvalue=keep\n"),"Copy rewrote existing configuration");
      Require(Sha256(target/"main.bin")==hash && Ini::ReadText(target/"res/extra.dat")=="extra" &&
              fs::file_size(target/"zero.bin")==0 && fs::is_directory(target/"empty directory"),"Copy lost data or empty directories");
      Require(Sha256(fixture.data/"main.bin")==hash && Ini::ReadText(fixture.data/"res/extra.dat")=="extra","Copy modified source bytes");
    } else {
      Reject(copy,"Unsafe, corrupt or cancelled copy was published");
      Require(Sha256(config)==config_hash,"Failed copy changed configuration");
      if(scenario==2)Require(Ini::ReadText(target/"existing.txt")=="preserve this","Nonempty target was overwritten");
      else Require(!fs::exists(target),"Failed copy left an installed target");
    }
    if(fs::exists(target.parent_path()))for(const auto& item:fs::directory_iterator(target.parent_path()))
      Require(!item.path().filename().native().starts_with(L".copy-"),"Owned copy staging directory leaked");
    if(scenario==8)Require(GetFileAttributesW(restore.file.c_str())&FILE_ATTRIBUTE_READONLY,"Source read-only attribute changed");
    Reject([&]{CopyDataToCollection(fixture.root,game,target);},"Source/target identity accepted");
  }
  Fixture overlap;Write(overlap.data/"main.bin","abc");
  Reject([&]{CopyDataToCollection(overlap.root,game,overlap.root);},"Copy accepted source containing target");
  game.coming_soon=true;Reject([&]{CopyDataToCollection(overlap.root,game,overlap.data);},"Coming Soon copy was accepted");
}
void PlacementGuideCopyChecks() {
  const auto game=SyntheticCopyGame();
  const fs::path guide=L"README.txt";
  const std::string text="Placement instructions\nKeep this note unchanged.\n";
  for(bool same_source_guide:{false,true}) {
    Fixture fixture;Write(fixture.data/"main.bin","abc");
    if(same_source_guide)Write(fixture.data/guide,text);
    const auto target=fixture.root/"game-data/fixture";Write(target/guide,text);
    const auto original_attributes=GetFileAttributesW((target/guide).c_str());
    Require(SetFileAttributesW((target/guide).c_str(),original_attributes|FILE_ATTRIBUTE_READONLY),"Mark placement-only target guide read-only");
    const auto result=CopyDataToCollection(fixture.root,game,fixture.data);
    Require(result.directory==target && result.files==(same_source_guide?2u:1u),"Guide-aware import changed the source inventory count");
    Require(Ini::ReadText(target/guide)==text && Ini::ReadText(target/"main.bin")=="abc","Import did not preserve the existing placement guide");
    Require(Within(result.guide_backup,fixture.root/"cache") && Ini::ReadText(result.guide_backup/guide)==text,
            "Original placement directory was not retained as a recoverable backup");
    Require(GetFileAttributesW((result.guide_backup/guide).c_str())&FILE_ATTRIBUTE_READONLY,"Import changed original guide attributes");
    Require(Inspect(game,fixture.root,target,CheckLevel::Full).data_state==DataState::Verified &&
            Ini::ReadText(fixture.data/"main.bin")=="abc","Guide-aware import did not validate or modified source data");
    // Reset only the test-owned backup attribute for fixture cleanup.
    Require(SetFileAttributesW((result.guide_backup/guide).c_str(),original_attributes),"Reset fixture backup attributes");
  }
  for(bool conflicting_source_guide:{false,true}) {
    Fixture fixture;Write(fixture.data/"main.bin","abc");
    const auto target=fixture.root/"game-data/fixture";Write(target/guide,text);
    if(conflicting_source_guide)Write(fixture.data/guide,"Different source guide\n");
    else Write(target/"personal-notes.txt","Preserve unrelated files");
    const auto config=EnsureConfig(fixture.root);const auto before=Sha256(config);
    Reject([&]{CopyDataToCollection(fixture.root,game,fixture.data);},"Guide exception accepted unrelated or conflicting content");
    Require(Ini::ReadText(target/guide)==text && !fs::exists(target/"main.bin") && Sha256(config)==before,
            "Rejected guide import changed the target or configuration");
    if(!conflicting_source_guide)Require(Ini::ReadText(target/"personal-notes.txt")=="Preserve unrelated files","Unrelated file was overwritten");
    for(const auto& item:fs::directory_iterator(target.parent_path()))
      Require(!item.path().filename().native().starts_with(L".copy-"),"Rejected guide import leaked owned staging data");
  }
}
}
int wmain(int count,wchar_t** arguments) {
  try {
    if(count>1 && std::wstring_view(arguments[1])==L"--fixture-child")return Child(count,arguments);
    if(count==4 && std::wstring_view(arguments[1])==L"--copy-fixture") {
      // Exposes only an authored abc/main.bin fixture for Python junction tests.
      const auto result=CopyDataToCollection(arguments[2],SyntheticCopyGame(),arguments[3]);
      std::cout<<result.files<<" synthetic files copied\n";return 0;
    }
    if(count!=1)throw std::runtime_error("Unknown synthetic test mode");
    Fixture fixture;ConfigChecks(fixture);LaunchChecks(fixture,fs::canonical(arguments[0]));CopyChecks();PlacementGuideCopyChecks();
    std::cout<<checks<<" launch/config/process/data-copy checks passed; only synthetic helper processes executed.\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
