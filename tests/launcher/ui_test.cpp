// Exercise the production window/click/save paths in hidden, fixture-owned
// windows. No desktop input, external editor, game or network is involved.
#include <Windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <filesystem>
#include <cstring>
#include <vector>
#include <commctrl.h>
static std::vector<std::filesystem::path> opened_paths;
static unsigned unexpected_dialogs{};
static unsigned show_requests{},foreground_requests{};
static int last_show_request{};
static std::vector<int> nickname_responses;
static std::vector<std::wstring> nickname_details;
static std::vector<int> update_responses;
static std::vector<std::wstring> update_notices;
static HWND update_foreground{};
static int held_exit_key{};
static bool update_dialog_failure{};
static HWND WINAPI FixtureGetForeground(){return update_foreground?update_foreground:GetForegroundWindow();}
static BOOL WINAPI FixtureVisible(HWND window){return window==update_foreground?TRUE:IsWindowVisible(window);}
static SHORT WINAPI FixtureKeyState(int key){return key==held_exit_key?SHRT_MIN:0;}
struct PaintRequest { HWND window; RECT area; bool whole; BOOL erase; };
static std::vector<PaintRequest> paint_requests;
static BOOL WINAPI FixtureInvalidate(HWND window,const RECT* area,BOOL erase) {
  paint_requests.push_back({window,area?*area:RECT{},!area,erase});
  return InvalidateRect(window,area,erase);
}
static HRESULT WINAPI FixtureNicknameDialog(const TASKDIALOGCONFIG* config,int* result,int*,BOOL*) {
  if(config->cButtons==2 && config->pButtons[0].nButtonID==IDYES && config->pButtons[1].nButtonID==IDCANCEL) {
    if(update_dialog_failure)return E_FAIL;
    if(update_responses.empty()){++unexpected_dialogs;return E_FAIL;}
    update_notices.emplace_back(config->pszMainInstruction);
    *result=update_responses.front();update_responses.erase(update_responses.begin());return S_OK;
  }
  if(nickname_responses.empty()) {++unexpected_dialogs;return E_FAIL;}
  if(config->nDefaultButton!=IDCANCEL || config->cButtons!=2 || config->pButtons[0].nButtonID!=IDCANCEL ||
      config->pButtons[1].nButtonID!=IDYES || !(config->dwFlags&TDF_ALLOW_DIALOG_CANCELLATION)) {
    ++unexpected_dialogs;return E_FAIL;
  }
  nickname_details.emplace_back(config->pszExpandedInformation);
  *result=nickname_responses.front();nickname_responses.erase(nickname_responses.begin());return S_OK;
}
static BOOL WINAPI FixtureShowWindow(HWND,int command) {++show_requests;last_show_request=command;return TRUE;}
static BOOL WINAPI FixtureForeground(HWND) {++foreground_requests;return TRUE;}
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
#define ShowWindow FixtureShowWindow
#define SetForegroundWindow FixtureForeground
#define TaskDialogIndirect FixtureNicknameDialog
#define InvalidateRect FixtureInvalidate
#define GetForegroundWindow FixtureGetForeground
#define IsWindowVisible FixtureVisible
#define GetAsyncKeyState FixtureKeyState
#include "../../launcher/main.cpp"
#include "launcher/input_devices.h"
#undef GetAsyncKeyState
#undef IsWindowVisible
#undef GetForegroundWindow
#undef InvalidateRect
#undef TaskDialogIndirect
#undef SetForegroundWindow
#undef ShowWindow
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
  explicit Frame(const fs::path& root,std::string_view selection={},bool minimized=false):app(Options{.root=root,.selection=std::string(selection)}) {
    // Keep player config bytes intact while replacing only network I/O.
    app.update_fetch=[]{return update::Result{false,{},"fixture transport"};};
    Require(CreateWindowExW(0,kWindowClass,kWindowTitle,WS_OVERLAPPEDWINDOW|WS_VSCROLL|WS_CLIPCHILDREN|(minimized?WS_MINIMIZE:0),
        0,0,1080,900,nullptr,nullptr,GetModuleHandleW(nullptr),&app)!=nullptr,"Create hidden production launcher window");
    Settle();Require(app.preferences_available,"Load quick settings");
  }
  void Settle() {
    for(unsigned n=0;n<8 && (app.worker.joinable() || app.work);++n) {if(app.worker.joinable())app.worker.join();if(app.work)app.Complete();}
    MSG message{};
    for(unsigned n=0;IsWindow(app.hwnd) && PeekMessageW(&message,app.hwnd,kUpdateScrollBar,kUpdateScrollBar,PM_REMOVE);) {
      if(message.message==WM_QUIT)continue;
      if(++n>8)throw std::runtime_error("Launcher scrollbar visibility did not settle");
      DispatchMessageW(&message);
    }
  }
  ~Frame() {Settle();if(app.hwnd && IsWindow(app.hwnd))DestroyWindow(app.hwnd);}
  void Click(D2D1_RECT_F rectangle) {
    const auto scale=float(GetDpiForWindow(app.hwnd))/96;
    const auto point=MAKELPARAM(int((rectangle.left+rectangle.right)/2*scale),int((rectangle.top+rectangle.bottom)/2*scale));
    App::Window(app.hwnd,WM_LBUTTONUP,0,point);
    Require(unexpected_dialogs==0,"Click raised an unexpected dialog");
  }
};
void Restart(const fs::path& root,std::wstring_view code,std::wstring_view nickname,std::wstring_view game=L"futari");
void UpdateChecks() {
  Fixture fixture;Frame frame(fixture.root);auto& app=frame.app;
  app.update_request.reset();app.update_auto=true;
  Write(ConfigPath(fixture.root),"[collection]\ncheck_for_updates=true\n");
  app.update_state={};
  app.update_fetch=[]{return update::Result{true,update::Release{"v0.1",std::string(update::kReleasePrefix)+"v0.1",""},{}};};
  app.next_update_refresh={};
  app.UpdateTick();
  for(unsigned n=0;n<200 && app.update_request;++n){Sleep(5);app.UpdateTick();}
  Require(app.update_state.checked && app.update_state.latest && app.update_state.latest->tag=="v0.1" &&
          app.next_update_refresh>std::chrono::steady_clock::now(),"An open launcher did not recheck for updates");
  app.update_state.latest=update::Release{"v9.0",std::string(update::kReleasePrefix)+"v9.0","Authored UI fixture"};
  const auto before=update_notices.size();
  app.UpdateTick();Require(update_notices.size()==before && app.update_state.notified.empty(),"Hidden launcher displayed/marked reminder");
  update_foreground=app.hwnd;app.busy=true;app.UpdateTick();app.busy=false;
  Require(update_notices.size()==before && app.update_state.notified.empty(),"Busy launcher reminder");
  app.child.emplace();app.UpdateTick();app.child.reset();
  Require(update_notices.size()==before && app.update_state.notified.empty(),"Running game reminder");
  app.preferences_dirty=true;app.UpdateTick();app.preferences_dirty=false;
  Require(update_notices.size()==before,"Nickname editing interrupted");
  update_dialog_failure=true;bool failed=false;
  try{app.ShowUpdate();}catch(...){failed=true;}update_dialog_failure=false;
  Require(failed && app.update_state.notified.empty(),"Failed dialog counted as shown");
  update_responses.push_back(IDCANCEL);app.UpdateTick();
  Require(update_notices.size()==before+1 && app.update_state.notified.contains("v9.0"),"Visible reminder missing");
  app.UpdateTick();Require(update_notices.size()==before+1,"Same session repeated reminder");
  app.update_state=update::Load(fixture.root);app.UpdateTick();
  Require(update_notices.size()==before+1,"Cold state repeated reminder");
  app.update_state.latest=update::Release{"v9.1",std::string(update::kReleasePrefix)+"v9.1","Next release"};
  update_responses.push_back(IDYES);app.UpdateTick();
  Require(update_notices.size()==before+2,"Next release not announced");
  Require(!opened_paths.empty() && opened_paths.back()==FromUtf8(std::string(update::kReleasePrefix)+"v9.1"),"Notice did not open release page");
  Write(ConfigPath(fixture.root),"[collection]\ncheck_for_updates=false\n");
  app.ManualRefresh();frame.Settle();
  Require(!app.update_auto,"Refresh ignored disabled update setting");
  app.update_state.latest=update::Release{"v9.2",std::string(update::kReleasePrefix)+"v9.2","Pending release"};
  app.UpdateTick();Require(update_notices.size()==before+2,"Disabled updates displayed pending reminder");
  Write(ConfigPath(fixture.root),"[collection]\ncheck_for_updates=true\n");
  app.update_state.checked=update::Now();app.ManualRefresh();frame.Settle();
  Require(app.update_auto && !app.update_request,"Refresh ignored enabled setting or bypassed cache");
  update_responses.push_back(IDCANCEL);app.UpdateTick();
  Require(update_notices.size()==before+3,"Reenabled reminder missing");

  app.packaged_update=true;
  app.update_state.notified.erase("v9.2");
  app.update_state.latest->program=update::ProgramAsset{
    "https://github.com/zxl0714/EVAC-PC-LABEL/releases/download/v9.2/EVAC-PC-LABEL-v9.2-win-x64.zip",
    std::string(64,'a'),123};
  app.download_program=[](const fs::path&,const update::Release&){throw std::runtime_error("offline fixture");};
  app.UpdateTick();
  Require(app.program_download.has_value() && !app.ProgramReady() && app.UpdateVisible(),
          "A pending download hid the update indicator or counted as installable");
  const auto settle_download=[&] {
    for(unsigned n=0;n<200 && app.program_download;++n) {
      Sleep(5);
      app.UpdateTick();
    }
    Require(!app.program_download,"Program download fixture did not finish");
  };
  update_responses.push_back(IDCANCEL);
  settle_download();
  Require(!app.ProgramReady() && update_notices.size()==before+4 && app.update_state.notified.contains("v9.2"),
          "Failed download did not offer the release page once");
  app.UpdateTick();
  Require(!app.program_download,"Failed download immediately retried");
  update_responses.push_back(IDYES);frame.Click(app.layout.update_badge());
  Require(update_notices.size()==before+5 && !opened_paths.empty() &&
          opened_paths.back()==FromUtf8(std::string(update::kReleasePrefix)+"v9.2") && !app.close_requested,
          "Update indicator did not offer the release page before the download finished");
  app.program_retry={};
  app.download_program=[](const fs::path&,const update::Release&){};
  app.UpdateTick();settle_download();
  Require(app.ProgramReady() && update_notices.size()==before+5,"Verified update required a modal announcement");
  app.child.emplace();frame.Click(app.layout.update_badge());app.child.reset();
  Require(update_notices.size()==before+5,"Update opened during play");
  update_responses.push_back(IDCANCEL);frame.Click(app.layout.update_badge());
  Require(app.ProgramReady() && update_notices.back().ends_with(L"v9.2"),"Update badge did not open the matching release");
  static std::wstring requested_update;
  requested_update.clear();
  app.start_update_worker=[](const fs::path& root,std::wstring_view action){
    if(!fs::is_regular_file(ConfigPath(root)))throw std::runtime_error("Incorrect update root");
    requested_update=action;
  };
  update_responses.push_back(IDYES);frame.Click(app.layout.update_badge());
  Require(app.close_requested && requested_update==L"v9.2" &&
          update::Load(fixture.root).latest->program==app.update_state.latest->program,"Restart lost the verified release asset");
  update_foreground=nullptr;
}
void CoverLayoutChecks() {
  Fixture fixture;
  Write(fixture.root/"user-data/config.ini","[collection]\nlanguage=en\nselected_game=espgaluda\ncheck_for_updates=false\n");
  {
    Frame frame(fixture.root);auto& app=frame.app;
    Require(Catalogue()[app.selected].id=="espgaluda","Cover grid did not restore its selected game");
    for(const auto dimensions:std::array{std::pair{720.f,520.f},std::pair{1080.f,900.f},std::pair{1280.f,720.f}}) {
      app.layout.width=dimensions.first;app.layout.height=dimensions.second;
      Require(app.layout.columns()>1,"Representative cover window did not form a grid");
      for(size_t index=0;index<LauncherCatalogue().size();++index) {
        app.Select(app.library.visible[index]);frame.Settle();
        const auto card=app.layout.card(index);
        const auto visible=std::min(card.bottom,app.layout.footer())-std::max(card.top,app.layout.header());
        Require(visible>=std::min(card.bottom-card.top,app.layout.viewport_height())-1,
                "Selected cover is outside the visible grid viewport");
        Require(card.left>=app.layout.margin()-1 && card.right<=app.layout.width-app.layout.margin()+1,
                "Cover lies outside the grid width");
      }
      App::Window(app.hwnd,WM_KEYDOWN,VK_HOME,0);frame.Settle();
      Require(app.selected==0,"Cover Home did not select the first game");
      App::Window(app.hwnd,WM_KEYDOWN,VK_DOWN,0);frame.Settle();
      Require(app.VisiblePosition()==app.layout.columns(),"Cover Down did not select the next row");
      App::Window(app.hwnd,WM_KEYDOWN,VK_RIGHT,0);frame.Settle();
      Require(app.VisiblePosition()==app.layout.columns()+1,"Cover Right did not select the next column");
      App::Window(app.hwnd,WM_KEYDOWN,VK_UP,0);frame.Settle();
      Require(app.VisiblePosition()==1,"Cover Up did not preserve its column");
    }
    app.Select(Catalogue().size()-1);frame.Settle();
    Require(Catalogue()[app.selected].id=="rtype-final" &&
            app.rows[app.selected].inspection.state=="missing_data" &&
            !CanPlay(app.rows[app.selected].inspection), "R-Type Final should ask for a disc when its folder is empty");
  }
  const auto saved=Ini::Load(ConfigPath(fixture.root));
  Require(saved.Get("collection","selected_game")->text==Catalogue().back().id,
          "Cover selection was not retained at close");
}
void WindowSizeChecks() {
  Fixture fixture;
  Write(fixture.root/"user-data/config.ini","[collection]\ncheck_for_updates=false\nlauncher_layout=banner\n"
        "launcher_banner_width=1150\nlauncher_banner_height=800\n");
  {
    cave::launcher::WindowSize expected{};
    {
      Frame frame(fixture.root);auto& app=frame.app;
      Require(!app.saved_window_size,"Legacy banner dimensions overrode the grid default");
      SetWindowPos(app.hwnd,nullptr,0,0,1250,850,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
      expected=*app.normal_window_size;
      App::Window(app.hwnd,WM_SIZE,SIZE_MAXIMIZED,MAKELPARAM(1800,1000));
      App::Window(app.hwnd,WM_SIZE,SIZE_MINIMIZED,0);
      Require(app.normal_window_size->width==expected.width && app.normal_window_size->height==expected.height,
              "Maximize/minimize replaced the remembered normal size");
    }
    const auto saved=cave::launcher::ReadWindowSize(Ini::Load(ConfigPath(fixture.root)));
    Require(saved && saved->width==expected.width && saved->height==expected.height,"Normal close did not save window size");
    {
      Frame frame(fixture.root);auto& app=frame.app;
      FitInitialWindow(app.hwnd,app.saved_window_size);
      RECT bounds{};GetWindowRect(app.hwnd,&bounds);const auto scale=float(GetDpiForWindow(app.hwnd))/96;
      Require(std::abs((bounds.right-bounds.left)/scale-expected.width)<=1 &&
              std::abs((bounds.bottom-bounds.top)/scale-expected.height)<=1,
              "Restart did not restore the normal window dimensions");

      MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
      Require(GetMonitorInfoW(MonitorFromWindow(app.hwnd,MONITOR_DEFAULTTONEAREST),&monitor),"Read fixture monitor");
      const auto& work=monitor.rcWork;
      const auto original=bounds;
      // A window scaled for a denser display can extend beyond its work area.
      RECT suggested{work.left,work.top-80,work.right+80,work.bottom};
      const auto dpi=GetDpiForWindow(app.hwnd);
      SendMessageW(app.hwnd,WM_DPICHANGED,MAKELONG(dpi,dpi),reinterpret_cast<LPARAM>(&suggested));
      GetWindowRect(app.hwnd,&bounds);
      Require(bounds.left>=work.left && bounds.top>=work.top && bounds.right<=work.right && bounds.bottom<=work.bottom,
              "DPI change moved launcher window controls outside the work area");
      SendMessageW(app.hwnd,WM_DPICHANGED,MAKELONG(dpi,dpi),reinterpret_cast<LPARAM>(&original));
      GetWindowRect(app.hwnd,&bounds);
      Require(EqualRect(&bounds,&original),"DPI change modified a window that already fit the work area");
    }
  }
  {
    Frame frame(fixture.root);auto& app=frame.app;
    app.Select(Catalogue().size()-1);frame.Settle();
    const auto games=app.layout.game_count;
    app.layout.game_count=Catalogue().size();
    FitInitialWindow(app.hwnd,{},app.layout.game_count);frame.Settle();
    Require(app.normal_window_size.has_value(),"Save a pre-arcade default window size");
    const auto previous=app.normal_window_size;
    app.layout.game_count=games;
    FitInitialWindow(app.hwnd,previous,games);frame.Settle();
    ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);frame.Settle();UpdateWindow(app.hwnd);
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromWindow(app.hwnd,MONITOR_DEFAULTTONEAREST),&monitor);
    const auto dpi=GetDpiForWindow(app.hwnd);const auto scale=float(dpi)/96;
    RECT border{};AdjustWindowRectExForDpi(&border,WS_OVERLAPPEDWINDOW,FALSE,0,dpi);
    const auto full_height=(cave::launcher::kCoverHeaderHeight+cave::launcher::kFooterHeight+
        3*cave::launcher::CoverHeight(cave::launcher::kCoverPreferredWidth)+2*cave::launcher::kCoverRowGap)*scale+border.bottom-border.top;
    if(full_height<=monitor.rcWork.bottom-monitor.rcWork.top-24*scale) {
      std::cout<<"Full library: client="<<app.layout.width<<"x"<<app.layout.height
               <<" content="<<app.layout.content_height()<<" viewport="<<app.layout.viewport_height()<<"\n";
      Require(app.layout.max_scroll()==0,"Full-library initial size leaves a small scroll range");
      Require(!(GetWindowLongPtrW(app.hwnd,GWL_STYLE)&WS_VSCROLL),"A fitting library retains a scrollbar");
      Require(app.layout.card(games-1).bottom<=app.layout.footer(),"Saved default dimensions clipped the new arcade card");
    }
  }
  {
    Frame frame(fixture.root);auto& app=frame.app;
    Require(app.saved_window_size.has_value(),"First launch did not save its cover grid size");
    FitInitialWindow(app.hwnd,app.saved_window_size);frame.Settle();
    RECT bounds{},client{};
    Require(GetWindowRect(app.hwnd,&bounds) && GetClientRect(app.hwnd,&client) &&
            bounds.right-bounds.left>client.right && bounds.bottom-bounds.top>client.bottom,
            "Reopening the saved cover grid collapsed the native window frame");
    SendMessageW(app.hwnd,WM_SYSCOMMAND,SC_CLOSE,0);
    Require(!IsWindow(app.hwnd),"Restored launcher did not accept the native close command");
  }
}
void SelectionChecks() {
  Fixture fixture;
  const auto ini="\xef\xbb\xbf; keep selected-game fixture\r\n[collection]\r\nconfig_version=1\r\nlanguage=en\r\nlauncher_layout=banner\r\nselected_game=daioujou\r\n[community]\r\nnickname=Baseline\r\n[future]\r\nvalue=untouched\r\n";
  Write(fixture.root/"user-data/config.ini",ini);
  {
    Frame frame(fixture.root);auto& app=frame.app;
    Require(Catalogue()[app.selected].id=="daioujou","Cold GUI did not restore selected ID");
    Require(ControlText(app.hwnd)==L"EVAC PC LABEL v"+Wide(cave::launcher::kProductVersion),"Production launcher caption lacks the product name and version");
    Require(std::string_view(cave::launcher::kProductVersion).find('.')!=std::string_view::npos,"Product version is not a dotted release number");
    const auto scale=float(GetDpiForWindow(app.hwnd))/96;
    App::Window(app.hwnd,WM_SIZE,SIZE_RESTORED,MAKELPARAM(int(720*scale),int(520*scale)));
    const auto selected=app.layout.card(app.VisiblePosition().value());
    const auto visible=std::min(selected.bottom,app.layout.footer())-std::max(selected.top,app.layout.header());
    Require(app.layout.columns()>1 && visible>=std::min(selected.bottom-selected.top,app.layout.viewport_height())-1,
            "Legacy banner config did not restore its selected game in the grid");
    Require(app.rows[app.selected].inspection.state=="missing_data","Missing data erased a valid saved selection");
    SetWindowTextW(app.nickname_control,L"Unsaved draft");
    App::Window(app.hwnd,WM_KEYDOWN,VK_LEFT,0);frame.Settle();
    Require(Catalogue()[app.selected].id=="ketsui","Left no longer selects the preceding game");
    App::Window(app.hwnd,WM_KEYDOWN,VK_RIGHT,0);frame.Settle();
    Require(Catalogue()[app.selected].id=="daioujou","Right no longer selects the next game");
    app.Select(2);frame.Settle();
    Require(Ini::Load(ConfigPath(fixture.root)).Get("collection","selected_game")->text=="daioujou","Selecting saved too early instead of at normal close");
    Write(ConfigPath(fixture.root),Ini::ReadText(ConfigPath(fixture.root))+"[editor]\r\nconcurrent=preserved\r\n");
    App::Window(app.hwnd,WM_CLOSE,0,0);
    Require(!IsWindow(app.hwnd),"Normal close did not destroy the GUI");
  }
  const auto saved=Ini::ReadText(ConfigPath(fixture.root));
  const auto settings=Ini::Parse(saved);
  Require(settings.Get("collection","selected_game")->text=="ketsui","Normal close did not remember the selected, unlaunched game");
  Require(settings.Get("community","nickname")->text=="Baseline" && settings.Get("editor","concurrent")->text=="preserved" && saved.starts_with("\xef\xbb\xbf; keep"),
          "Remembering selection overwrote an unsaved form or an editor's latest text");
  Restart(fixture.root,L"en",L"Baseline",L"ketsui");
  {Frame frame(fixture.root);Require(Catalogue()[frame.app.selected].id=="ketsui","Next GUI did not restore the last closed choice");}
  {Frame frame(fixture.root,"esprade");Require(Catalogue()[frame.app.selected].id=="esprade","Explicit GUI choice did not take priority over saved choice");}
  {Frame frame(fixture.root,"guwange");Require(Catalogue()[frame.app.selected].id=="guwange","Explicit GUI choice did not select the seventh title");}
  for(const auto choice:{"removed-game",""}) {
    Write(ConfigPath(fixture.root),std::string("[collection]\nselected_game=")+choice+"\n");
    Frame frame(fixture.root);Require(Catalogue()[frame.app.selected].id=="futari","Unknown or empty saved ID did not use the first supported game");
  }
}
void NicknameWarningChecks() {
  Fixture fixture;Write(fixture.root/"user-data/config.ini","[collection]\nlanguage=en\n[community]\nnickname=Player\n[future]\nx=kept\n");
  Frame frame(fixture.root);auto& app=frame.app;
  const auto before=Ini::ReadText(ConfigPath(fixture.root));
  SetWindowTextW(app.nickname_control,L"友人😀");
  const auto count=nickname_details.size();nickname_responses.push_back(IDCANCEL);
  App::Window(app.hwnd,WM_COMMAND,MAKEWPARAM(kSaveControl,BN_CLICKED),0);
  Require(nickname_details.size()==count+1 && nickname_details.back().find(L"U+1F600")!=std::wstring::npos,"Save did not show missing-character details");
  Require(Ini::ReadText(ConfigPath(fixture.root))==before && app.preferences_dirty && ControlText(app.nickname_control)==L"友人😀","Cancel changed configuration or discarded the nickname draft");
  app.rows[app.selected].inspection.state="ready";
  nickname_responses.push_back(IDCANCEL);app.LaunchSelected();
  Require(!app.busy && !app.child && !app.work && Ini::ReadText(ConfigPath(fixture.root))==before,"Launch bypassed the nickname warning/cancel");
  nickname_responses.push_back(IDYES);
  App::Window(app.hwnd,WM_COMMAND,MAKEWPARAM(kSaveControl,BN_CLICKED),0);frame.Settle();
  Require(ResolveSharedSettings(Ini::Load(ConfigPath(fixture.root))).nickname=="友人😀" && !app.preferences_dirty,"Save anyway replaced the full Unicode nickname");
  Require(Ini::ReadText(ConfigPath(fixture.root)).find("x=kept")!=std::string::npos,"Warning save lost unknown configuration");
  const auto accepted=nickname_details.size();
  SendMessageW(app.fullscreen_control,CB_SETCURSEL,0,0);app.preferences_dirty=true;
  Require(app.SavePreferences() && nickname_details.size()==accepted,"Fullscreen-only edit repeated an accepted nickname warning");
  SetWindowTextW(app.nickname_control,L"Player");
  Require(app.SavePreferences() && nickname_details.size()==accepted,"Supported nickname raised a warning");
  Require(nickname_responses.empty() && unexpected_dialogs==0,"Nickname dialog response was not consumed / unexpected side effect");
}
void NicknameAlignmentChecks() {
  Fixture fixture;Frame frame(fixture.root);auto& app=frame.app;
  const auto edit=app.nickname_control;
  const auto style=GetWindowLongPtrW(edit,GWL_STYLE);
  Require(!(style&ES_MULTILINE) && (style&ES_AUTOHSCROLL),"Nickname must remain a horizontally scrolling single-line edit");
  Require(SendMessageW(edit,EM_GETLIMITTEXT,0,0)==64,"Nickname character limit changed");
  SetWindowTextW(edit,L"Player-友人");SendMessageW(edit,EM_SETSEL,1,4);
  const auto before=Ini::ReadText(ConfigPath(fixture.root));
  for(const auto dpi:{96u,120u,144u,192u}) {
    app.PositionPreferences(dpi);
    RECT rectangle{};GetWindowRect(edit,&rectangle);
    MapWindowPoints(nullptr,app.hwnd,reinterpret_cast<POINT*>(&rectangle),2);
    const auto dc=GetDC(edit);const auto old=SelectObject(dc,app.form_font);
    TEXTMETRICW metrics{};Require(GetTextMetricsW(dc,&metrics)!=FALSE,"Measure native edit font");
    SelectObject(dc,old);ReleaseDC(edit,dc);
    const auto field=app.layout.nickname();const float scale=float(dpi)/96;
    const auto text=SendMessageW(edit,EM_POSFROMCHAR,0,0);
    const auto top=rectangle.top+GET_Y_LPARAM(text),bottom=top+metrics.tmHeight;
    Require(std::abs((top-int(field.top*scale))-(int(field.bottom*scale)-bottom))<=1,
            "Nickname text is not vertically centered at this DPI");
    Require(rectangle.bottom-rectangle.top>=metrics.tmHeight,"Nickname line clips its font height");
    DWORD start{},end{};SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
    Require(start==1 && end==4 && ControlText(edit)==L"Player-友人","Relayout changed text or selection");
  }
  app.PositionPreferences();
  const auto r=app.layout.nickname();app.Click(r.left+2,r.top+2);
  Require(app.focus==2,"Clicking nickname padding did not select the edit");
  Require(Ini::ReadText(ConfigPath(fixture.root))==before,"Relayout saved the nickname draft");
  SetWindowTextW(edit,L"Player");
  MSG enter{};enter.hwnd=edit;enter.message=WM_KEYDOWN;enter.wParam=VK_RETURN;
  Require(app.Input(enter),"Enter no longer saves the single-line nickname");frame.Settle();
  Require(ResolveSharedSettings(Ini::Load(ConfigPath(fixture.root))).nickname=="Player" && !app.preferences_dirty,
          "Enter did not persist the nickname");
}
void DisplayDropdownChecks() {
  Fixture fixture;
  for(const auto lang:std::array{Language::Chinese,Language::English,Language::Japanese}) {
    Frame frame(fixture.root);auto& app=frame.app;
    language=lang;app.PopulateDisplay(ResolveSharedSettings(Ini::Load(ConfigPath(fixture.root))));
    Require((GetWindowLongPtrW(app.fullscreen_control,GWL_STYLE)&CBS_DROPDOWNLIST)==CBS_DROPDOWNLIST,
            "Display selector must be a real accessible keyboard-operable dropdown");
    SetWindowTextW(app.nickname_control,L"Unsaved nickname");
    const auto old_name=ResolveSharedSettings(Ini::Load(ConfigPath(fixture.root))).nickname;
    RECT bounds{};
    Require(GetWindowRect(app.hwnd,&bounds) &&
            SetWindowPos(app.hwnd,nullptr,bounds.left+22,bounds.top+30,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE),
            "Move launcher fixture before opening the resolution list");
    const auto before_open=Ini::ReadText(ConfigPath(fixture.root));
    const auto error_count=unexpected_dialogs;
    const auto old_choice=SendMessageW(app.fullscreen_control,CB_GETCURSEL,0,0);
    SendMessageW(app.fullscreen_control,CB_SETCURSEL,WPARAM(-1),0);
    App::Window(app.hwnd,WM_COMMAND,MAKEWPARAM(kFullscreenControl,CBN_SELCHANGE),0);
    SendMessageW(app.fullscreen_control,CB_SETCURSEL,old_choice,0);
    App::Window(app.hwnd,WM_COMMAND,MAKEWPARAM(kFullscreenControl,CBN_SELCHANGE),0);
    Require(unexpected_dialogs==error_count && Ini::ReadText(ConfigPath(fixture.root))==before_open,
            "Transient or unchanged dropdown selection changed config or opened an error");
    for(size_t index=0;index<app.display_choices.size();++index) {
      SendMessageW(app.fullscreen_control,CB_SETCURSEL,index,0);
      App::Window(app.hwnd,WM_COMMAND,MAKEWPARAM(kFullscreenControl,index%2?CBN_SELCHANGE:CBN_SELENDOK),0);
      const auto settings=ResolveSharedSettings(Ini::Load(ConfigPath(fixture.root)));
      const auto expected=app.display_choices[index];
      Require(settings.fullscreen==expected.fullscreen && settings.window_width==expected.width &&
              settings.window_height==expected.height,"Dropdown change did not immediately persist");
      Require(settings.nickname==old_name && ControlText(app.nickname_control)==L"Unsaved nickname" && app.preferences_dirty,
              "Display selection submitted or discarded the nickname draft");
    }
    {Frame reopened(fixture.root);Require(reopened.app.SelectedDisplay()==app.SelectedDisplay(),"Display selection lost after closing/reopening");}
  }
  const auto before=Ini::ReadText(ConfigPath(fixture.root));
  SaveSharedDisplay(fixture.root,before,{false,1234,696});
  {Frame frame(fixture.root);Require(frame.app.SelectedDisplay()==DisplaySelection{false,1234,696},"Custom INI resolution silently replaced by a preset");}
  {
    Frame frame(fixture.root);auto& app=frame.app;
    const auto choice=app.display_choices[1];
    SendMessageW(app.fullscreen_control,CB_SETCURSEL,1,0);
    App::Window(app.hwnd,WM_COMMAND,MAKEWPARAM(kFullscreenControl,CBN_SELENDOK),0);
    MSG shortcut{};shortcut.hwnd=app.fullscreen_control;shortcut.message=WM_KEYDOWN;shortcut.wParam=VK_F11;
    Require(app.Input(shortcut) && app.BigPicture(),"F11 must work while the display selector still owns keyboard input");
    App::Window(app.hwnd,WM_KEYDOWN,VK_F11,0);
    Require(!app.BigPicture() && app.SelectedDisplay()==choice,"Returning to desktop changed the selected resolution");
    Require(app.display_choices.front().fullscreen && SendMessageW(app.fullscreen_control,CB_GETCOUNT,0,0)==LRESULT(app.display_choices.size()),
            "Fullscreen must remain the first display option after returning to desktop");
  }
}
void GameLibraryChecks() {
  using cave::launcher::GameLibrary;
  const GameLibrary defaults(Ini::Parse(""));
  Require(GameLibrary::Encode(defaults.visible)==
          "futari,espgaluda2,sdoj,guwange,ketsui,daioujou,esprade,feveron,garegga,espgaluda,ibara,daioujou-ps2,rtype-final,donpachi,dodonpachi",
          "The default launcher order must start with Futari and group games by platform");
  Require(GameLibrary::Encode(GameLibrary(Ini::Parse("[collection]\ngame_order=ketsui,espgaluda2\n")).order)==
          "ketsui,espgaluda2,futari,sdoj,guwange,daioujou,esprade,feveron,garegga,espgaluda,ibara,daioujou-ps2,rtype-final,donpachi,dodonpachi",
          "An existing custom prefix must take precedence over the default order");
  Fixture fixture;
  Write(ConfigPath(fixture.root),"[collection]\ncheck_for_updates=false\ngame_order=futari,espgaluda2,ketsui\n");
  const auto drag=[](App& app,size_t from,size_t to) {
    const auto point=[&](size_t position) {
      const auto card=app.layout.card(position);
      const auto scale=float(GetDpiForWindow(app.hwnd))/96;
      return MAKELPARAM(int((card.left+30)*scale),int((card.top+30)*scale));
    };
    App::Window(app.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,point(from));
    Require(!app.DragPreview(),"A click began a drag before the pointer moved");
    const auto start=app.card_press->origin;
    const auto same_card=MAKELPARAM(start.x+20,start.y+12);
    App::Window(app.hwnd,WM_MOUSEMOVE,MK_LBUTTON,same_card);
    const auto first=app.DragPreview();
    Require(first.has_value(),"Moving within the source card did not show a drag preview");
    paint_requests.clear();
    App::Window(app.hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(start.x+25,start.y+16));
    const auto moved=app.DragPreview();
    const auto scale=float(GetDpiForWindow(app.hwnd))/96;
    Require(moved && std::abs(moved->rectangle.left-first->rectangle.left-5/scale)<.01f &&
            std::abs(moved->rectangle.top-first->rectangle.top-4/scale)<.01f,
            "The moving card did not preserve its pointer offset at this DPI");
    Require(std::ranges::any_of(paint_requests,[&](const auto& request){return request.window==app.hwnd;}),
            "Dragging within one card did not request a repaint");
    App::Window(app.hwnd,WM_MOUSEMOVE,MK_LBUTTON,point(to));
    Require(app.DragPreview() && app.DragPreview()->insertion,"Dragging to a new slot has no insertion marker");
    App::Window(app.hwnd,WM_LBUTTONUP,0,point(to));
    Require(!app.DragPreview(),"Drop left a floating card behind");
  };
  {
    Frame frame(fixture.root);auto& app=frame.app;
    app.layout.width=1080;app.layout.height=900;
    drag(app,0,2);
    Require(app.library.order[0]==1 && app.library.order[1]==2 && app.library.order[2]==0,
            "Dragging a cover did not move the game to the drop position");
    Require(app.selected==0,"Dragging changed the selected game");
    app.rows[0].inspection.state="ready";
    app.ToggleGameHidden(0);frame.Settle();
    Require(app.library.Hidden(0) && app.selected==1 && app.layout.game_count+1==LauncherCatalogue().size(),
            "Hiding the selected installed game did not choose a visible game");
    app.ToggleGameHidden(2);
    Require(app.library.Hidden(2) && app.layout.game_count+2==LauncherCatalogue().size(),
            "A game without data could not be hidden");
    frame.Click(app.layout.hidden_games());
    Require(app.library.show_hidden && app.layout.game_count==LauncherCatalogue().size(),"Eye button did not reveal hidden games");
    app.ToggleGameHidden(2);
    Require(!app.library.Hidden(2),"A revealed game could not be restored");
  }
  {
    const auto before=Ini::ReadText(ConfigPath(fixture.root));
    const std::array edits{IniEdit{"collection","launcher_layout","banner"}};
    UpdateConfig(fixture.root,before,edits);
    Frame frame(fixture.root);auto& app=frame.app;
    app.layout.width=1080;app.layout.height=900;
    Require(app.layout.columns()>1 && app.library.order[2]==0 && app.library.Hidden(0) &&
            !app.library.show_hidden && app.library.visible[0]==1 && app.library.visible[1]==2,
            "A legacy banner config lost its game order or hidden games in the grid");
    drag(app,1,0);
    App::Window(app.hwnd,WM_KEYDOWN,VK_HOME,0);frame.Settle();
    Require(app.library.visible.front()==2 && app.selected==2,"Grid reordering did not update keyboard navigation");
    auto next=app.library;
    for(const auto game:next.order)next.SetHidden(game,true);
    app.SaveLibrary(std::move(next),false);
    Require(app.library.visible.empty() && app.layout.game_count==0 && app.layout.content_height()==0,
            "Hiding all games left cards or scrolling in the empty library");
    App::Window(app.hwnd,WM_KEYDOWN,VK_DOWN,0);
    frame.Click(app.layout.hidden_games());
    Require(app.library.show_hidden && app.layout.game_count==LauncherCatalogue().size(),"The eye button could not restore an empty library view");
  }
}
void ExternalLauncherControlsChecks() {
  Fixture fixture;
  Write(ConfigPath(fixture.root),"[collection]\ncheck_for_updates=false\n[keyboard_p2]\nsouth=B\nright=none\n");
  Write(cave::launcher::SdojRecompExecutable(fixture.root),"");
  const auto launch=cave::launcher::PrepareSdojRecomp(fixture.root,Ini::Load(ConfigPath(fixture.root)),{},96);
  Require(std::ranges::find(launch.arguments,L"--input_backend=sdl")!=launch.arguments.end(),
          "The eandis launch must enable SDL support for HID controllers");
  Require(std::ranges::find(launch.arguments,L"--keyboard_p2_shot=B")!=launch.arguments.end() &&
          std::ranges::find(launch.arguments,L"--keyboard_p2_right=")!=launch.arguments.end() &&
          std::ranges::none_of(launch.arguments,[](const auto& arg){return arg.starts_with(L"--keyboard_p2_left=");}),
          "External P2 overrides must preserve unconfigured upstream bindings");
  {
    Frame frame(fixture.root,"futari");auto& app=frame.app;
    app.layout.width=880;app.layout.height=900;app.layout.scroll=0;
    const auto game=std::ranges::find_if(app.library.visible,[](size_t index){return Catalogue()[index].id=="sdoj";});
    Require(game!=app.library.visible.end(),"The SDOJ switch card is missing");
    const auto position=size_t(game-app.library.visible.begin());
    const auto center=[&](D2D1_RECT_F bounds) {
      const auto scale=float(GetDpiForWindow(app.hwnd))/96;
      return MAKELPARAM(int((bounds.left+bounds.right)/2*scale),int((bounds.top+bounds.bottom)/2*scale));
    };
    const auto press=[&](D2D1_RECT_F bounds,ExternalHit hit) {
      const auto point=center(bounds);
      App::Window(app.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,point);
      Require(!app.card_press && app.external_pressed==hit,"The external control was treated as a card drag");
      return point;
    };
    const auto click=[&](D2D1_RECT_F bounds) {
      const auto point=press(bounds,{ExternalControl::SdojToggle});
      App::Window(app.hwnd,WM_LBUTTONUP,0,point);
      Require(Catalogue()[app.selected].id=="futari","Toggling eandis changed the selected game");
    };
    const auto click_link=[&](D2D1_RECT_F bounds,ExternalHit hit) {
      const auto opens=opened_paths.size();
      const auto selected=app.selected;
      const auto point=press(bounds,hit);
      Require(opened_paths.size()==opens,"The project link opened during mouse down");
      App::Window(app.hwnd,WM_LBUTTONUP,0,point);
      Require(opened_paths.size()==opens+1 && opened_paths.back()==fs::path(hit.url),
              "The link did not open its own project page");
      Require(app.selected==selected,"Clicking a project link changed the selected game");
    };
    const auto keyboard_link=[&](std::wstring_view project) {
      const auto opens=opened_paths.size();
      app.focus=15;SetFocus(app.hwnd);
      MSG tab{};
      tab.hwnd=app.hwnd;tab.message=WM_KEYDOWN;tab.wParam=VK_TAB;
      Require(app.Input(tab) && app.focus==kProjectLinkFocus,"The footer link is missing from Tab navigation");
      App::Window(app.hwnd,WM_KEYDOWN,VK_RETURN,0);
      Require(opened_paths.size()==opens+1 && opened_paths.back()==fs::path(project),"Enter did not activate the footer link");
    };
    click(LayoutSdojSwitch(app.layout.card(position)));
    Require(app.sdoj_external,"The eandis switch did not enable the runtime");
    const auto card=app.layout.card(position);
    const auto cover_switch=LayoutSdojSwitch(card);
    Require(app.ExternalControlAt(card.left+112,card.bottom-18).control==ExternalControl::None,
            "The eandis switch captured a click on the game's English name");
    click({cover_switch.left,cover_switch.top,cover_switch.left+29,cover_switch.bottom});
    Require(!app.sdoj_external,"The eandis switch label did not disable the runtime");
    const auto icon=LayoutProjectIcon(card);
    Require(app.ExternalControlAt(icon.left+4,icon.top+4).control==ExternalControl::None,
            "The icon was still interactive after disabling eandis");
    click({cover_switch.right-16,cover_switch.top,cover_switch.right,cover_switch.bottom});
    Require(app.sdoj_external,"The eandis switch track did not enable the runtime");
    Require(app.SelectedProjectUrl().empty(),"The footer link leaked onto another game");
    const std::wstring_view sdoj_project=cave::launcher::kSdojRecompProject;
    click_link(icon,{ExternalControl::ProjectIcon,sdoj_project});
    app.Select(*game,false);
    Require(app.SelectedProjectUrl()==sdoj_project,"The active eandis game did not expose its own footer link");
    click_link(app.layout.project_link(sdoj_project),{ExternalControl::ProjectLink,sdoj_project});
    keyboard_link(sdoj_project);

    const auto donpachi=std::ranges::find_if(app.library.visible,[](size_t index){return LauncherCatalogue()[index].id=="donpachi";});
    Require(donpachi!=app.library.visible.end(),"The DonPachi card is missing");
    const std::wstring_view donpachi_project=FindArcadeRecomp("donpachi")->project;
    app.Select(*donpachi,false);
    Require(ArcadeNeedsSetup(app.rows[*donpachi].inspection) && app.SelectedProjectUrl()==donpachi_project,
            "The DonPachi project link must be available before installation");
    Require(IsWindowEnabled(app.fullscreen_control) && app.display_choices.size()==2 && app.display_choices[0].fullscreen,
            "DonPachi must be selectable in both its display modes");
    click_link(LayoutProjectIcon(app.layout.card(*app.VisiblePosition())),{ExternalControl::ProjectIcon,donpachi_project});
    click_link(app.layout.project_link(donpachi_project),{ExternalControl::ProjectLink,donpachi_project});
    keyboard_link(donpachi_project);

    Write(DataChild(fixture.root,FindArcadeRecomp("donpachi")->game.executable),"fixture engine");
    Write(fixture.root/"game-data/donpachi/donpachi.zip","fixture ROM");
    SendMessageW(app.fullscreen_control,CB_SETCURSEL,0,0);app.DisplayChanged();
    const auto arcade_ini=Ini::Load(ConfigPath(fixture.root));
    const auto arcade=cave::launcher::PrepareArcadeRecomp(*FindArcadeRecomp("donpachi"),fixture.root,arcade_ini,"fullscreen-check");
    const auto arcade_display=ResolveSharedSettings(arcade_ini);
    Require(arcade_display.fullscreen,"DonPachi fullscreen preference did not save");
    cave::launcher::SetArcadeFullscreen(arcade,arcade_display.fullscreen);
    Require(Ini::Load(arcade.configuration).Get("window","fullscreen")->text=="true","First launch did not create fullscreen settings");
    Write(arcade.configuration,"[window]\r\nscale = 3\r\nfullscreen = true\r\nfullscreen_type = exclusive\r\n[video]\r\nrenderer = direct3d12\r\n");
    SendMessageW(app.fullscreen_control,CB_SETCURSEL,1,0);app.DisplayChanged();
    cave::launcher::SetArcadeFullscreen(arcade,ResolveSharedSettings(Ini::Load(ConfigPath(fixture.root))).fullscreen);
    Require(Ini::ReadText(arcade.configuration)=="[window]\r\nscale = 3\r\nfullscreen = false\r\nfullscreen_type = exclusive\r\n[video]\r\nrenderer = direct3d12\r\n",
            "Changing DonPachi display mode altered upstream settings");
    const auto& ddp = *FindArcadeRecomp("dodonpachi");
    const auto ddp_engine = DataChild(fixture.root, ddp.game.executable);
    Write(ddp_engine, "fixture engine");
    Write(ddp_engine.parent_path() / "ddonpach_jpn.dat", "prepared Japan");
    const auto ddp_launch = PrepareArcadeRecomp(ddp, fixture.root, arcade_ini, "japan-only");
    Require(Ini::Load(ddp_launch.configuration).Get("game", "version")->text == "japan",
            "An installation containing only the Japanese DAT must select Japan");
    const auto archives = fixture.root / "ROM sets";
    const auto world = archives / "ddonpach.zip";
    const auto japan = archives / "ddonpachj.zip";
    const auto seven_zip = archives / "ddonpachj.7z";
    Write(world, "World"); Write(japan, "Japan"); Write(seven_zip, "Unsupported");
    Require(cave::launcher::FindDoDonPachiZip(archives) == world, "Prefer the parent ZIP when given a directory");
    Require(InspectArcadeRecomp(ddp, fixture.root, seven_zip).state == "arcade_rom_missing",
            "A selected 7z cannot be imported even with a cached DAT");
    const auto imported = PrepareArcadeRecomp(ddp, fixture.root, arcade_ini, "zip", japan);
    Require(imported.arguments.at(0) == L"--rom" && imported.arguments.at(1) == japan.native() &&
            Ini::Load(imported.configuration).Get("game", "version")->text == "japan",
            "Pass the selected Japanese ZIP directly to upstream");
    const auto merged = PrepareArcadeRecomp(ddp, fixture.root, arcade_ini, "zip", world);
    Require(merged.arguments.at(1) == world.native(), "Changing ZIP requests a fresh upstream import");
    Write(merged.configuration, "[files]\nrom = " + Utf8(world) + "\n[game]\nversion = japan\n");
    const auto repeat = PrepareArcadeRecomp(ddp, fixture.root, arcade_ini, "zip", world);
    Require(repeat.arguments.front() == L"--settings" &&
            Ini::Load(repeat.configuration).Get("game", "version")->text == "japan",
            "Keep the game's selected region and reuse its DAT on the next launch");
    app.Select(*game,false);
    Require(app.display_choices.size()>2,"Returning to an integrated game lost its window presets");
  }
  Frame reopened(fixture.root);
  Require(reopened.app.sdoj_external,"Reopening the launcher did not restore the eandis switch");
}
void DragCancelChecks() {
  Fixture fixture;Frame frame(fixture.root);auto& app=frame.app;
  app.layout.width=1080;app.layout.height=900;app.layout.scroll=0;
  const auto scale=float(GetDpiForWindow(app.hwnd))/96;
  const auto source=app.layout.card(0),destination=app.layout.card(1);
  const auto at=[&](D2D1_RECT_F r){return MAKELPARAM(int((r.left+30)*scale),int((r.top+30)*scale));};
  const auto saved=Ini::ReadText(ConfigPath(fixture.root));
  const auto selected=app.selected;
  for(const auto cancel:{WM_CANCELMODE,WM_CAPTURECHANGED,WM_KEYDOWN}) {
    App::Window(app.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,at(source));
    App::Window(app.hwnd,WM_MOUSEMOVE,MK_LBUTTON,at(destination));
    App::Window(app.hwnd,cancel,cancel==WM_KEYDOWN?VK_ESCAPE:0,0);
    Require(!app.card_press && !app.DragPreview(),"Cancelled drag kept its floating card");
    App::Window(app.hwnd,WM_LBUTTONUP,0,at(destination));
    Require(app.selected==selected && Ini::ReadText(ConfigPath(fixture.root))==saved,
            "Cancelling a drag changed selection or saved order");
  }
  App::Window(app.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,at(source));
  App::Window(app.hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(-20,-20));
  Require(app.DragPreview() && !app.DragPreview()->insertion,"Outside drop has an insertion target");
  App::Window(app.hwnd,WM_LBUTTONUP,0,MAKELPARAM(-20,-20));
  Require(!app.card_press && Ini::ReadText(ConfigPath(fixture.root))==saved,"Outside drop changed saved order");
  Require(app.DropGameAt(destination.left-2,destination.top+30)==app.library.visible[1],
          "Card gap is not a valid nearby drop target");
}
void PreferenceRepaintChecks() {
  Fixture fixture;Frame frame(fixture.root);auto& app=frame.app;
  Require(GetWindowLongPtrW(app.hwnd,GWL_STYLE)&WS_CLIPCHILDREN,"Parent painting must clip native preference controls");
  // Hidden fixtures are not shown, so explicitly deliver the client-size event.
  RECT client{};GetClientRect(app.hwnd,&client);
  App::Window(app.hwnd,WM_SIZE,SIZE_RESTORED,MAKELPARAM(client.right,client.bottom));
  const auto scale=float(GetDpiForWindow(app.hwnd))/96;
  for(bool hover:{false,true}) {
    // Hidden HWNDs need not retain an update region. Observe the production
    // invalidation request instead, while still forwarding it to User32.
    paint_requests.clear();
    if(hover) {
      const auto card=app.layout.card(1);
      const auto top=std::max(card.top,app.layout.header());
      const auto bottom=std::min(card.bottom,app.layout.footer());
      Require(bottom>top,"Hover fixture card is outside the viewport");
      App::Window(app.hwnd,WM_MOUSEMOVE,0,MAKELPARAM(int((card.left+20)*scale),int((top+bottom)/2*scale)));
      Require(app.hover==1,"Hover fixture did not reach its visible game card");
    }
    else app.Scroll(90);
    unsigned parent_requests{};
    for(const auto& request:paint_requests) {
      if(request.window!=app.hwnd)continue;
      ++parent_requests;
      Require(!request.whole && !request.erase &&
              request.area.top>=std::floor((app.layout.header()-2)*scale) &&
              request.area.bottom<=std::ceil(app.layout.footer()*scale),
              "Scroll/hover repaint reaches native nickname/fullscreen controls");
    }
    Require(parent_requests>0,"Game-list interaction did not request a repaint");
  }
  Require(unexpected_dialogs==0,"Repaint checks opened a dialog");
}
void FooterHoverChecks() {
  Fixture fixture;Frame frame(fixture.root);auto& app=frame.app;
  RECT client{};GetClientRect(app.hwnd,&client);
  App::Window(app.hwnd,WM_SIZE,SIZE_RESTORED,MAKELPARAM(client.right,client.bottom));
  const auto scale=float(GetDpiForWindow(app.hwnd))/96;
  const auto move=[&](int slot) {
    const auto r=app.layout.focus_button(slot);
    App::Window(app.hwnd,WM_MOUSEMOVE,0,MAKELPARAM(int((r.left+r.right)/2*scale),int((r.top+r.bottom)/2*scale)));
  };
  const auto check_repaint=[&](size_t index,int slot) {
    const auto r=app.layout.focus_button(slot);
    const auto& request=paint_requests.at(index);
    Require(request.window==app.hwnd && !request.whole && !request.erase &&
            request.area.left==LONG(std::floor(r.left*scale))-1 && request.area.top==LONG(std::floor(r.top*scale))-1 &&
            request.area.right==LONG(std::ceil(r.right*scale))+1 && request.area.bottom==LONG(std::ceil(r.bottom*scale))+1,
            "Footer hover repainted outside its button");
  };
  app.focus=5;
  paint_requests.clear();
  move(6);
  Require(app.button_hover==6 && paint_requests.size()==1,"Entering a footer button did not set hover");
  check_repaint(0,6);
  move(6);
  Require(paint_requests.size()==1,"Moving inside the same button repainted it");
  for(int slot:{7,13,4,5,9,8}) {
    const auto previous=app.button_hover;
    paint_requests.clear();
    move(slot);
    Require(app.button_hover==slot && paint_requests.size()==2,"Moving between footer buttons did not transfer hover");
    check_repaint(0,previous);
    check_repaint(1,slot);
  }
  paint_requests.clear();
  App::Window(app.hwnd,WM_MOUSELEAVE,0,0);
  Require(app.button_hover==-1 && app.focus==5 && paint_requests.size()==1,"Leaving the window did not clear hover independently of keyboard focus");
  check_repaint(0,8);

  app.layout={1080,900,0};
  auto& inspection=app.rows[app.selected].inspection;
  inspection.state="ready";inspection.data_state=DataState::Verified;
  ComPtr<IWICBitmap> bitmap;
  Check(app.renderer.wic->CreateBitmap(1080,900,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap),"Create hover bitmap");
  ComPtr<ID2D1RenderTarget> target;
  Check(app.renderer.factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(),&target),"Create hover target");
  target->SetDpi(96,96);
  const auto pixel=[&](int slot,int hovered,bool busy) {
    target->BeginDraw();
    app.renderer.Draw(target.Get(),app.layout,app.rows,app.selected,-1,busy,L"",{},false,0,{},&app.library,false,nullptr,hovered);
    Check(target->EndDraw(),"Render button hover");
    const auto r=app.layout.focus_button(slot);
    WICRect sample{INT(r.left)+9,INT(r.top)+6,1,1};
    std::array<BYTE,4> bgra{};
    Check(bitmap->CopyPixels(&sample,4,4,bgra.data()),"Read hover color");
    return bgra;
  };
  Require(pixel(4,-1,false)!=pixel(4,4,false),"Hovered secondary button did not change color");
  Require(pixel(8,-1,false)!=pixel(8,8,false),"Hovered launch button did not change color");
  Require(pixel(8,-1,true)==pixel(8,8,true),"Disabled launch button changed color on hover");
}
void InputBindingChecks() {
  using namespace cave::launcher::controls;
  const PadSnapshot idle{{0,0},{0,0},{0}},pressed{{0,1},{0,0},{0}},hat{{0,0},{0,0},{8}},axis{{0,0},{-23000,0},{0}};
  Require(CapturePad(idle,pressed,idle,false,false)=="b1" && CapturePad(pressed,pressed,idle,false,false).empty(),
          "Controller capture must accept new button presses, not buttons already held");
  Require(CapturePad(idle,hat,idle,false,false)=="h0.8" && PadSourceActive("h0.8",hat,false),"Arcade stick hat capture failed");
  Require(CapturePad(idle,axis,idle,false,false)=="-a0" && CapturePad(idle,axis,idle,true,false)=="a0~" &&
          PadSourceActive("-a0",axis,false),"Controller axis polarity was lost");
  const auto map=ParsePadMap("a:b1,dpleft:h0.8,rightx:a0~,platform:Windows,crc:0000,");
  Require(map.size()==3 && ParsePadMap(SerializePadMap(map))==map,"Controller mapping lost button, hat or axis bindings");
}
#include "instance_checks.h"
#include "visual_checks.h"
void ColdReadback(const fs::path& root,std::wstring_view expected_language,std::wstring_view nickname,std::wstring_view game) {
  const auto before=Ini::ReadText(ConfigPath(root));
  Frame frame(root);
  Require(cave::launcher::LanguageCode(language)==Utf8(expected_language),"Cold language readback");
  Require(ControlText(frame.app.nickname_control)==nickname,"Cold nickname readback");
  Require(Catalogue()[frame.app.selected].id==Utf8(game),"Independent cold process did not restore the selected game");
  Require(!frame.app.SelectedDisplay().fullscreen && frame.app.SelectedDisplay().width==1280 &&
          frame.app.SelectedDisplay().height==720,"Cold display selection readback");
  Require(ControlText(frame.app.save_control)==T(L"保存"),"Cold localized controls");
  Require(Ini::ReadText(ConfigPath(root))==before,"Readback changed configuration");
  Require(ResolveSettings(Ini::Load(ConfigPath(root)),"sdoj").sdoj_frame_delay==0,
          "Cold startup did not resolve SDOJ's default 0V");
  Require(ResolveSettings(Ini::Load(ConfigPath(root)),"espgaluda2").g2_frame_delay==0,
          "Cold startup did not resolve G2's default 0V");
}
void Restart(const fs::path& root,std::wstring_view code,std::wstring_view nickname,std::wstring_view game) {
  std::wstring executable(32768,L'\0');
  const auto size=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
  Require(size && size<executable.size(),"Locate cold-start test program");executable.resize(size);
  auto command=QuoteArgument(executable)+L" --readback "+QuoteArgument(root.native())+L" "+QuoteArgument(code)+L" "+QuoteArgument(nickname)+L" "+QuoteArgument(game);
  STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
  Require(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Start independent readback process");
  CloseHandle(process.hThread);
  const auto waited=WaitForSingleObject(process.hProcess,10000);DWORD exit_code=1;
  GetExitCodeProcess(process.hProcess,&exit_code);CloseHandle(process.hProcess);
  Require(waited==WAIT_OBJECT_0 && exit_code==0,"Independent process did not read back saved settings");
}
void ArtworkStateChecks() {
  Renderer renderer;
  ComPtr<IWICBitmap> bitmap;
  Check(renderer.wic->CreateBitmap(64,64,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap),"Create grey artwork fixture");
  ComPtr<ID2D1RenderTarget> target;
  Check(renderer.factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(),&target),"Create grey artwork surface");
  const auto grey=renderer.Image(target.Get(),0,true);
  Require(grey==renderer.Image(target.Get(),0,true),"Grey artwork is decoded once per target");
  Require(grey!=renderer.Image(target.Get(),0,false),"Colour artwork remains available after data import");
  for(size_t index=0;index<LauncherCatalogue().size();++index) {
    const auto cover=renderer.Image(target.Get(),index,false);
    const auto missing=renderer.Image(target.Get(),index,true);
    Require(cover==renderer.Image(target.Get(),index,false) && missing==renderer.Image(target.Get(),index,true),
            "Cover repaint must reuse decoded colour and grey images");
    Require(cover!=missing,"Missing-data covers lost their independent colour image");
  }
  target->BeginDraw();target->Clear(D2D1::ColorF(0xffffff));
  target->DrawBitmap(grey,{0,0,64,64});
  Check(target->EndDraw(),"Render actual grey artwork");
  std::array<BYTE,64*64*4> pixels{};
  Check(bitmap->CopyPixels(nullptr,64*4,UINT(pixels.size()),pixels.data()),"Read grey artwork output");
  unsigned minimum=255,maximum=0;
  for(size_t index=0;index<pixels.size();index+=4) {
    Require(pixels[index]==pixels[index+1] && pixels[index+1]==pixels[index+2],"Missing-data artwork must be neutral grey");
    minimum=std::min(minimum,unsigned(pixels[index]));maximum=std::max(maximum,unsigned(pixels[index]));
  }
  Require(maximum-minimum>40,"Grey artwork must preserve image detail");
}
void GameExitChecks() {
  using cave::launcher::ExitButtons;
  Fixture fixture;
  const auto config=ConfigPath(fixture.root);
  Write(config,"[collection]\ncheck_for_updates=false\n");
  Frame frame(fixture.root,"futari");
  auto& app=frame.app;
  const auto defaults=ReadGameExitShortcuts(Ini::Load(config),fixture.root,"futari");
  Require(defaults.keyboard==ExitKeys{VK_ESCAPE} && defaults.buttons==ExitButtons{"start","dpup"},"Game exit defaults");
  for(const auto game:{"donpachi","dodonpachi"}) {
    const auto shortcuts=ReadGameExitShortcuts(Ini::Load(config),fixture.root,game);
    Require(shortcuts.keyboard==ExitKeys{} && shortcuts.buttons==ExitButtons{"start","dpup"},
            "Escape must reach the arcade game's own menu by default");
  }
  Write(config,"[collection]\ngame_exit_key=F12\n");
  for(const auto game:{"donpachi","dodonpachi"})
    Require(ReadGameExitShortcuts(Ini::Load(config),fixture.root,game).keyboard==ExitKeys{VK_F12},
            "A recorded exit key must also apply to the arcade games");
  Write(config,"[collection]\nbig_picture_exit_key= F12\nbig_picture_exit_buttons= START + Select\n");
  Require(ReadGameExitShortcuts(Ini::Load(config),fixture.root,"futari").keyboard==ExitKeys{VK_F12},"Legacy single exit key");
  Write(config,"[collection]\ngame_exit_key=Control + F12\nbig_picture_exit_key=Escape\nbig_picture_exit_buttons=START + Select\n");
  const auto custom=ReadGameExitShortcuts(Ini::Load(config),fixture.root,"futari");
  Require(custom.keyboard==ExitKeys{VK_CONTROL,VK_F12} && custom.buttons==ExitButtons{"start","back"},"Custom game exit bindings");
  Write(config,"[collection]\ngame_exit_key=none\nbig_picture_exit_key=F12\nbig_picture_exit_buttons=none\n");
  const auto disabled=ReadGameExitShortcuts(Ini::Load(config),fixture.root,"futari");
  Require(disabled.keyboard==ExitKeys{} && disabled.buttons.empty(),"Game exit shortcuts could not be disabled");
  Require(!cave::launcher::ParseExitButtons("Start + Start") && !cave::launcher::ParseExitButtons("Start +"),
          "Big Picture accepted an invalid controller combination");
  using cave::launcher::ExitPadBinding;
  using cave::launcher::PressedExitPadInputs;
  using cave::launcher::RecordExitBinding;
  using cave::launcher::ParseExitPadBinding;
  const cave::launcher::controls::PadSnapshot neutral{{0,0},{0},{0}};
  const auto press=[](std::string& first,const std::vector<std::string>& held) {return RecordExitBinding<std::string>(first,held);};
  std::string first;
  Require(!press(first,PressedExitPadInputs(neutral,{{1,0},{0},{0}})) &&
          press(first,PressedExitPadInputs(neutral,{{0,1},{0},{0}}))==ExitPadBinding{"b0"},"Separate presses recorded as a chord");
  first.clear();
  Require(!press(first,{"b0"}) && press(first,{"b0","b1"})==ExitPadBinding{"b0","b1"},"Two overlapping buttons did not bind");
  Require(PressedExitPadInputs(neutral,{{0,1},{-32768},{0}})==std::vector<std::string>{"b1","-a0"} &&
          PressedExitPadInputs(neutral,{{1,0},{0},{1}})==std::vector<std::string>{"b0","h0.1"},"Arcade stick or D-pad chord did not bind");
  Require(PressedExitPadInputs({{1,0},{0},{0}},{{1,1},{0},{0}})==std::vector<std::string>{"b1"},"An input held before rebinding entered the chord");
  Require(ParseExitPadBinding("b1")==ExitPadBinding{"b1"} && ParseExitPadBinding("b1,+a0")==ExitPadBinding{"b1","+a0"} &&
          !ParseExitPadBinding("b1,b1"),"Stored controller binding did not round-trip");
  std::uint16_t first_key{};
  const std::array control{std::uint16_t(VK_CONTROL)};
  Require(!RecordExitBinding<std::uint16_t>(first_key,control) &&
          RecordExitBinding<std::uint16_t>(first_key,{})==ExitKeys{VK_CONTROL},"Keyboard tap could not bind a single key");
  first_key=0;
  Require(!RecordExitBinding<std::uint16_t>(first_key,control) &&
          RecordExitBinding<std::uint16_t>(first_key,custom.keyboard)==custom.keyboard,"Keyboard overlap did not record a chord");

  const auto create_game=[] {
    const auto window=CreateWindowExW(0,L"STATIC",L"Game exit fixture",WS_OVERLAPPEDWINDOW,
                                      0,0,200,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Require(window!=nullptr,"Create hidden game window");
    return window;
  };
  auto game=create_game();
  const auto pid=GetCurrentProcessId();
  const auto check_close=[&](bool expected,const char* reason) {
    MSG message{};
    const bool found=PeekMessageW(&message,game,WM_CLOSE,WM_CLOSE,PM_REMOVE) && message.message==WM_CLOSE;
    Require(found==expected,reason);
    if(found)DispatchMessageW(&message);
  };
  update_foreground=game;
  app.game_exit_shortcuts=defaults;
  app.child.emplace(nullptr,pid);
  app.active_game="futari";
  app.GameExitKey(VK_ESCAPE,false);
  check_close(true,"Desktop-launched game did not close from a single key press");
  app.GameExitKey(VK_ESCAPE,true);
  app.ChildTick();
  Require(!app.child && !app.BigPicture(),"Desktop game exit changed the launcher mode");

  game=create_game();
  update_foreground=game;
  app.SetBigPicture(true);
  const auto selected=app.selected;
  app.game_exit_shortcuts=custom;
  app.child.emplace(nullptr,pid+1);
  app.GameExitKey(VK_LCONTROL,false);
  app.GameExitKey(VK_F12,false);
  check_close(false,"Exit shortcut affected a window from another process");

  app.child.emplace(nullptr,pid);
  app.active_game="futari";
  app.GameExitKey(VK_F12,false);
  check_close(false,"A held keyboard chord closed the game on focus");
  app.GameExitKey(VK_F12,true);
  app.GameExitKey(VK_LCONTROL,true);
  app.GameExitKey(VK_F12,false);
  app.GameExitKey(VK_ESCAPE,false);
  check_close(false,"An incomplete chord or former exit key closed the game");
  held_exit_key=VK_F12;
  app.GameExitKey(VK_LCONTROL,false);
  check_close(true,"The full keyboard chord did not request a normal game window close");
  Require(!IsWindow(game),"The game window did not handle WM_CLOSE");
  update_foreground=app.hwnd;
  const auto previous_focus_requests=foreground_requests;
  app.ChildTick();
  Require(!app.child && app.BigPicture() && app.selected==selected && foreground_requests==previous_focus_requests+1,
          "Game exit did not return focus to the same Big Picture card");
  Require(app.exit_keys_awaiting_release==ExitKeys{0,VK_F12},"The exit key was not held until release on return");
  held_exit_key=0;
  app.MenuControllerTick();
  app.BigPictureKeyboard(VK_ESCAPE,false);
  Require(app.menu_open,"A new Esc press was ignored after returning");
  update_foreground=nullptr;
}
// Model a third-party game that destroys its last window but stalls during shutdown.
void LingeringGameHost() {
  const auto title=L"EVAC lingering game fixture "+std::to_wstring(GetCurrentProcessId());
  const auto window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",title.c_str(),WS_POPUP|WS_VISIBLE,
                                    -30000,-30000,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  Require(window!=nullptr,"Create lingering game window");
  const auto deadline=GetTickCount64()+15000;
  while(GetTickCount64()<deadline) {
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))DispatchMessageW(&message);
    Sleep(10);
  }
}
void ExternalExitCleanupChecks() {
  Fixture fixture;
  Frame frame(fixture.root,"sdoj");
  auto& app=frame.app;
  wchar_t executable[32768]{};
  Require(GetModuleFileNameW(nullptr,executable,std::size(executable))!=0,"Locate lingering game fixture");
  LaunchSpec spec{};
  spec.paths.root=fixture.root;spec.executable=executable;spec.arguments={L"--lingering-game"};
  app.child.emplace(StartGame(spec));app.active_game="sdoj";
  app.external_session.emplace(fixture.root);
  const auto title=L"EVAC lingering game fixture "+std::to_wstring(app.child->pid());
  HWND window{};
  for(unsigned i=0;i<400 && !window;++i){window=FindWindowW(L"STATIC",title.c_str());if(!window)Sleep(10);}
  Require(window && !AllProcessWindowsClosed(app.child->pid()),"Even an owned or tool window must protect a running game");
  app.RequestGameExit(window,"keyboard");
  for(unsigned i=0;i<400 && IsWindow(window);++i)Sleep(10);
  Require(!IsWindow(window) && !app.child->ExitCode(),"Fixture did not survive closing its last window");
  app.FinishEandisExit(1000);
  app.FinishEandisExit(10999);
  Require(!app.child->ExitCode() && !app.eandis_exit_finished,"Graceful shutdown was interrupted");
  app.FinishEandisExit(11000);
  Require(app.eandis_exit_finished,"Stalled windowless eandis process was not cleaned up");
  app.child->Wait();
  Require(app.child->ExitCode()==ERROR_CANCELLED,"Unexpected external cleanup exit code");
  app.ChildTick();
  Require(!app.child && !app.external_session && app.message.empty(),"Could not return to library after requested cleanup");
}
void BigPictureBindingChecks() {
  Fixture fixture;
  const auto config=ConfigPath(fixture.root);
  const std::string remap="[keyboard]\nstart=1\n[game.futari.keyboard]\nstart=2\n";
  const auto check=[&](Frame& frame,WPARAM confirm,WPARAM ignored) {
    auto& app=frame.app;
    app.SetBigPicture(true);
    app.rows[app.selected].inspection.state="needs_verification";
    App::Window(app.hwnd,WM_KEYDOWN,ignored,0);
    Require(!app.work,"Big Picture used the previous or per-game Start binding");
    App::Window(app.hwnd,WM_KEYDOWN,confirm,1L<<30);
    Require(!app.work,"Holding Start requested another launch");
    App::Window(app.hwnd,WM_KEYDOWN,confirm,0);
    Require(app.work && app.work->starting,"Configured Start did not launch the selected game");
    frame.Settle();
  };
  {
    Frame frame(fixture.root,"futari");
    Write(config,remap);
    frame.app.ReloadPreferences();
    check(frame,'1',VK_RETURN);
  }
  {
    Frame frame(fixture.root,"futari");
    check(frame,'1','2');
    Write(config,"[collection]\ncheck_for_updates=false\n");
    frame.app.ReloadPreferences();
    check(frame,VK_RETURN,'1');
  }
}
void MainChecks() {
  ArtworkStateChecks();
  const auto module=GetModuleHandleW(nullptr);
  for(const auto size:{16,20,24,32,40,48,64,128,256}) {
    const auto icon=BrandIcon(module,size);ICONINFO info{};
    Require(GetIconInfo(icon,&info)!=FALSE,"Decode embedded icon size");
    BITMAP bitmap{};const auto okay=GetObjectW(info.hbmColor,sizeof(bitmap),&bitmap);
    if(info.hbmColor)DeleteObject(info.hbmColor);if(info.hbmMask)DeleteObject(info.hbmMask);
    Require(okay && bitmap.bmWidth==size && bitmap.bmHeight==size,"Embedded icon has the wrong size");
  }
  const auto logo_resource=FindResourceW(nullptr,MAKEINTRESOURCEW(cave::launcher::kBrandLogoResource),RT_RCDATA);
  Require(logo_resource!=nullptr,"Missing embedded logo");
  const auto logo_bytes=SizeofResource(nullptr,logo_resource);
  const auto logo_data=static_cast<BYTE*>(LockResource(LoadResource(nullptr,logo_resource)));
  std::ifstream logo_file(fs::path(CAVE_TEST_SOURCE_ROOT)/"launcher/branding/evac-logo.png",std::ios::binary);
  const std::string logo_source{std::istreambuf_iterator<char>(logo_file),{}};
  Require(logo_data && logo_bytes==logo_source.size() && std::memcmp(logo_data,logo_source.data(),logo_bytes)==0,
          "Embedded logo differs from the authored asset");
  {
    Renderer renderer;ComPtr<IWICStream> stream;Check(renderer.wic->CreateStream(&stream),"Create logo test stream");
    Check(stream->InitializeFromMemory(logo_data,logo_bytes),"Open logo resource");
    ComPtr<IWICBitmapDecoder> decoder;Check(renderer.wic->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder),"Decode logo resource");
    ComPtr<IWICBitmapFrameDecode> frame;Check(decoder->GetFrame(0,&frame),"Get logo test frame");
    UINT width{},height{};Check(frame->GetSize(&width,&height),"Logo test dimensions");
    ComPtr<IWICFormatConverter> converter;Check(renderer.wic->CreateFormatConverter(&converter),"Create logo test converter");
    Check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Convert logo test frame");
    std::vector<BYTE> pixels(size_t(width)*height*4);Check(converter->CopyPixels(nullptr,width*4,UINT(pixels.size()),pixels.data()),"Read logo alpha");
    size_t transparent{},foreground{};for(size_t index=3;index<pixels.size();index+=4) {transparent+=pixels[index]==0;foreground+=pixels[index]>=240;}
    Require(pixels[3]==0 && transparent>size_t(width)*height/10 && foreground>size_t(width)*height/10,
            "Logo is not a transparent foreground cutout");
  }
  Fixture fixture;
  const std::string before="\xef\xbb\xbf; preserve comment\r\n[collection]\r\nconfig_version=1\r\nlanguage=zh\r\nlauncher_layout=banner\r\n"
      "[display]\r\nfullscreen=true\r\n[community]\r\nnickname=Baseline\r\n"
      "[game.futari.display]\r\nfullscreen=true\r\n[future]\r\nkey=preserved\r\n";
  Write(fixture.root/"config.ini",before);
  {
    Frame frame(fixture.root);auto& app=frame.app;
    Require(!fs::exists(fixture.root/"config.ini") && Ini::ReadText(ConfigPath(fixture.root))==before,"Real app startup did not preserve migration bytes");
    for(const auto dimensions:std::array{std::pair{685.f,520.f},std::pair{720.f,520.f},std::pair{1080.f,900.f}}) {
      app.layout.width=dimensions.first;app.layout.height=dimensions.second;
      const auto brand=app.layout.brand();
      Require(app.layout.nickname().bottom==brand.bottom && app.layout.save().bottom==brand.bottom,
              "Nickname controls must bottom-align with the logo");
      Require(app.layout.header()-brand.bottom==24,"Unexpected spacing between the logo and cover grid");
      Require(brand.right<app.layout.language_choice(0).left && brand.bottom<app.layout.fullscreen().top &&
              brand.left>=0 && brand.top>=0,"Logo overlaps the launcher controls");
      const std::array controls{app.layout.data(),app.layout.file_upload(),app.layout.controls(),app.layout.config(),app.layout.logs(),app.layout.big_picture_button(),app.layout.play()};
      for(size_t index=0;index<controls.size();++index) {
        const auto r=controls[index];
        Require(r.top>=app.layout.footer() && r.bottom<=app.layout.height && r.left>=0 && r.right<=app.layout.width,"Utility control is outside bottom row");
        Require(r.bottom==app.layout.play().bottom,"Launch and utility buttons do not bottom-align");
        for(size_t other=index+1;other<controls.size();++other)
          Require(r.right<=controls[other].left || controls[other].right<=r.left,"Utility controls overlap");
      }
      Require(app.layout.language_choice(0).top==brand.top && app.layout.language_choice(1).left-app.layout.language_choice(0).left==40,
              "Language choices are not compact and top-aligned");
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
    const auto display=std::find(app.display_choices.begin(),app.display_choices.end(),DisplaySelection{false,1280,720});
    Require(display!=app.display_choices.end(),"1280x720 display choice is missing");
    SendMessageW(app.fullscreen_control,CB_SETCURSEL,WPARAM(std::distance(app.display_choices.begin(),display)),0);
    nickname_responses.push_back(IDYES);
    App::Window(app.hwnd,WM_COMMAND,MAKEWPARAM(kSaveControl,BN_CLICKED),0);frame.Settle();
    const auto settings=ResolveSharedSettings(Ini::Load(ConfigPath(fixture.root)));
    Require(settings.nickname=="Draft-友人" && !settings.fullscreen,"Save handler did not persist quick settings");
    Require(cave::launcher::ReadLanguage(Ini::Load(ConfigPath(fixture.root)))==Language::Japanese,"Quick settings overwrote language");
    Require(ResolveSettings(Ini::Load(ConfigPath(fixture.root)),"futari").fullscreen,"UI save changed game-specific override");
    Require(Ini::ReadText(ConfigPath(fixture.root)).find("key=preserved\r\n")!=std::string::npos,"UI save lost unknown text");
    frame.Click(app.layout.config());Require(opened_paths.back()==fixture.root/"user-data/config.ini","Config button opened the wrong file");
    static unsigned controls_opened{};
    app.show_controls=[](HWND,const fs::path&,const Game& game,Language lang) {
      Require(game.id=="futari" && lang==Language::Japanese,"Controls opened with the wrong game or language");
      ++controls_opened;return false;
    };
    frame.Click(app.layout.controls());
    app.focus=13;App::Window(app.hwnd,WM_KEYDOWN,VK_RETURN,0);
    Require(controls_opened==2,"Controls are not accessible by both mouse and keyboard");
    static fs::path reports_root;
    app.show_reports=[](HWND,const fs::path& root,Language lang,std::string_view preferred){
      Require(lang==Language::Japanese && preferred.empty(),"Unexpected upload dialog language or filter");
      reports_root=root;
    };
    frame.Click(app.layout.logs());Require(reports_root==fixture.root,"Upload logs opened for the wrong collection");
    static fs::path file_upload_root;
    app.show_file_upload=[](HWND,const fs::path& root,Language lang,std::string_view game){
      Require(lang==Language::Japanese && game=="futari","Upload file opened with the wrong game or language");
      file_upload_root=root;
    };
    frame.Click(app.layout.file_upload());
    Require(file_upload_root==fixture.root,"Upload file opened for the wrong collection");
    const auto text=Ini::ReadText(ConfigPath(fixture.root));
    App::Window(app.hwnd,WM_KEYDOWN,VK_F5,0);frame.Settle();
    Require(language==Language::Japanese && ControlText(app.nickname_control)==L"Draft-友人" && Ini::ReadText(ConfigPath(fixture.root))==text,"Refresh changed saved preferences");
    const auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(cave::launcher::kCoverArtwork[3].resource),RT_RCDATA);
    Require(resource!=nullptr,"Missing SDOJ resource");
    const auto count=SizeofResource(nullptr,resource);const auto data=LockResource(LoadResource(nullptr,resource));
    std::ifstream file(fs::path(CAVE_TEST_SOURCE_ROOT)/"launcher/assets/covers/sdoj.jpg",std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(file),{}};
    Require(data && count==source.size() && std::memcmp(data,source.data(),count)==0,"Embedded SDOJ picture differs from authored asset");
  }
  Restart(fixture.root,L"ja",L"Draft-友人");
  for(const auto index:{1u,0u}) {
    {Frame frame(fixture.root);frame.Click(frame.app.layout.language_choice(index));}
    Restart(fixture.root,index==1?L"en":L"zh",L"Draft-友人");
  }
  Require(unexpected_dialogs==0 && opened_paths.size()==1,"Unexpected shell or dialog side effects");
  SelectionChecks();
  WindowSizeChecks();
  {
    Fixture fixture;
    for(const auto extent : {std::pair{1280.f,720.f},std::pair{640.f,480.f},std::pair{480.f,640.f},std::pair{1440.f,2560.f}}) {
      const BigPictureLayout view{extent.first,extent.second,12,6};
      for(size_t i=view.First();i<view.Last();++i) {
        const auto card=view.Card(i);
        Require(card.left>=0 && card.right<=view.width && card.top>=0 &&
            card.bottom<=view.height-119*view.Scale(),"Big Picture cover overlaps the window or controls");
        const auto narrow=view.Cover(i,0.70f),wide=view.Cover(i,0.806f);
        Require(std::abs(narrow.top-wide.top)<0.01f && std::abs(narrow.bottom-wide.bottom)<0.01f &&
            narrow.left>wide.left && narrow.right<wide.right,
            "Big Picture covers with different proportions must share top and bottom edges");
      }
      if(view.Portrait()) {
        const auto first=view.Card(view.First()),right=view.Card(view.First()+1),below=view.Card(view.First()+2);
        Require(view.PageSize()==4 && first.right<right.left && first.bottom<below.top && first.left==below.left,
                "Portrait Big Picture did not show a two-by-two grid");
        Require(view.Move(MenuKey::Up)==4 && view.Move(MenuKey::Down)==8 &&
                view.Move(MenuKey::Left)==3 && view.Move(MenuKey::Right)==7,
                "Portrait Big Picture did not navigate rows, columns, and adjacent pages");
      } else
        Require(view.Move(MenuKey::Left)==5 && view.Move(MenuKey::Right)==7 &&
                view.Move(MenuKey::Up)==6-view.Columns() && view.Move(MenuKey::Down)==6+view.Columns(),
                "Horizontal Big Picture did not keep its existing navigation");
    }
    const BigPictureLayout first_page{480,640,13,2}, before_last_page{480,640,13,11}, last_page{480,640,13,12};
    Require(first_page.Move(MenuKey::Left)==2 && before_last_page.Move(MenuKey::Right)==12 &&
            last_page.Move(MenuKey::Right)==12 && last_page.Move(MenuKey::Left)==9,
            "Portrait paging must stop at the library ends and select an available game on an incomplete page");
    Write(ConfigPath(fixture.root),"[collection]\ncheck_for_updates=false\nbig_picture_layout=horizontal\n");
    Frame frame(fixture.root,"futari");
    RECT client{};GetClientRect(frame.app.hwnd,&client);
    App::Window(frame.app.hwnd,WM_SIZE,SIZE_RESTORED,MAKELPARAM(client.right,client.bottom));
    frame.Click(frame.app.layout.big_picture_button());
    Require(frame.app.BigPicture() && (GetWindowLongPtrW(frame.app.hwnd,GWL_STYLE)&WS_POPUP),
        "Big Picture entry did not open a full-screen window");
    const auto before=frame.app.VisiblePosition().value();
    App::Window(frame.app.hwnd,WM_KEYDOWN,VK_RIGHT,0);
    Require(frame.app.VisiblePosition()==before+1,"Big Picture input did not move game focus");
    frame.app.big_picture_mode=cave::launcher::ReadBigPictureMode(Ini::Parse("[collection]\nbig_picture_layout=vertical\n"));
    App::Window(frame.app.hwnd,WM_KEYDOWN,VK_RIGHT,0);
    Require(frame.app.BigView().Page()==1 && frame.app.VisiblePosition()==before+4,
            "Right from the right column did not select the same row on the next portrait page");
    App::Window(frame.app.hwnd,WM_KEYDOWN,VK_LEFT,0);
    Require(frame.app.BigView().Page()==0 && frame.app.VisiblePosition()==before+1,
            "Left from the left column did not select the same row on the previous portrait page");
    App::Window(frame.app.hwnd,WM_KEYDOWN,VK_DOWN,0);
    Require(frame.app.BigView().PageSize()==4 && frame.app.VisiblePosition()==before+3,
            "Portrait Big Picture did not select the next row with Down");
    App::Window(frame.app.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0);
    Require(frame.app.VisiblePosition()==before+1,"Vertical Big Picture did not follow the mouse wheel");
    App::Window(frame.app.hwnd,WM_KEYDOWN,VK_RETURN,0);
    Require(!frame.app.work && !frame.app.child,"Big Picture began launching missing game data");
    App::Window(frame.app.hwnd,WM_KEYDOWN,VK_ESCAPE,0);
    Require(frame.app.menu_open,"Big Picture Back did not open the exit menu");
    App::Window(frame.app.hwnd,WM_KEYDOWN,VK_RETURN,0);
    Require(!frame.app.BigPicture() && (GetWindowLongPtrW(frame.app.hwnd,GWL_STYLE)&WS_OVERLAPPEDWINDOW),
        "Big Picture exit did not restore the desktop layout");
    Require(frame.app.VisiblePosition()==before+1,"Returning to desktop lost game focus");
  }
  CoverLayoutChecks();
  NicknameWarningChecks();
  NicknameAlignmentChecks();
  PreferenceRepaintChecks();
  FooterHoverChecks();
  InputBindingChecks();
  BigPictureBindingChecks();
  ExternalExitCleanupChecks();
  DisplayDropdownChecks();
  GameLibraryChecks();
  GameExitChecks();
  ExternalLauncherControlsChecks();
  DragCancelChecks();
  InstanceChecks();
  UpdateChecks();
}
}
// Exercise the actual modal settings page, including its draft and Save path.
void CheckControllerSwapDialog(HWND owner,const fs::path& root) {
  static thread_local HWND owner_window;
  static thread_local std::wstring before,after;
  owner_window=owner;
  before.clear();after.clear();
  const auto callback=[](HWND,UINT,UINT_PTR timer,DWORD) {
    KillTimer(nullptr,timer);
    auto dialog=GetWindow(owner_window,GW_ENABLEDPOPUP);
    if(!dialog || dialog==owner_window) {
      dialog=FindWindowW(L"EvacInputSettings",L"Input settings");
      if(!dialog || GetWindow(dialog,GW_OWNER)!=owner_window) {
        PostQuitMessage(1);
        return;
      }
    }
    const auto tab=FindWindowExW(dialog,nullptr,L"Button",L"Controller / stick");
    SendMessageW(tab,BM_CLICK,0,0);
    const auto switcher=FindWindowExW(dialog,nullptr,L"Button",L"Swap 1P / 2P");
    HWND combo=nullptr;
    for(HWND item=nullptr;(item=FindWindowExW(dialog,item,L"ComboBox",nullptr));) {
      if(!IsWindowVisible(item))continue;
      const auto n=SendMessageW(item,CB_GETCOUNT,0,0);
      for(LRESULT i=0;i<n;++i) {
        const auto length=SendMessageW(item,CB_GETLBTEXTLEN,i,0);
        if(length<=0)continue;
        std::wstring name(size_t(length)+1,L'\0');
        SendMessageW(item,CB_GETLBTEXT,i,reinterpret_cast<LPARAM>(name.data()));
        if(!name.starts_with(L"1P · "))continue;
        before=name;
        combo=item;
        SendMessageW(switcher,BM_CLICK,0,0);
        const auto updated=SendMessageW(combo,CB_GETLBTEXTLEN,i,0);
        if(updated>0) {
          after.resize(size_t(updated)+1);
          SendMessageW(combo,CB_GETLBTEXT,i,reinterpret_cast<LPARAM>(after.data()));
        }
        break;
      }
      if(combo)break;
    }
    if(combo)SendMessageW(dialog,WM_COMMAND,IDOK,0);
    if(IsWindow(dialog))SendMessageW(dialog,WM_COMMAND,IDCANCEL,0);
  };
  Require(SetTimer(nullptr,0,80,callback)!=0,"Arm controller dialog acceptance");
  const bool saved=cave::launcher::controls::Show(owner,root,FindGame("futari"),Language::English);
  Require(saved && before.starts_with(L"1P · ") && after.starts_with(L"2P · ") &&
          before.substr(2)==after.substr(2),"Swap kept the selected device but did not update its player label");
  Require(LoadSettings(ConfigPath(root),"futari").swap_players,"Reopening saved settings lost the controller order");
}

void MenuControllerChecks(const fs::path& sdl) {
  Fixture fixture;
  Write(ConfigPath(fixture.root),"[collection]\ncheck_for_updates=false\n");
  const auto target=fixture.root/L"runtime/ps2/SDL3.dll";
  fs::create_directories(target.parent_path());
  fs::copy_file(sdl,target);
  Frame frame(fixture.root,"futari");
  auto& app=frame.app;
  const auto tick=[&]{App::Window(app.hwnd,WM_TIMER,kMenuControllerTimer,0);};
  const auto module=LoadLibraryExW(target.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  const std::unique_ptr<std::remove_pointer_t<HMODULE>,decltype(&FreeLibrary)> library(module,FreeLibrary);
  Require(module && app.menu_controller && !app.BigPicture(),"SDL3 menu input did not start in desktop mode");
  const auto init=reinterpret_cast<decltype(&SDL_InitSubSystem)>(GetProcAddress(module,"SDL_InitSubSystem"));
  const auto quit=reinterpret_cast<decltype(&SDL_QuitSubSystem)>(GetProcAddress(module,"SDL_QuitSubSystem"));
  Require(init && quit && init(SDL_INIT_GAMEPAD),"Keep the virtual input subsystem alive for detach");
  const auto attach=reinterpret_cast<decltype(&SDL_AttachVirtualJoystick)>(GetProcAddress(module,"SDL_AttachVirtualJoystick"));
  const auto detach=reinterpret_cast<decltype(&SDL_DetachVirtualJoystick)>(GetProcAddress(module,"SDL_DetachVirtualJoystick"));
  const auto open=reinterpret_cast<decltype(&SDL_OpenJoystick)>(GetProcAddress(module,"SDL_OpenJoystick"));
  const auto close=reinterpret_cast<decltype(&SDL_CloseJoystick)>(GetProcAddress(module,"SDL_CloseJoystick"));
  const auto hat=reinterpret_cast<decltype(&SDL_SetJoystickVirtualHat)>(GetProcAddress(module,"SDL_SetJoystickVirtualHat"));
  const auto button=reinterpret_cast<decltype(&SDL_SetJoystickVirtualButton)>(GetProcAddress(module,"SDL_SetJoystickVirtualButton"));
  Require(attach && detach && open && close && hat && button,"SDL3 virtual controller functions are missing");
  SDL_VirtualJoystickDesc descriptor{};
  descriptor.version=sizeof(descriptor);
  descriptor.type=SDL_JOYSTICK_TYPE_ARCADE_STICK;
  descriptor.nbuttons=2;
  descriptor.nhats=1;
  descriptor.name="EVAC Test Arcade Stick";
  update_foreground=app.hwnd;
  const auto select=[&](const SDL_VirtualJoystickDesc& info) {
    const auto id=attach(&info);
    Require(id!=0,"Attach virtual controller");
    auto* joystick=open(id);
    Require(joystick!=nullptr,"Open virtual controller");
    std::this_thread::sleep_for(std::chrono::milliseconds(375));
    tick();
    return std::pair{id,joystick};
  };
  const auto [first,stick]=select(descriptor);
  SDL_VirtualJoystickDesc gamepad_descriptor{};
  gamepad_descriptor.version=sizeof(gamepad_descriptor);
  gamepad_descriptor.type=SDL_JOYSTICK_TYPE_GAMEPAD;
  gamepad_descriptor.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
  gamepad_descriptor.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
  gamepad_descriptor.name="EVAC Test Gamepad";
  const auto [gamepad_id,gamepad]=select(gamepad_descriptor);
  app.layout.width=720;
  app.layout.height=520;
  const auto initial=app.VisiblePosition().value();
  Require(hat(stick,0,SDL_HAT_DOWN),"Press arcade stick Down");
  tick();
  const auto second_row=initial+app.layout.columns();
  Require(app.VisiblePosition()==second_row && app.layout.scroll>0 && !app.work,
          "Desktop controller did not scroll to the next cover row using the library snapshot");
  Require(button(gamepad,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,true),"Press gamepad Right");
  tick();
  Require(app.VisiblePosition()==second_row+1,"Desktop controller did not select the adjacent cover");
  Require(hat(stick,0,SDL_HAT_CENTERED) && button(gamepad,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,false),
          "Release desktop directions");
  tick();
  Require(button(gamepad,SDL_GAMEPAD_BUTTON_SOUTH,true),"Press gamepad Confirm");
  tick();
  Require(!app.work && !app.child,"Desktop controller tried to launch a game with missing data");
  Require(button(gamepad,SDL_GAMEPAD_BUTTON_SOUTH,false),"Release gamepad Confirm");
  tick();

  app.EnsureVisible();
  const auto grid_position=app.VisiblePosition();
  update_foreground=app.nickname_control;
  Require(hat(stick,0,SDL_HAT_UP),"Hold arcade stick Up while launcher is inactive");
  tick();
  update_foreground=app.hwnd;
  tick();
  Require(app.VisiblePosition()==grid_position,"Held direction moved the grid when launcher regained focus");
  Require(hat(stick,0,SDL_HAT_CENTERED),"Release arcade stick Up");
  tick();
  Require(hat(stick,0,SDL_HAT_UP),"Press arcade stick Up in the grid");
  tick();
  Require(app.VisiblePosition()==grid_position.value()-app.layout.columns(),"The grid did not move by one row");

  app.child.emplace();
  app.active_game=Catalogue()[app.selected].id;
  const auto game=app.selected;
  tick();
  App::Window(app.hwnd,WM_TIMER,kChildTimer,0);
  tick();
  Require(!app.child && app.selected==game,"Returning from a game lost the selected title");
  Require(hat(stick,0,SDL_HAT_CENTERED),"Release arcade stick after the game");
  tick();

  app.SetBigPicture(true);
  tick();
  const size_t selection=app.selected;
  Require(hat(stick,0,SDL_HAT_RIGHT),"Set virtual stick direction");
  tick();
  Require(app.selected!=selection,"SDL arcade stick did not move the Big Picture focus");
  Require(button(gamepad,SDL_GAMEPAD_BUTTON_DPAD_LEFT,true),"Press the second device's D-pad");
  tick();
  Require(app.selected==selection,"Second controller did not move focus while the first was held");
  Require(button(gamepad,SDL_GAMEPAD_BUTTON_DPAD_LEFT,false),"Release the second device's D-pad");
  close(stick);
  Require(detach(first),"Detach virtual arcade stick");

  const auto [second,reconnected]=select(descriptor);
  Require(button(reconnected,1,true),"Set virtual Back button");
  tick();
  Require(app.menu_open,"Reconnected SDL controller could not open the exit menu");
  Require(button(reconnected,1,false),"Release virtual Back button");
  tick();
  Require(button(reconnected,0,true),"Set virtual Confirm button");
  tick();
  Require(!app.BigPicture() && app.menu_controller && app.selected==selection,
          "SDL controller did not return to desktop with the current game selected");

  const auto devices=cave::launcher::controls::InputDevices(fixture.root).List();
  const auto arcade=std::ranges::find(devices,second,&cave::launcher::controls::InputDevices::Device::id);
  Require(arcade!=devices.end(),"Read virtual arcade stick GUID");
  Write(ConfigPath(fixture.root),"[controller]\n"+arcade->guid+"=start:b0,dpup:h0.1,\n"
      "[game.futari.controller]\n"+arcade->guid+"=start:b1,dpup:h0.1,\n");
  app.ReloadPreferences();
  app.SetBigPicture(true);
  Require(button(reconnected,0,false),"Release arcade button before remapping");
  tick();
  App::Window(app.hwnd,WM_KEYDOWN,VK_ESCAPE,0);
  Require(app.menu_open,"Open Big Picture menu for the shared Start binding");
  Require(button(reconnected,1,true),"Press per-game Start");
  tick();
  Require(app.menu_open && app.BigPicture(),"Big Picture used the in-game override");
  Require(button(reconnected,1,false),"Release per-game Start");
  tick();
  Require(button(reconnected,0,true),"Press shared Start");
  tick();
  Require(!app.BigPicture(),"Big Picture ignored the shared controller Start binding");
  const auto shortcuts=ReadGameExitShortcuts(Ini::Load(ConfigPath(fixture.root)),fixture.root,"futari");
  Require(button(reconnected,0,false),"Release arcade stick Confirm");
  const auto poll=[&]{return app.menu_controller->PollGameExit(shortcuts);};
  Require(!poll(),"Idle controllers requested an exit");
  Require(hat(reconnected,0,SDL_HAT_UP) && button(gamepad,SDL_GAMEPAD_BUTTON_START,true),"Hold one half of the exit shortcut on each controller");
  Require(!poll(),"Two controllers combined into one exit shortcut");
  Require(button(gamepad,SDL_GAMEPAD_BUTTON_DPAD_UP,true),"Complete the exit shortcut on the gamepad");
  Require(poll() && !poll(),"Gamepad shortcut did not trigger immediately or repeated while held");
  Require(button(gamepad,SDL_GAMEPAD_BUTTON_START,false) && button(gamepad,SDL_GAMEPAD_BUTTON_DPAD_UP,false),"Release gamepad exit buttons");
  Require(button(reconnected,0,true),"Hold the globally mapped arcade Start button");
  Require(!poll(),"Arcade shortcut ignored the per-game mapping");
  Require(button(reconnected,0,false) && button(reconnected,1,true),"Hold the game's arcade Start button");
  Require(poll(),"Remapped arcade stick could not request an exit");

  Write(ConfigPath(fixture.root),"[game_exit_controller]\n"+arcade->guid+"=b0,b1\n");
  const auto recorded=ReadGameExitShortcuts(Ini::Load(ConfigPath(fixture.root)),fixture.root,"futari");
  app.menu_controller->ResetGameExit();
  Require(!app.menu_controller->PollGameExit(recorded),"Physical binding inherited an unrelated held direction");
  Require(button(reconnected,0,true) && app.menu_controller->PollGameExit(recorded),"The recorded controller chord did not take effect");

  Write(ConfigPath(fixture.root),"[game_exit_controller]\n"+arcade->guid+"=b1\n");
  const auto single=ReadGameExitShortcuts(Ini::Load(ConfigPath(fixture.root)),fixture.root,"futari");
  app.menu_controller->ResetGameExit();
  Require(!app.menu_controller->PollGameExit(single),"Held single controller exit fired before release");
  Require(button(reconnected,1,false) && !app.menu_controller->PollGameExit(single),"Release of the exit input closed the game");
  Require(button(reconnected,1,true) && app.menu_controller->PollGameExit(single) && !app.menu_controller->PollGameExit(single),
          "A single controller input did not exit exactly once");

  close(reconnected);
  Require(detach(second),"Detach reconnected controller");
  const auto [twin_id,twin]=select(gamepad_descriptor);
  CheckControllerSwapDialog(app.hwnd,fixture.root);
  close(twin);
  Require(detach(twin_id),"Detach second gamepad after saving player order");
  close(gamepad);
  Require(detach(gamepad_id),"Detach virtual gamepad");
  quit(SDL_INIT_GAMEPAD);
  update_foreground=nullptr;
}

int wmain(int count,wchar_t** arguments) {
  try {
    Check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Initialize test COM");
    struct Apartment {~Apartment(){CoUninitialize();}} apartment;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW definition{};definition.lpfnWndProc=App::Window;definition.hInstance=GetModuleHandleW(nullptr);definition.lpszClassName=kWindowClass;
    Require(RegisterClassW(&definition)!=0,"Register production window handler");
    if(count==6 && std::wstring_view(arguments[1])==L"--readback")ColdReadback(arguments[2],arguments[3],arguments[4],arguments[5]);
    else if(count==5 && std::wstring_view(arguments[1])==L"--instance-host")InstanceHost(arguments[2],arguments[3],arguments[4]);
    else if(count==4 && std::wstring_view(arguments[1])==L"--instance-probe")InstanceProbe(arguments[2],arguments[3]);
    else if(count==3 && std::wstring_view(arguments[1])==L"--visual-checks")VisualChecks(arguments[2]);
    else if(count==3 && std::wstring_view(arguments[1])==L"--drag-previews")DragVisualChecks(arguments[2]);
    else if(count==3 && std::wstring_view(arguments[1])==L"--cover-sizing-previews")VisualChecks(arguments[2],true);
    else if(count==2 && std::wstring_view(arguments[1])==L"--window-size-checks")WindowSizeChecks();
    else if(count==3 && std::wstring_view(arguments[1])==L"--big-picture-sdl-checks")MenuControllerChecks(arguments[2]);
    else if(count==2 && std::wstring_view(arguments[1])==L"--lingering-game")LingeringGameHost();
    else if(count==1)MainChecks();
    else throw std::runtime_error("Unexpected launcher test arguments");
    std::cout<<checks<<" launcher integration checks passed.\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
