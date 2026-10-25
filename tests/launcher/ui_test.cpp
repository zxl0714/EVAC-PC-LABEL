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
static bool update_dialog_failure{};
static HWND WINAPI FixtureGetForeground(){return update_foreground?update_foreground:GetForegroundWindow();}
static BOOL WINAPI FixtureVisible(HWND window){return window==update_foreground?TRUE:IsWindowVisible(window);}
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
#include "../../launcher/main.cpp"
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
  void Settle() {for(unsigned n=0;n<8 && (app.worker.joinable() || app.work);++n) {if(app.worker.joinable())app.worker.join();if(app.work)app.Complete();}}
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
  update_foreground=nullptr;
}
void SelectionChecks() {
  Fixture fixture;
  const auto ini="\xef\xbb\xbf; keep selected-game fixture\r\n[collection]\r\nconfig_version=1\r\nlanguage=en\r\nselected_game=daioujou\r\n[community]\r\nnickname=Baseline\r\n[future]\r\nvalue=untouched\r\n";
  Write(fixture.root/"user-data/config.ini",ini);
  {
    Frame frame(fixture.root);auto& app=frame.app;
    Require(Catalogue()[app.selected].id=="daioujou","Cold GUI did not restore selected ID");
    Require(ControlText(app.hwnd)==L"EVAC PC LABEL","Production launcher caption differs from product name");
    const auto scale=float(GetDpiForWindow(app.hwnd))/96;
    App::Window(app.hwnd,WM_SIZE,SIZE_RESTORED,MAKELPARAM(int(720*scale),int(520*scale)));
    const auto selected=app.layout.card(app.selected);
    Require(selected.top>=app.layout.header()-1 && selected.bottom<=app.layout.footer()+1,"Restored selection was not scrolled visible");
    Require(app.rows[app.selected].inspection.state=="missing_data","Missing data erased a valid saved selection");
    SetWindowTextW(app.nickname_control,L"Unsaved draft");
    App::Window(app.hwnd,WM_KEYDOWN,VK_UP,0);frame.Settle();
    Require(Catalogue()[app.selected].id=="sdoj","Up no longer selects the preceding game");
    App::Window(app.hwnd,WM_KEYDOWN,VK_DOWN,0);frame.Settle();
    Require(Catalogue()[app.selected].id=="daioujou","Down no longer selects the next game");
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
  SendMessageW(app.fullscreen_control,BM_SETCHECK,BST_CHECKED,0);app.preferences_dirty=true;
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
      App::Window(app.hwnd,WM_MOUSEMOVE,0,MAKELPARAM(int((card.left+20)*scale),int((card.top+20)*scale)));
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
#include "instance_checks.h"
#include "visual_checks.h"
void ColdReadback(const fs::path& root,std::wstring_view expected_language,std::wstring_view nickname,std::wstring_view game) {
  const auto before=Ini::ReadText(ConfigPath(root));
  Frame frame(root);
  Require(cave::launcher::LanguageCode(language)==Utf8(expected_language),"Cold language readback");
  Require(ControlText(frame.app.nickname_control)==nickname,"Cold nickname readback");
  Require(Catalogue()[frame.app.selected].id==Utf8(game),"Independent cold process did not restore the selected game");
  Require(SendMessageW(frame.app.fullscreen_control,BM_GETCHECK,0,0)==BST_UNCHECKED,"Cold fullscreen readback");
  Require(ControlText(frame.app.save_control)==T(L"保存"),"Cold localized controls");
  Require(Ini::ReadText(ConfigPath(root))==before,"Readback changed configuration");
  Require(ResolveSettings(Ini::Load(ConfigPath(root)),"sdoj").sdoj_frame_delay==1,
          "Cold startup did not resolve SDOJ's default 1V");
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
void MainChecks() {
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
  const std::string before="\xef\xbb\xbf; preserve comment\r\n[collection]\r\nconfig_version=1\r\nlanguage=zh\r\n"
      "[display]\r\nfullscreen=true\r\n[community]\r\nnickname=Baseline\r\n"
      "[game.futari.display]\r\nfullscreen=true\r\n[future]\r\nkey=preserved\r\n";
  Write(fixture.root/"config.ini",before);
  {
    Frame frame(fixture.root);auto& app=frame.app;
    Require(!fs::exists(fixture.root/"config.ini") && Ini::ReadText(ConfigPath(fixture.root))==before,"Real app startup did not preserve migration bytes");
    for(const auto dimensions:std::array{std::pair{720.f,520.f},std::pair{1080.f,900.f}}) {
      app.layout.width=dimensions.first;app.layout.height=dimensions.second;
      const auto brand=app.layout.brand();
      Require(app.layout.nickname().bottom==brand.bottom && app.layout.save().bottom==brand.bottom,
              "Nickname controls must bottom-align with the logo");
      Require(app.layout.header()-brand.bottom==14,"Header retains a removed helper-row reservation");
      Require(brand.right<app.layout.language_choice(0).left && brand.bottom<app.layout.fullscreen().top &&
              brand.left>=0 && brand.top>=0,"Logo overlaps the launcher controls");
      const std::array controls{app.layout.data(),app.layout.refresh(),app.layout.config(),app.layout.logs(),app.layout.detail(),app.layout.play()};
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
    SendMessageW(app.fullscreen_control,BM_SETCHECK,BST_UNCHECKED,0);
    nickname_responses.push_back(IDYES);
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
  SelectionChecks();
  NicknameWarningChecks();
  NicknameAlignmentChecks();
  PreferenceRepaintChecks();
  InstanceChecks();
  UpdateChecks();
}
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
    else if(count==1)MainChecks();
    else throw std::runtime_error("Unexpected launcher test arguments");
    std::cout<<checks<<" launcher integration checks passed; hidden fixture windows only.\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
