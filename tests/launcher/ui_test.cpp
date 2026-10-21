// Exercise the production window/click/save paths in hidden, fixture-owned
// windows. No desktop input, external editor, game or network is involved.
#include <Windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <filesystem>
#include <cstring>
#include <vector>
static std::vector<std::filesystem::path> opened_paths;
static unsigned unexpected_dialogs{};
static HINSTANCE WINAPI FixtureOpen(HWND,LPCWSTR verb,LPCWSTR file,LPCWSTR parameters,LPCWSTR directory,int show) {
  if(!verb || std::wstring_view(verb)!=L"open" || parameters || directory || show!=SW_SHOWNORMAL)
    return nullptr;
  opened_paths.emplace_back(file);return reinterpret_cast<HINSTANCE>(33);
}
static int WINAPI FixtureDialog(HWND,LPCWSTR,LPCWSTR,UINT) {
  ++unexpected_dialogs;return IDNO;
}
#define ShellExecuteW FixtureOpen
#define MessageBoxW FixtureDialog
#include "../../launcher/main.cpp"
#undef MessageBoxW
#undef ShellExecuteW
#include <iostream>

namespace {
unsigned checks{};
void Require(bool okay,const char* reason) {
  ++checks;if(!okay)throw std::runtime_error(reason);
}
void Write(const fs::path& path,std::string_view text) {
  fs::create_directories(path.parent_path());
  std::ofstream file(path,std::ios::binary);file<<text;
  if(!file)throw std::runtime_error("Cannot create launcher fixture");
}
std::wstring ControlText(HWND control) {
  std::wstring text(size_t(GetWindowTextLengthW(control))+1,L'\0');
  GetWindowTextW(control,text.data(),int(text.size()));text.resize(text.size()-1);return text;
}
struct Fixture {
  fs::path directory,root;
  Fixture() {
    std::array<unsigned char,16> nonce{};
    Require(BCryptGenRandom(nullptr,nonce.data(),ULONG(nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Fixture nonce");
    std::wstring name=L"cave-launcher-check-";
    for(const auto byte:nonce)name+=std::to_wstring(byte)+L"-";
    directory=fs::temp_directory_path()/name;
    Require(fs::create_directory(directory),"Fixture directory collision");
    root=directory/FromUtf8("合集 空格");fs::create_directory(root);
  }
  ~Fixture() {
    if(directory.parent_path()==fs::temp_directory_path() && directory.filename().native().starts_with(L"cave-launcher-check-")) {
      std::error_code error;fs::remove_all(directory,error);
    }
  }
};
struct Frame {
  App app;
  explicit Frame(const fs::path& root):app(Options{.root=root}) {
    Require(CreateWindowExW(0,kWindowClass,L"Launcher fixture",WS_OVERLAPPEDWINDOW,
        0,0,1080,900,nullptr,nullptr,GetModuleHandleW(nullptr),&app)!=nullptr,"Create hidden production launcher window");
    Settle();Require(app.preferences_available,"Load quick settings");
  }
  void Settle() {if(app.worker.joinable())app.worker.join();if(app.work)app.Complete();}
  ~Frame() {Settle();if(app.hwnd && IsWindow(app.hwnd))DestroyWindow(app.hwnd);}
  void Click(D2D1_RECT_F rectangle) {
    const auto scale=float(GetDpiForWindow(app.hwnd))/96;
    const auto point=MAKELPARAM(int((rectangle.left+rectangle.right)/2*scale),int((rectangle.top+rectangle.bottom)/2*scale));
    App::Window(app.hwnd,WM_LBUTTONUP,0,point);
    Require(unexpected_dialogs==0,"Click raised an unexpected dialog");
  }
};
void ColdReadback(const fs::path& root,std::wstring_view expected_language,std::wstring_view nickname) {
  const auto before=Ini::ReadText(ConfigPath(root));
  Frame frame(root);
  Require(cave::launcher::LanguageCode(language)==Utf8(expected_language),"Cold language readback");
  Require(ControlText(frame.app.nickname_control)==nickname,"Cold nickname readback");
  Require(SendMessageW(frame.app.fullscreen_control,BM_GETCHECK,0,0)==BST_UNCHECKED,"Cold fullscreen readback");
  Require(ControlText(frame.app.save_control)==T(L"保存"),"Cold localized controls");
  Require(Ini::ReadText(ConfigPath(root))==before,"Readback changed configuration");
  Require(ResolveSettings(Ini::Load(ConfigPath(root)),"sdoj").sdoj_frame_delay==1,
          "Cold startup did not resolve SDOJ's default 1V");
}
void Restart(const fs::path& root,std::wstring_view code,std::wstring_view nickname) {
  std::wstring executable(32768,L'\0');
  const auto size=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
  Require(size && size<executable.size(),"Locate cold-start test program");executable.resize(size);
  auto command=QuoteArgument(executable)+L" --readback "+QuoteArgument(root.native())+L" "+QuoteArgument(code)+L" "+QuoteArgument(nickname);
  STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
  Require(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Start independent readback process");
  CloseHandle(process.hThread);
  const auto waited=WaitForSingleObject(process.hProcess,10000);DWORD exit_code=1;
  GetExitCodeProcess(process.hProcess,&exit_code);CloseHandle(process.hProcess);
  Require(waited==WAIT_OBJECT_0 && exit_code==0,"Independent process did not read back saved settings");
}
void MainChecks() {
  Fixture fixture;
  const std::string before="\xef\xbb\xbf; preserve comment\r\n[collection]\r\nconfig_version=1\r\nlanguage=zh\r\n"
      "[display]\r\nfullscreen=true\r\n[community]\r\nnickname=Baseline\r\n"
      "[game.futari.display]\r\nfullscreen=true\r\n[future]\r\nkey=preserved\r\n";
  Write(fixture.root/"config.ini",before);
  {
    Frame frame(fixture.root);auto& app=frame.app;
    Require(!fs::exists(fixture.root/"config.ini") && Ini::ReadText(ConfigPath(fixture.root))==before,"Real app startup did not preserve migration bytes");
    for(const auto dimensions:std::array{std::pair{720.f,520.f},std::pair{1080.f,900.f}}) {
      app.layout.width=dimensions.first;app.layout.height=dimensions.second;
      const std::array controls{app.layout.data(),app.layout.refresh(),app.layout.config(),app.layout.logs(),app.layout.detail()};
      for(size_t index=0;index<controls.size();++index) {
        const auto r=controls[index];
        Require(r.top>=app.layout.footer() && r.bottom<=app.layout.height && r.left>=0 && r.right<=app.layout.width,"Utility control is outside bottom row");
        for(size_t other=index+1;other<controls.size();++other)
          Require(r.right<=controls[other].left || controls[other].right<=r.left,"Utility controls overlap");
      }
      for(const auto index:{1u,2u,0u}) {
        frame.Click(app.layout.language_choice(index));
        const std::array expected{Language::Chinese,Language::English,Language::Japanese};
        Require(language==expected[index],"Mouse handler selected the wrong language");
        Require(cave::launcher::ReadLanguage(Ini::Load(ConfigPath(fixture.root)))==language,"Language choice was not persisted");
        Require(ControlText(app.save_control)==T(L"保存"),"Language did not update native controls");
      }
    }
    SetWindowTextW(app.nickname_control,L"Draft-友人");
    Require(app.preferences_dirty,"Native edit notification did not mark preferences dirty");
    frame.Click(app.layout.language_choice(1));
    Require(ControlText(app.nickname_control)==L"Draft-友人" && app.preferences_dirty,"Language switch discarded unsaved nickname");
    Require(ResolveSharedSettings(Ini::Load(ConfigPath(fixture.root))).nickname=="Baseline","Language switch silently saved nickname");
    const auto saved=Ini::ReadText(ConfigPath(fixture.root));app.busy=true;
    frame.Click(app.layout.language_choice(2));app.busy=false;
    Require(language==Language::English && Ini::ReadText(ConfigPath(fixture.root))==saved,"Busy language switch changed config");
    app.focus=12;App::Window(app.hwnd,WM_KEYDOWN,VK_RETURN,0);
    Require(language==Language::Japanese,"Keyboard activation did not select JP");
    SendMessageW(app.fullscreen_control,BM_SETCHECK,BST_UNCHECKED,0);
    App::Window(app.hwnd,WM_COMMAND,MAKEWPARAM(kSaveControl,BN_CLICKED),0);frame.Settle();
    const auto settings=ResolveSharedSettings(Ini::Load(ConfigPath(fixture.root)));
    Require(settings.nickname=="Draft-友人" && !settings.fullscreen,"Save handler did not persist quick settings");
    Require(cave::launcher::ReadLanguage(Ini::Load(ConfigPath(fixture.root)))==Language::Japanese,"Quick settings overwrote language");
    Require(ResolveSettings(Ini::Load(ConfigPath(fixture.root)),"futari").fullscreen,"UI save changed game-specific override");
    Require(Ini::ReadText(ConfigPath(fixture.root)).find("key=preserved\r\n")!=std::string::npos,"UI save lost unknown text");
    frame.Click(app.layout.config());Require(opened_paths.back()==fixture.root/"user-data/config.ini","Config button opened the wrong file");
    frame.Click(app.layout.logs());Require(opened_paths.back()==fixture.root/"logs","Log button opened the wrong directory");
    const auto text=Ini::ReadText(ConfigPath(fixture.root));frame.Click(app.layout.refresh());frame.Settle();
    Require(language==Language::Japanese && ControlText(app.nickname_control)==L"Draft-友人" && Ini::ReadText(ConfigPath(fixture.root))==text,"Refresh changed saved preferences");
    const auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(cave::launcher::kArtwork[3].resource),RT_RCDATA);
    Require(resource!=nullptr,"Missing SDOJ resource");
    const auto count=SizeofResource(nullptr,resource);const auto data=LockResource(LoadResource(nullptr,resource));
    std::ifstream file(fs::path(CAVE_TEST_SOURCE_ROOT)/"launcher/assets/sdoj.jpg",std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(file),{}};
    Require(data && count==source.size() && std::memcmp(data,source.data(),count)==0,"Embedded SDOJ picture differs from authored asset");
  }
  Restart(fixture.root,L"ja",L"Draft-友人");
  for(const auto index:{1u,0u}) {
    {Frame frame(fixture.root);frame.Click(frame.app.layout.language_choice(index));}
    Restart(fixture.root,index==1?L"en":L"zh",L"Draft-友人");
  }
  Require(unexpected_dialogs==0 && opened_paths.size()==2,"Unexpected shell or dialog side effects");
}
}
int wmain(int count,wchar_t** arguments) {
  try {
    Check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Initialize test COM");
    struct Apartment {~Apartment(){CoUninitialize();}} apartment;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW definition{};definition.lpfnWndProc=App::Window;definition.hInstance=GetModuleHandleW(nullptr);definition.lpszClassName=kWindowClass;
    Require(RegisterClassW(&definition)!=0,"Register production window handler");
    if(count==5 && std::wstring_view(arguments[1])==L"--readback")ColdReadback(arguments[2],arguments[3],arguments[4]);
    else if(count==1)MainChecks();
    else throw std::runtime_error("Unexpected launcher test arguments");
    std::cout<<checks<<" launcher integration checks passed; hidden fixture windows only.\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
