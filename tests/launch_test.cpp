#include "common/launch.h"
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
  const auto config=fixture.root/"config.ini";
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
  for(const auto& item:fs::directory_iterator(fixture.root))
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
  auto ini=Ini::Load(fixture.root/"config.ini");
  auto spec=PrepareLaunch(game,fixture.root,ini,"launch-test");
  Require(spec.arguments.size()==5 && spec.configuration==fixture.root/"config.ini","Launch contract/config identity");
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
  game.backend="ps4";Reject([&]{PrepareLaunch(game,fixture.root,ini);},"Unimplemented backend launched");game.backend="xbox360";
  const auto helper=fixture.root/FromUtf8("工具 空格/child helper.exe");
  fs::create_directories(helper.parent_path());fs::copy_file(executable,helper);
  const std::vector<std::wstring> payload{L"",L"simple",L"two words",L"中文 路径",L"\"quoted\"",L"trailing\\",L"before\\\"quote",L"& | ; < > %PATH%",L"a\tb"};
  for(const auto code:{0UL,37UL,259UL}) {
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
    Require(!GameIsRunning(fixture.root),"Exited child kept game lock");
    ChildProcess moved(std::move(child));Require(moved.ExitCode()==code,"Moving process handle lost status");
  }
}
}
int wmain(int count,wchar_t** arguments) {
  try {
    if(count>1 && std::wstring_view(arguments[1])==L"--fixture-child")return Child(count,arguments);
    Fixture fixture;ConfigChecks(fixture);LaunchChecks(fixture,fs::canonical(arguments[0]));
    std::cout<<checks<<" launch/config/process checks passed; only synthetic helper processes executed.\n";return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
