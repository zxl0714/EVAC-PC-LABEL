#include "common/catalogue.h"
#include "common/verification_cache.h"
#include "common/ini.h"
#include "common/launch.h"
#include "common/data_import.h"
#include "common/daioujou_music.h"
#include "launcher/arrange_status.h"
#include "common/settings.h"
#include "artwork.h"
#include "launcher/window_size.h"
#include "launcher/strings.h"
#include "launcher/preferences.h"
#include "launcher/game_library.h"
#include "launcher/data_state.h"
#include "launcher/nickname.h"
#include "launcher/single_instance.h"
#include "launcher/update_check.h"
#include "launcher/program_update.h"
#include "launcher/log_upload.h"
#include "launcher/input_settings.h"
#include "launcher/big_picture.h"
#include "launcher/sdoj_recomp.h"
#include "common/config_update.h"
#include "common/controller_bindings.h"
#include <Windows.h>
#include <hidusage.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <commctrl.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <memory>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <string>
#include <stdexcept>
#include <thread>
#include <utility>

using Microsoft::WRL::ComPtr;
using namespace cave;
using cave::launcher::LauncherCatalogue;
using cave::launcher::FindLauncherGame;
using cave::launcher::BigPictureLayout;
using cave::launcher::GameExitShortcuts;
using cave::launcher::ExitKeys;
using cave::launcher::MenuKey;
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
namespace {
constexpr UINT kWorkDone=WM_APP+1;
constexpr UINT kCopyProgress=WM_APP+2;
constexpr UINT kUpdateScrollBar=WM_APP+3;
constexpr UINT kShowFirstDisplayChoice=WM_APP+4;
constexpr UINT_PTR kChildTimer=1;
constexpr UINT_PTR kUpdateTimer=2;
constexpr UINT_PTR kMenuControllerTimer=3;
constexpr UINT kMenuPollInterval=16;
constexpr int kFullscreenControl=101,kNicknameControl=102,kSaveControl=103;
constexpr int kProjectLinkFocus=16;
enum class ExternalControl { None, SdojToggle, ProjectIcon, ProjectLink };
struct ExternalHit {
  ExternalControl control{};
  std::wstring_view url{};
  bool operator==(const ExternalHit&) const = default;
};
constexpr wchar_t kWindowClass[]=L"EVACPCLabel.Launcher.v1";
constexpr wchar_t kWindowTitle[]=L"EVAC PC LABEL";
HICON BrandIcon(HINSTANCE instance,int size) {
  const auto icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(cave::launcher::kAppIconResource),
      IMAGE_ICON,size,size,LR_SHARED));
  if(!icon)throw std::runtime_error("Cannot load the embedded launcher icon");
  return icon;
}
using cave::launcher::Language;
namespace update=cave::launcher::update;
Language language=cave::launcher::kDefaultLanguage;
const wchar_t* T(std::wstring_view key) {return cave::launcher::Translate(language,key);}
std::wstring DisplayLabel(DisplaySelection display) {
  return display.fullscreen?T(L"全屏"):std::to_wstring(display.width)+L" × "+std::to_wstring(display.height);
}
void Check(HRESULT status,const char* operation) {
  if (FAILED(status)) throw std::runtime_error(std::string(operation)+" (HRESULT "+std::to_string(static_cast<unsigned>(status))+")");
}
std::wstring Wide(std::string_view text) {return FromUtf8(text).native();}
std::wstring_view RecompProject(const Game& game,bool sdoj_external) {
  if(game.id==cave::launcher::kDonPachi.id)return cave::launcher::kDonPachiProject;
  if(game.id=="sdoj" && sdoj_external)return cave::launcher::kSdojRecompProject;
  return {};
}
const std::wstring& LauncherCaption() {
  static const auto caption=std::wstring(kWindowTitle)+L" v"+Wide(cave::launcher::kProductVersion);
  return caption;
}
void Error(HWND window,std::string_view message) {
  std::wstring value;
  try {value=Wide(message.size()<16000?message:std::string_view("Error message is too long; check the log."));}
  catch (...) {value=T(L"无法显示详细信息，请检查日志。");}
  value=std::wstring(T(L"操作失败。详细信息：\n\n"))+value;
  MessageBoxW(window,value.c_str(),kWindowTitle,MB_OK|MB_ICONERROR);
}
std::wstring WideCodepoint(char32_t cp) {
  if(cp<0x10000)return std::wstring(1,wchar_t(cp));
  cp-=0x10000;return {wchar_t(0xd800+(cp>>10)),wchar_t(0xdc00+(cp&0x3ff))};
}
bool ConfirmNickname(HWND window,const std::vector<cave::launcher::NicknameIssue>& issues) {
  if(issues.empty())return true;
  std::wstring body=T(L"部分游戏无法原样显示此昵称。完整昵称仍会保留。"),details;
  std::vector<bool> printed(issues.size());
  for(size_t index=0;index<issues.size();++index) {
    if(printed[index])continue;
    const auto& item=issues[index];
    for(size_t other=index;other<issues.size();++other) {
      const auto& match=issues[other];
      if(match.native_name!=item.native_name || match.missing!=item.missing || match.alias!=item.alias || match.shortened!=item.shortened)continue;
      if(other!=index)details+=L" / ";
      details+=Wide(match.game->DisplayName());
      if(!match.version.empty())details+=L" ("+Wide(match.version)+L")";
      printed[other]=true;
    }
    details+=L"\n";
    if(!item.missing.empty()) {
      details+=T(L"不支持的字符：");
      for(const auto cp:item.missing) {
        wchar_t code[16]{};swprintf_s(code,L"U+%04X",unsigned(cp));
        details+=L"「"+WideCodepoint(cp)+L"」("+code+L") ";
      }
      details+=L"\n";
    }
    details+=item.alias?T(L"游戏内显示名："):T(L"传入游戏的显示名：");
    details+=Wide(item.native_name)+L"\n";
    if(!item.alias && !item.missing.empty())details+=std::wstring(T(L"其中缺少字形的字符无法正常显示。"))+L"\n";
    if(item.shortened)details+=std::wstring(T(L"超出游戏昵称长度的部分会截短。"))+L"\n";
    details+=L"\n";
  }
  const TASKDIALOG_BUTTON buttons[]{{IDCANCEL,T(L"返回修改")},{IDYES,T(L"仍然保存")}};
  TASKDIALOGCONFIG dialog{};dialog.cbSize=sizeof(dialog);
  dialog.hwndParent=window;dialog.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;
  dialog.pszWindowTitle=kWindowTitle;dialog.pszMainIcon=TD_WARNING_ICON;
  dialog.pszMainInstruction=T(L"确认排行榜昵称");dialog.pszContent=body.c_str();
  dialog.pszExpandedInformation=details.c_str();dialog.pszExpandedControlText=T(L"隐藏详情");
  dialog.pszCollapsedControlText=T(L"查看游戏内显示详情");
  dialog.dwFlags|=TDF_EXPANDED_BY_DEFAULT;
  dialog.cButtons=UINT(std::size(buttons));dialog.pButtons=buttons;dialog.nDefaultButton=IDCANCEL;
  int result=IDCANCEL;
  Check(TaskDialogIndirect(&dialog,&result,nullptr,nullptr),"Show nickname warning");
  return result==IDYES;
}
fs::path ExecutableDirectory() {
  std::wstring value(32768,L'\0');
  const auto length=GetModuleFileNameW(nullptr,value.data(),DWORD(value.size()));
  if (!length || length>=value.size()) throw std::runtime_error("Cannot locate launcher executable");
  value.resize(length);return fs::path(value).parent_path();
}
fs::path DefaultRoot() {
  return fs::canonical(ExecutableDirectory());
}
void OpenPath(HWND window,const fs::path& path) {
  if (reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
    throw std::runtime_error("Cannot open the selected file or directory");
}
bool IsGameWindow(HWND window,DWORD pid) {
  DWORD owner{};
  return window && pid && GetWindowThreadProcessId(window,&owner) && owner==pid &&
      IsWindowVisible(window) && !IsIconic(window) && !GetWindow(window,GW_OWNER) &&
      !(GetWindowLongPtrW(window,GWL_EXSTYLE)&(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE));
}
HWND VisibleGameWindow(DWORD pid) {
  struct Search { DWORD pid; HWND result; } search{pid,nullptr};
  EnumWindows([](HWND window,LPARAM parameter)->BOOL {
    auto& search=*reinterpret_cast<Search*>(parameter);
    if(!IsGameWindow(window,search.pid))return TRUE;
    search.result=window;
    return FALSE;
  },reinterpret_cast<LPARAM>(&search));
  return search.result;
}
fs::path LogDirectory(const fs::path& root) {
  const auto directory=DataChild(root,"logs");
  const auto ini=Ini::Load(ConfigPath(root));
  for (const auto& game:LauncherCatalogue()) {
    const auto data=Resolve(root,ini.DataDirectory(game));
    if (Within(directory,data) || Within(data,directory))
      throw std::runtime_error("Launcher logs overlap a read-only game-data directory");
  }
  return directory;
}
void Log(const fs::path& root,std::string_view message) {
  try {
    const auto directory=DataChild(LogDirectory(root),"launcher");
    fs::create_directories(directory);
    std::ofstream stream(directory/("run-"+std::to_string(GetCurrentProcessId())+".log"),std::ios::app);
    SYSTEMTIME now{};GetSystemTime(&now);
    stream<<now.wYear<<'-'<<now.wMonth<<'-'<<now.wDay<<'T'<<now.wHour<<':'<<now.wMinute<<':'<<now.wSecond
          <<"Z "<<message<<'\n';
  } catch (...) {}
}
GameExitShortcuts ReadGameExitShortcuts(const Ini& ini,const fs::path& root,std::string_view game) {
  GameExitShortcuts shortcuts;
  if(const auto entry=cave::launcher::ExitKeySetting(ini)) {
    const auto keys=cave::launcher::ParseExitKeys(entry->text);
    shortcuts.keyboard=keys.value_or(ExitKeys{});
    if(!keys)Log(root,"Invalid [collection].game_exit_key; keyboard exit disabled");
  }
  if(const auto entry=ini.Get("collection",cave::launcher::kExitButtonsSetting)) {
    auto buttons=cave::launcher::ParseExitButtons(entry->text);
    if(buttons)shortcuts.buttons=std::move(*buttons);
    else {
      shortcuts.buttons.clear();
      Log(root,"Invalid [collection].big_picture_exit_buttons; default controller exit disabled");
    }
  }
  if(!shortcuts.buttons.empty()) {
    try {
      for(const auto& [guid,source]:ControllerMappings(ini,game))
        shortcuts.controller_mappings.emplace(guid,cave::launcher::controls::ParsePadMap(source));
    }catch(const std::exception& error) {Log(root,error.what());}
  }
  for(const auto& [address,entry]:ini.Values()) {
    if(address.first!=cave::launcher::kExitControllerSection)continue;
    const auto binding=cave::launcher::ParseExitPadBinding(entry.text);
    if(ControllerGuid(address.second) && binding)shortcuts.controller_bindings.emplace(address.second,*binding);
    else Log(root,"Invalid game exit controller binding at config.ini line "+std::to_string(entry.line));
  }
  return shortcuts;
}
bool KeyHeld(std::uint16_t key) {
  return key && (GetAsyncKeyState(key)&0x8000);
}
struct Options {
  fs::path root,data,preview;
  std::string game,profile="default",selection;
  std::optional<Language> language;
  std::wstring update_action;
  unsigned long update_parent{};
  bool big_picture{};
};
Options Arguments() {
  Options options;options.root=DefaultRoot();
  int count{};auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);
  if (!arguments) throw std::runtime_error("Cannot parse launcher arguments");
  try {
    for (int index=1;index<count;++index) {
      const std::wstring_view key=arguments[index];
      if (key==L"--big-picture") {options.big_picture=true;continue;}
      if (++index>=count) throw std::runtime_error("Expected a value after launcher option");
      const auto value=arguments[index];
      if (key==L"--root") options.root=value;
      else if (key==L"--game") options.game=Utf8(value);
      else if (key==L"--select") options.selection=Utf8(value);
      else if (key==L"--profile") options.profile=Utf8(value);
      else if (key==L"--data-dir") options.data=value;
      else if (key==L"--render-preview") options.preview=value;
      else if (key==L"--language") options.language=cave::launcher::ParseLanguage(Utf8(value));
      else if (key==L"--apply-update") options.update_action=value;
      else if (key==L"--update-parent") {
        const std::wstring_view pid=value;
        if(pid.empty() || pid.find_first_not_of(L"0123456789")!=pid.npos)
          throw std::runtime_error("Invalid update parent process");
        options.update_parent=std::stoul(value);
      }
      else throw std::runtime_error("Unknown launcher option");
    }
  } catch (...) {LocalFree(arguments);throw;}
  LocalFree(arguments);
  options.root=fs::canonical(options.root);
  if(options.update_action.empty()!=(!options.update_parent) ||
     (!options.update_action.empty() && (!options.game.empty() || !options.preview.empty())))
    throw std::runtime_error("Invalid update worker options");
  if (options.big_picture && (!options.game.empty() || !options.update_action.empty()))
    throw std::runtime_error("--big-picture opens the graphical launcher");
  if (!options.data.empty() && options.game.empty()) throw std::runtime_error("--data-dir requires --game");
  if (!options.game.empty()) (void)FindLauncherGame(options.game);
  if (!options.selection.empty()) (void)FindLauncherGame(options.selection);
  if (!options.game.empty() && !options.selection.empty())throw std::runtime_error("Use --game to launch directly, or --select to open the launcher");
  return options;
}
struct Row {Inspection inspection;daioujou::ArrangeStatus arrange;};
std::vector<Row> InspectRows(const fs::path& root,size_t selected,bool prepare_title_updates) {
  const auto started=std::chrono::steady_clock::now();
  VerificationCache cache(root);
  std::vector<Row> rows(LauncherCatalogue().size());
  for(size_t index=0;index<rows.size();++index) {
    rows[index].inspection.game=LauncherCatalogue()[index].id;
    rows[index].inspection.state=LauncherCatalogue()[index].coming_soon?"coming_soon":"checking";
  }
  try {
    const auto ini=Ini::Load(ConfigPath(root));
    // Inspect once on the existing worker, selected title first. Every card
    // needs a real base-data result; repaint/scroll never touch the filesystem.
    for(size_t step=0;step<rows.size();++step) {
      const auto index=step==0?selected:(step<=selected?step-1:step);
      const auto& game=LauncherCatalogue()[index];
      try {
        if (game.integrated) (void)ResolveSettings(ini,game.id);
        const auto data=ini.DataDirectory(game);
        rows[index].inspection=game.id==cave::launcher::kDonPachi.id
            ? cave::launcher::InspectDonPachi(root,data)
            : (prepare_title_updates ? InspectLaunchData(game,root,data,CheckLevel::Quick,cache)
                                     : Inspect(game,root,data,CheckLevel::Quick,&cache));
        if(!rows[index].inspection.missing_runtime.empty()) {
          try {cave::launcher::reports::RecordRuntimeFailure(root,game.id);} catch (...) {}
        }
        if(game.id=="daioujou")rows[index].arrange=daioujou::InspectArrangeMusic(rows[index].inspection.data_dir,CheckLevel::Quick);
      } catch (const std::exception& error) {
        rows[index].inspection.game=game.id;rows[index].inspection.state="invalid_config";rows[index].inspection.reason=error.what();
      }
    }
  } catch (const std::exception& error) {
    for (size_t index=0;index<rows.size();++index) {
      auto& result=rows[index].inspection;result.game=LauncherCatalogue()[index].id;
      result.state=LauncherCatalogue()[index].coming_soon?"coming_soon":"invalid_config";result.reason=error.what();
    }
  }
  Log(root,"data-list-inspection ms="+std::to_string(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count())+
      " cache_hits="+std::to_string(cache.hits)+" bytes="+std::to_string(cache.bytes_read));
  return rows;
}
std::wstring Status(const Inspection& inspection) {
  const auto& state=inspection.state;
  if (state=="ready") return T(L"已就绪");
  if (state=="needs_verification") return T(L"数据已找到 · 启动时校验");
  if (state=="missing_data") return T(L"未找到游戏数据");
  if (state=="incomplete_data") return T(L"游戏数据不完整");
  if (state=="nested_data") return T(L"解压目录层级不正确");
  if (state=="raw_disc_image") return T(L"请选择已解密的 PS4 游戏目录");
  if (state=="unknown_layout") return T(L"请检查游戏数据目录");
  if (state=="unsupported_version") return T(L"数据版本不支持");
  if (state=="title_update_failed") return T(L"无法解包游戏更新 · 点击详情");
  if (state=="missing_runtime") return T(L"运行组件不可用");
  if (state=="donpachi_program_missing") return T(L"请安装 DonPachi Recomp");
  if (state=="donpachi_rom_missing") return T(L"请放置 donpachi.zip（美版）");
  if (state=="invalid_config") return T(L"配置有误 · 请检查 config.ini");
  if (state=="invalid_path") return T(L"数据路径不可用");
  if (state=="not_integrated") return T(L"正在接入");
  if (state=="coming_soon") return T(L"敬请期待");
  return T(L"正在检测…");
}
bool CanPlay(const Inspection& inspection) {return inspection.state=="ready" || inspection.state=="needs_verification";}
bool DonPachiNeedsSetup(const Inspection& inspection) {
  return inspection.state=="donpachi_program_missing" || inspection.state=="donpachi_rom_missing";
}
std::string Details(const Inspection& inspection) {
  auto result=inspection.reason.empty()?inspection.state:inspection.reason;
  if(!inspection.version.empty())result+="\n"+inspection.game+" "+inspection.version+" ("+inspection.layout+")";
  if (!inspection.problems.empty()) result+="\n"+inspection.problems.front();
  if (!inspection.missing_runtime.empty()) result+="\n"+inspection.missing_runtime.front();
  return result;
}
struct Layout {
  float width{},height{},scroll{};
  size_t game_count=LauncherCatalogue().size();
  bool narrow() const {return width<800;}
  float margin() const {return cave::launcher::CoverMargin(width);}
  float header() const {return cave::launcher::kCoverHeaderHeight;}
  float footer() const {return height-cave::launcher::kFooterHeight;}
  float row_height() const {return cave::launcher::CoverHeight(cover_width());}
  float viewport_height() const {return std::max(0.f,footer()-header());}
  size_t columns() const {return cave::launcher::CoverColumns(width);}
  float cover_width() const {return cave::launcher::CoverWidth(width);}
  float row_gap() const {return cave::launcher::kCoverRowGap;}
  float content_height() const {
    return game_count?float((game_count+columns()-1)/columns())*(row_height()+row_gap())-row_gap():0.f;
  }
  float max_scroll() const {return std::max(0.f,content_height()-viewport_height());}
  D2D1_RECT_F card(size_t index) const {
    const auto top=header()+float(index/columns())*(row_height()+row_gap())-scroll;
    const auto left=margin()+float(index%columns())*(cover_width()+cave::launcher::kCoverColumnGap);
    return {left,top,left+cover_width(),top+row_height()};
  }
  D2D1_RECT_F play() const {
    const auto right=width-margin();
    return {right-(narrow()?150.f:176.f),height-64,right,height-14};
  }
  D2D1_RECT_F utility_button(size_t index) const {
    constexpr std::array widths{85.f,75.f,70.f,77.f,80.f,98.f};
    constexpr float total=std::accumulate(widths.begin(),widths.end(),0.f);
    const float gap=narrow()?5.f:7.f;
    const auto available=play().left-margin()-gap*widths.size();
    const auto scale=std::clamp(available/total,0.f,1.f);
    const auto left=margin()+gap*index+scale*std::accumulate(widths.begin(),widths.begin()+index,0.f);
    return {left,height-45,left+scale*widths[index],height-14};
  }
  D2D1_RECT_F data() const {return utility_button(0);}
  D2D1_RECT_F file_upload() const {return utility_button(1);}
  D2D1_RECT_F controls() const {return utility_button(2);}
  D2D1_RECT_F config() const {return utility_button(3);}
  D2D1_RECT_F logs() const {return utility_button(4);}
  D2D1_RECT_F big_picture_button() const {return utility_button(5);}
  D2D1_RECT_F brand() const {return {margin(),16,margin()+216,96};}
  D2D1_RECT_F language_choice(size_t index) const {
    const auto left=width-margin()-112+float(index)*40;
    return {left,16,left+32,44};
  }
  D2D1_RECT_F hidden_games() const {
    const auto left=language_choice(0).left-36;
    return {left,16,left+28,44};
  }
  D2D1_RECT_F update_badge() const {
    const auto right=hidden_games().left-6;
    return {right-24,16,right,44};
  }
  D2D1_RECT_F project_link(std::wstring_view url) const {
    const bool donpachi=url==cave::launcher::kDonPachiProject;
    const float status_width=donpachi?168.f:124.f;
    const float link_width=donpachi?260.f:202.f;
    const auto right=play().left-18;
    const auto left=margin()+(right-margin()>=status_width+link_width?status_width:0);
    return {left,height-80,std::min(left+link_width,right),height-64};
  }
  D2D1_RECT_F focus_button(int slot) const {
    switch(slot) {
    case 4:return config(); case 5:return logs(); case 6:return data();
    case 7:return file_upload(); case 8:return play(); case 13:return controls(); case 14:return hidden_games(); case 15:return update_badge();
    default:return big_picture_button();
    }
  }
  int footer_button_at(float x,float y) const;
  D2D1_RECT_F fullscreen() const {const auto r=play();return {r.left,r.top-33,r.right,r.top-5};}
  D2D1_RECT_F nickname() const {const auto right=width-margin();return {right-263,66,right-73,96};}
  D2D1_RECT_F nickname_label() const {const auto r=nickname();return {r.left-80,r.top+7,r.left-6,r.bottom};}
  D2D1_RECT_F save() const {const auto right=width-margin();return {right-62,66,right,96};}
};
bool Inside(D2D1_RECT_F r,float x,float y) {return x>=r.left && x<r.right && y>=r.top && y<r.bottom;}
D2D1_RECT_F LayoutSdojSwitch(D2D1_RECT_F card) {
  const auto right=card.right-8.f;
  const auto top=card.bottom-25.f;
  // Keep room for the complete English title on the narrowest cover card.
  const auto width=std::clamp((card.right-card.left)-138.f,46.f,52.f);
  return {right-width,top,right,top+16};
}
D2D1_RECT_F LayoutProjectIcon(D2D1_RECT_F card) {
  const auto left=card.right-31.f;
  const auto top=card.bottom-66.f;
  return {left,top,left+22,top+22};
}
D2D1_RECT_F ArtworkSource(ID2D1Bitmap* image,const cave::launcher::Artwork& artwork) {
  const auto size=image->GetSize();
  const auto& trim=artwork.trim;
  return {size.width*trim[0],size.height*trim[1],size.width*(1-trim[2]),size.height*(1-trim[3])};
}
int Layout::footer_button_at(float x,float y) const {
  for(int slot:{6,7,13,4,5,9,8})if(Inside(focus_button(slot),x,y))return slot;
  return -1;
}
struct CardDrag {
  size_t game{};
  D2D1_RECT_F rectangle{};
  std::optional<D2D1_RECT_F> insertion;
};
RECT FitToWorkArea(RECT bounds) {
  MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
  if(!GetMonitorInfoW(MonitorFromRect(&bounds,MONITOR_DEFAULTTONEAREST),&monitor))return bounds;
  const auto& work=monitor.rcWork;
  const auto width=std::min(bounds.right-bounds.left,work.right-work.left);
  const auto height=std::min(bounds.bottom-bounds.top,work.bottom-work.top);
  const auto left=std::clamp(bounds.left,work.left,work.right-width);
  const auto top=std::clamp(bounds.top,work.top,work.bottom-height);
  return {left,top,left+width,top+height};
}
void FitInitialWindow(HWND window,std::optional<cave::launcher::WindowSize> saved={},
                      std::size_t games=LauncherCatalogue().size()) {
  MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
  if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))
    throw std::runtime_error("Cannot read launcher monitor work area");
  const auto dpi=GetDpiForWindow(window);
  RECT frame{};
  if(!AdjustWindowRectExForDpi(&frame,WS_OVERLAPPEDWINDOW|WS_VSCROLL,FALSE,0,dpi))
    throw std::runtime_error("Cannot size launcher window frame");
  // AdjustWindowRectExForDpi excludes scrollbars; include ours in the initial grid width.
  const auto scrollbar=GetSystemMetricsForDpi(SM_CXVSCROLL,dpi);
  frame.right+=scrollbar;
  const auto& work=monitor.rcWork;
  const auto scale=float(dpi)/96;
  const auto automatic=[&](std::size_t count) {
    auto size=cave::launcher::InitialCoverWindowSize(work.right-work.left,work.bottom-work.top,dpi,
        frame.right-frame.left,frame.bottom-frame.top,count);
    const Layout planned{(size.width-frame.right+frame.left)/scale,(size.height-frame.bottom+frame.top)/scale,0.f,count};
    // A fitting library hides the native scrollbar. Keep its resulting client width
    // equal to the width used to calculate the card heights.
    if(planned.max_scroll()==0)size.width-=scrollbar;
    return size;
  };
  auto size=automatic(games);
  if(saved) {
    bool old_default=false;
    // Launcher versions before the arcade entry persisted their automatic grid bounds in DIPs.
    if(games>Catalogue().size()) {
      const auto previous=automatic(Catalogue().size());
      old_default=std::abs(std::lround(previous.width/scale)-saved->width)<=2 &&
                  std::abs(std::lround(previous.height/scale)-saved->height)<=2;
    }
    if(!old_default)
      size=cave::launcher::RestoreWindowSize(*saved,work.right-work.left,work.bottom-work.top,dpi);
  }
  SetWindowPos(window,nullptr,work.left+(work.right-work.left-size.width)/2,
      work.top+(work.bottom-work.top-size.height)/2,size.width,size.height,SWP_NOZORDER|SWP_NOACTIVATE);
}
class Renderer {
 public:
  Renderer() {
    Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()),"Create Direct2D factory");
    Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write.GetAddressOf())),"Create text factory");
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"Create image factory");
  }
  void Text(ID2D1RenderTarget* target,std::wstring_view value,D2D1_RECT_F rectangle,float size,UINT32 color,
            DWRITE_TEXT_ALIGNMENT align=DWRITE_TEXT_ALIGNMENT_LEADING,bool wrap=false,bool underline=false) {
    auto found=formats.find(size);
    if(found==formats.end()) {
      ComPtr<IDWriteTextFormat> format;
      Check(write->CreateTextFormat(L"Segoe UI",nullptr,size>=20?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",&format),"Create launcher font");
      format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
      found=formats.emplace(size,std::move(format)).first;
    }
    auto format=found->second;format->SetTextAlignment(align);
    format->SetWordWrapping(wrap?DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP);
    ComPtr<ID2D1SolidColorBrush> brush;Check(target->CreateSolidColorBrush(D2D1::ColorF(color),&brush),"Create text brush");
    if(underline) {
      ComPtr<IDWriteTextLayout> text;
      Check(write->CreateTextLayout(value.data(),UINT32(value.size()),format.Get(),
          rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,&text),"Lay out project link");
      Check(text->SetUnderline(TRUE,{0,UINT32(value.size())}),"Underline project link");
      target->DrawTextLayout({rectangle.left,rectangle.top},text.Get(),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
      return;
    }
    target->DrawText(value.data(),UINT32(value.size()),format.Get(),rectangle,brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
  }
  void Button(ID2D1RenderTarget* target,D2D1_RECT_F rectangle,std::wstring_view text,bool play=false,bool disabled=false,bool hovered=false) {
    play=play&&!disabled;
    hovered=hovered&&!disabled;
    auto background=disabled?0xe7ebe5:0xffffff;
    auto outline=0xd9dfd6;
    if(play)background=outline=hovered?0x3c7152:0x2f5b42;
    else if(hovered) {background=0xeaf2eb;outline=0x82a18a;}
    ComPtr<ID2D1SolidColorBrush> fill,border;
    Check(target->CreateSolidColorBrush(D2D1::ColorF(background),&fill),"Create button fill");
    Check(target->CreateSolidColorBrush(D2D1::ColorF(outline),&border),"Create button border");
    const auto rounded=D2D1::RoundedRect(rectangle,8,8);target->FillRoundedRectangle(rounded,fill.Get());target->DrawRoundedRectangle(rounded,border.Get());
    Text(target,text,{rectangle.left+6,rectangle.top+(rectangle.bottom-rectangle.top-16)/2,rectangle.right-6,rectangle.bottom},12,play?0xffffff:(disabled?0x78857b:0x495a50),DWRITE_TEXT_ALIGNMENT_CENTER);
  }
  void SdojSwitch(ID2D1RenderTarget* target,D2D1_RECT_F bounds,bool enabled,bool hovered) {
    const auto fill=enabled?(hovered?0x2c513e:0x37644c):(hovered?0xd7ded9:0xe5e9e6);
    const float height=bounds.bottom-bounds.top;
    const float radius=height/2;
    ComPtr<ID2D1SolidColorBrush> brush;
    Check(target->CreateSolidColorBrush(D2D1::ColorF(fill),&brush),"Create eandis switch brush");
    target->FillRoundedRectangle(D2D1::RoundedRect(bounds,radius,radius),brush.Get());

    const float center_y=bounds.top+radius;
    const float thumb_x=enabled?bounds.right-radius:bounds.left+radius;
    const float thumb_radius=radius-2.5f;
    brush->SetColor(D2D1::ColorF(enabled?0xffffff:0x69766d));
    target->FillEllipse(D2D1::Ellipse({thumb_x,center_y},thumb_radius,thumb_radius),brush.Get());

    const auto font=bounds.right-bounds.left>=50.f?10.f:9.f;
    const float label_left=bounds.left+(enabled?4.f:height);
    const float label_right=bounds.right-(enabled?height:4.f);
    Text(target,L"eandis",{label_left,center_y-font*.61f,label_right,bounds.bottom},
        font,enabled?0xffffff:0x47564c,DWRITE_TEXT_ALIGNMENT_CENTER);
  }
  void ExternalLinkIcon(ID2D1RenderTarget* target,D2D1_RECT_F bounds,bool hovered) {
    ComPtr<ID2D1SolidColorBrush> brush;
    Check(target->CreateSolidColorBrush(D2D1::ColorF(hovered?0xeaf2eb:0xf3f6f2),&brush),"Create project icon brush");
    if(hovered)target->FillRoundedRectangle(D2D1::RoundedRect(bounds,5,5),brush.Get());
    brush->SetColor(D2D1::ColorF(hovered?0x2f5b42:0x687b6d));
    const auto x=(bounds.left+bounds.right)/2,y=(bounds.top+bounds.bottom)/2;
    target->DrawLine({x-5,y-4},{x-5,y+5},brush.Get(),1.3f);
    target->DrawLine({x-5,y+5},{x+4,y+5},brush.Get(),1.3f);
    target->DrawLine({x+4,y+5},{x+4,y},brush.Get(),1.3f);
    target->DrawLine({x-5,y-4},{x,y-4},brush.Get(),1.3f);
    target->DrawLine({x-1,y+1},{x+6,y-6},brush.Get(),1.3f);
    target->DrawLine({x+1,y-6},{x+6,y-6},brush.Get(),1.3f);
    target->DrawLine({x+6,y-6},{x+6,y-1},brush.Get(),1.3f);
  }
  ID2D1Bitmap* Image(ID2D1RenderTarget* target,size_t index,bool grey=false) {
    SelectTarget(target);
    auto& bitmap=grey?grey_covers[index]:covers[index];
    if(!bitmap)bitmap=DecodeImage(target,cave::launcher::kCoverArtwork[index].resource,grey);
    return bitmap.Get();
  }
  ID2D1Bitmap* Logo(ID2D1RenderTarget* target) {
    SelectTarget(target);
    if(!logo)logo=DecodeImage(target,cave::launcher::kBrandLogoResource);
    return logo.Get();
  }
  void SelectTarget(ID2D1RenderTarget* target) {
    if(last_target!=target) {
      for(auto& bitmap:covers)bitmap.Reset();
      for(auto& bitmap:grey_covers)bitmap.Reset();
      logo.Reset();last_target=target;
    }
  }
  ComPtr<ID2D1Bitmap> DecodeImage(ID2D1RenderTarget* target,unsigned identity,bool grey=false) {
    const auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(identity),RT_RCDATA);
    if (!resource) throw std::runtime_error("Missing embedded artwork resource");
    const auto bytes=SizeofResource(nullptr,resource);auto data=LockResource(LoadResource(nullptr,resource));
    if (!bytes || bytes>5*1024*1024 || !data) throw std::runtime_error("Invalid embedded artwork");
    ComPtr<IWICStream> stream;Check(wic->CreateStream(&stream),"Create artwork stream");
    Check(stream->InitializeFromMemory(static_cast<BYTE*>(data),bytes),"Read embedded artwork");
    ComPtr<IWICBitmapDecoder> decoder;Check(wic->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder),"Decode launcher artwork");
    ComPtr<IWICBitmapFrameDecode> frame;Check(decoder->GetFrame(0,&frame),"Read artwork frame");
    UINT width{},height{};Check(frame->GetSize(&width,&height),"Read artwork bounds");
    if (!width || !height || width>8192 || height>8192) throw std::runtime_error("Artwork dimensions exceed bounds");
    ComPtr<IWICFormatConverter> converter;Check(wic->CreateFormatConverter(&converter),"Create artwork converter");
    Check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Convert artwork");
    ComPtr<ID2D1Bitmap> bitmap;
    if(grey) {
      const auto stride=width*4;
      std::vector<BYTE> pixels(size_t(stride)*height);
      Check(converter->CopyPixels(nullptr,stride,UINT(pixels.size()),pixels.data()),"Read artwork pixels");
      for(size_t offset=0;offset<pixels.size();offset+=4) {
        const auto luminance=BYTE((29*unsigned(pixels[offset])+150*unsigned(pixels[offset+1])+77*unsigned(pixels[offset+2])+128)>>8);
        pixels[offset]=pixels[offset+1]=pixels[offset+2]=luminance;
      }
      const auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
      Check(target->CreateBitmap({width,height},pixels.data(),stride,properties,&bitmap),"Upload grey artwork");
    } else Check(target->CreateBitmapFromWicBitmap(converter.Get(),nullptr,&bitmap),"Upload artwork");
    return bitmap;
  }
  void Card(ID2D1RenderTarget* target,const std::vector<Row>& rows,
            size_t index,D2D1_RECT_F rectangle,size_t selected,int hover,bool hidden,
            bool sdoj_external=false,ExternalHit external_hover={}) {
    const auto& game=LauncherCatalogue()[index];const bool soon=game.coming_soon;
    const bool sdoj=game.id=="sdoj";
    const auto project=RecompProject(game,sdoj_external);
    const auto project_icon=LayoutProjectIcon(rectangle);
    const bool show_project=!project.empty();
    const bool project_hover=external_hover==ExternalHit{ExternalControl::ProjectIcon,project};
    const auto subtitle=game.id==cave::launcher::kDonPachi.id?
        std::wstring(cave::launcher::kDonPachiProjectName):Wide(game.name);
    const bool missing=cave::launcher::MissingGameData(rows[index].inspection);
    const bool muted=soon||missing||hidden;
    const auto color=muted?0xf6f7f4:0xffffff;
    ComPtr<ID2D1SolidColorBrush> background,border;
    Check(target->CreateSolidColorBrush(D2D1::ColorF(color),&background),"Create card fill");
    Check(target->CreateSolidColorBrush(D2D1::ColorF(selected==index?0x37644c:(hover==int(index)?0xa0b1a3:0xdee3db)),&border),"Create card border");
    auto round=D2D1::RoundedRect(rectangle,13,13);target->FillRoundedRectangle(round,background.Get());
    ComPtr<ID2D1RoundedRectangleGeometry> clip;Check(factory->CreateRoundedRectangleGeometry(round,&clip),"Create card clip");
    target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),clip.Get()),nullptr);
    const auto inset=cave::launcher::kCoverInset;
    const D2D1_RECT_F art{rectangle.left+inset,rectangle.top+inset,rectangle.right-inset,rectangle.bottom-74};
    auto bitmap=Image(target,index,muted);
    const auto& artwork=cave::launcher::kCoverArtwork[index];
    const auto source=ArtworkSource(bitmap,artwork);
    const float width=source.right-source.left,height=source.bottom-source.top;
    const float scale=artwork.contain?std::min((art.right-art.left)/width,(art.bottom-art.top)/height):
                                      std::max((art.right-art.left)/width,(art.bottom-art.top)/height);
    const float x=art.left-((width*scale)-(art.right-art.left))*.5f;
    const float y=artwork.contain?art.top:art.top-((height*scale)-(art.bottom-art.top))*artwork.focus_y;
    const D2D1_RECT_F cover{x,y,x+width*scale,y+height*scale};
    target->PushAxisAlignedClip(art,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target->DrawBitmap(bitmap,cover,soon?.26f:(muted?.55f:1.f),D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,source);
    target->PopAxisAlignedClip();
    if(soon || hidden) {
      ComPtr<ID2D1SolidColorBrush> shade;
      Check(target->CreateSolidColorBrush(D2D1::ColorF(0xf3f4f0,.87f),&shade),"Create future-cover label");
      const D2D1_RECT_F label{art.left,art.bottom-29,art.right,art.bottom};
      target->FillRectangle(label,shade.Get());
      Text(target,T(hidden?L"已隐藏":L"敬请期待"),{label.left+10,label.top+6,label.right-10,label.bottom},11,0x647168);
    }
    const auto left=rectangle.left+12;
    const auto title_right=show_project?project_icon.left-4:rectangle.right-12;
    Text(target,Wide(game.DisplayName()),{left,rectangle.bottom-64,title_right,rectangle.bottom-27},
        13,muted?0x647168:(selected==index?0x2f5b42:0x23312d),DWRITE_TEXT_ALIGNMENT_LEADING,true);
    if(show_project)ExternalLinkIcon(target,project_icon,project_hover);
    const auto subtitle_top=rectangle.bottom-24;
    const auto switch_bounds=LayoutSdojSwitch(rectangle);
    const auto subtitle_right=sdoj?switch_bounds.left-4:rectangle.right-12;
    Text(target,subtitle,{left,subtitle_top,subtitle_right,subtitle_top+14},10,0x748179);
    if(sdoj)SdojSwitch(target,switch_bounds,sdoj_external,external_hover.control==ExternalControl::SdojToggle);
    target->PopLayer();
    target->DrawRoundedRectangle(round,border.Get(),selected==index?2.5f:1.f);
  }
  void DrawBigPicture(ID2D1RenderTarget* target, const BigPictureLayout& view,
                      const std::vector<Row>& rows, const cave::launcher::GameLibrary& library,
                      std::wstring_view message, bool busy, bool menu, size_t menu_selection,bool sdoj_external=false) {
    constexpr UINT32 background=0x141520, panel=0x202332, text=0xf7f5ff, secondary=0xbfc1d1, accent=0xcfacff;
    const float scale=view.Scale(), margin=view.Margin();
    target->Clear(D2D1::ColorF(background));
    const auto fill=[&](D2D1_RECT_F area, UINT32 color, float radius=0) {
      ComPtr<ID2D1SolidColorBrush> brush;
      Check(target->CreateSolidColorBrush(D2D1::ColorF(color),&brush),"Create big picture brush");
      if(radius>0)target->FillRoundedRectangle(D2D1::RoundedRect(area,radius,radius),brush.Get());
      else target->FillRectangle(area,brush.Get());
    };
    const auto outline=[&](D2D1_RECT_F area, UINT32 color, float weight) {
      ComPtr<ID2D1SolidColorBrush> brush;
      Check(target->CreateSolidColorBrush(D2D1::ColorF(color),&brush),"Create big picture focus brush");
      target->DrawRoundedRectangle(D2D1::RoundedRect(area,6*scale,6*scale),brush.Get(),weight*scale);
    };
    const auto label=[&](std::wstring_view value, D2D1_RECT_F area, float size, UINT32 color,
                         DWRITE_TEXT_ALIGNMENT align=DWRITE_TEXT_ALIGNMENT_LEADING, bool wrap=false) {
      Text(target,value,area,std::round(size*scale),color,align,wrap);
    };
    const auto desktop=view.DesktopButton();
    fill(desktop,panel,7*scale);
    label(T(L"桌面模式"),{desktop.left,desktop.top+7*scale,desktop.right,desktop.bottom},15,text,DWRITE_TEXT_ALIGNMENT_CENTER);
    label(LauncherCaption(),{margin,19*scale,desktop.left-12*scale,59*scale},28,text);

    if(view.count) {
      label(std::to_wstring(view.Page()+1)+L" / "+std::to_wstring(view.PageCount()),
          {margin,62*scale,view.width-margin,86*scale},14,secondary,DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    for(size_t position=view.First();position<view.Last();++position) {
      const size_t index=library.visible[position];
      const bool selected=position==view.selected;
      const auto& game=LauncherCatalogue()[index];
      const bool available=CanPlay(rows[index].inspection) || (game.id=="sdoj" && sdoj_external);
      const auto bitmap=Image(target,index,!available);
      const auto& artwork=cave::launcher::kCoverArtwork[index];
      const auto source=ArtworkSource(bitmap,artwork);
      const auto aspect=(source.right-source.left)/(source.bottom-source.top);
      const auto cover=view.Cover(position,aspect);
      if(cover.bottom<=cover.top)continue;
      const auto padding=view.CoverPadding();
      const D2D1_RECT_F frame{cover.left-padding,cover.top-padding,cover.right+padding,cover.bottom+padding};
      fill(frame,panel,6*scale);
      target->DrawBitmap(bitmap,cover,available?1.f:.57f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,source);
      if(selected)outline(frame,accent,3);
    }

    const float footer=view.height-119*scale;
    fill({0,footer,view.width,view.height},panel);
    bool play=false,setup=false;
    if(view.count) {
      const auto index=library.visible[view.selected];
      const auto& inspection=rows[index].inspection;
      const auto& game=LauncherCatalogue()[index];
      const bool sdoj=game.id=="sdoj",external=sdoj&&sdoj_external;
      play=external||CanPlay(inspection);
      setup=DonPachiNeedsSetup(inspection);
      auto title=Wide(game.DisplayName());
      if(sdoj && external)title+=L"   ·   SDOJ-Recomp (eandis)";
      if(game.id==cave::launcher::kDonPachi.id){title+=L"   ·   ";title+=cave::launcher::kDonPachiProjectName;}
      label(title,{margin,footer+8*scale,view.width-margin,footer+48*scale},24,text);
      const auto caption=message.empty() ? (external?L"SDOJ-Recomp":std::wstring(Status(inspection))) : std::wstring(message);
      label(caption,{margin,footer+46*scale,view.width-margin,footer+78*scale},15,secondary);
    } else {
      label(T(L"没有可显示的游戏"),{margin,footer+11*scale,view.width-margin,footer+54*scale},22,text);
    }
    const float control_top=footer+86*scale;
    const float control_width=(view.width-2*margin)/3;
    auto controls=[&](size_t item,const wchar_t* value) {
      const auto left=margin+item*control_width;
      label(value,{left,control_top,left+control_width,view.height},15,secondary,DWRITE_TEXT_ALIGNMENT_CENTER);
    };
    controls(0,T(L"方向键　选择"));
    if(play && !busy)
      controls(1,T(L"A / Enter　启动"));
    else if(setup && !busy)
      controls(1,T(L"A / Enter　安装"));
    controls(2,T(L"B / Esc　返回"));

    if(!menu)return;
    ComPtr<ID2D1SolidColorBrush> veil;
    Check(target->CreateSolidColorBrush(D2D1::ColorF(background,0.86f),&veil),"Create big picture menu backdrop");
    target->FillRectangle({0,0,view.width,view.height},veil.Get());
    const std::array actions{T(L"桌面模式"),T(L"退出启动器")};
    for(size_t item=0;item<actions.size();++item) {
      const auto bounds=view.MenuItem(item);
      fill(bounds,panel,7*scale);
      if(item==menu_selection)outline(bounds,accent,2);
      label(actions[item],{bounds.left,bounds.top+9*scale,bounds.right,bounds.bottom},18,text,DWRITE_TEXT_ALIGNMENT_CENTER);
    }
  }
  void Draw(ID2D1RenderTarget* target,const Layout& layout,const std::vector<Row>& rows,size_t selected,int hover,bool busy,
            std::wstring_view message,std::string_view running_game={},bool preview=false,int focus=0,
            std::wstring_view override_hint={},const cave::launcher::GameLibrary* library=nullptr,bool update_ready=false,
            const CardDrag* drag=nullptr,int button_hover=-1,bool sdoj_external=false,ExternalHit external_hover={}) {
    const bool running=!running_game.empty();
    const auto margin=layout.margin();target->Clear(D2D1::ColorF(0xf3f4f0));
    const auto brand=Logo(target);const auto brand_size=brand->GetSize();
    const auto brand_box=layout.brand();
    const auto brand_scale=std::min((brand_box.right-brand_box.left)/brand_size.width,(brand_box.bottom-brand_box.top)/brand_size.height);
    target->DrawBitmap(brand,{brand_box.left,brand_box.top,brand_box.left+brand_size.width*brand_scale,brand_box.top+brand_size.height*brand_scale},
        1.f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    const std::array language_codes{L"CN",L"EN",L"JP"};
    const std::array language_choices{Language::Chinese,Language::English,Language::Japanese};
    for(size_t index=0;index<language_choices.size();++index) {
      const auto rectangle=layout.language_choice(index);
      const bool active=language==language_choices[index];
      const bool focused=focus==10+int(index);
      Text(target,language_codes[index],{rectangle.left,rectangle.top+4,rectangle.right,rectangle.bottom},12,
          active?0x2f5b42:(busy||running?0xadb5ad:0x7d8881),DWRITE_TEXT_ALIGNMENT_CENTER);
      if(active||focused) {
        ComPtr<ID2D1SolidColorBrush> brush;
        Check(target->CreateSolidColorBrush(D2D1::ColorF(active?0x2f5b42:0x7d8881),&brush),"Create language accent");
        target->DrawLine({rectangle.left+11,rectangle.bottom-3},{rectangle.right-11,rectangle.bottom-3},brush.Get(),active?2.f:1.f);
      }
      if(index+1<language_choices.size())
        Text(target,L"/",{rectangle.right,rectangle.top+4,rectangle.right+10,rectangle.bottom},12,0xb7bfb5,DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if(library) {
      const auto eye=layout.hidden_games();
      const D2D1_POINT_2F center{(eye.left+eye.right)/2,(eye.top+eye.bottom)/2};
      ComPtr<ID2D1SolidColorBrush> brush;
      Check(target->CreateSolidColorBrush(D2D1::ColorF(library->show_hidden?0x2f5b42:0x7d8881),&brush),"Create visibility icon");
      target->DrawEllipse(D2D1::Ellipse(center,8.f,5.f),brush.Get(),1.35f);
      target->FillEllipse(D2D1::Ellipse(center,2.2f,2.2f),brush.Get());
      if(!library->show_hidden)
        target->DrawLine({center.x-8,center.y+7},{center.x+8,center.y-7},brush.Get(),1.35f);
    }
    if(update_ready) {
      const auto badge=layout.update_badge();
      const D2D1_POINT_2F center{(badge.left+badge.right)/2,(badge.top+badge.bottom)/2};
      ComPtr<ID2D1SolidColorBrush> brush;
      Check(target->CreateSolidColorBrush(D2D1::ColorF(0xb93836),&brush),"Create update indicator");
      target->FillEllipse(D2D1::Ellipse(center,4.5f,4.5f),brush.Get());
    }
    {
      const auto r=layout.nickname();
      ComPtr<ID2D1SolidColorBrush> fill,border;
      Check(target->CreateSolidColorBrush(D2D1::ColorF(busy||running?0xf3f4f0:0xffffff),&fill),"Create nickname fill");
      Check(target->CreateSolidColorBrush(D2D1::ColorF(focus==2?0x37644c:0xd9dfd6),&border),"Create nickname border");
      target->FillRectangle(r,fill.Get());
      target->DrawRectangle({r.left+.5f,r.top+.5f,r.right-.5f,r.bottom-.5f},border.Get());
      if(preview)Text(target,L"Player",{r.left+6,r.top+6,r.right-6,r.bottom},13,0x23312d);
    }
    if(preview) {
      Button(target,layout.fullscreen(),L"1280 × 720     ▾");
      Button(target,layout.save(),T(L"保存"),false,true);
    }
    Text(target,T(L"排行榜昵称"),layout.nickname_label(),10,0x627368);
    target->PushAxisAlignedClip({0,layout.header()-2,layout.width,layout.footer()},D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t position=0;position<layout.game_count;++position) {
      const auto index=library?library->visible[position]:position;
      const auto rectangle=layout.card(position);if(rectangle.bottom<layout.header() || rectangle.top>layout.footer())continue;
      if(drag && drag->game==index) {
        ComPtr<ID2D1SolidColorBrush> fill,border;
        Check(target->CreateSolidColorBrush(D2D1::ColorF(0xe7ece5),&fill),"Create drag placeholder");
        Check(target->CreateSolidColorBrush(D2D1::ColorF(0xb4c3b5),&border),"Create drag placeholder border");
        const auto round=D2D1::RoundedRect(rectangle,13,13);
        target->FillRoundedRectangle(round,fill.Get());target->DrawRoundedRectangle(round,border.Get());
        continue;
      }
      Card(target,rows,index,rectangle,selected,hover,library&&library->Hidden(index),sdoj_external,external_hover);
    }
    if(drag) {
      ComPtr<ID2D1SolidColorBrush> shadow,accent;
      Check(target->CreateSolidColorBrush(D2D1::ColorF(0x23312d,.12f),&shadow),"Create drag shadow");
      Check(target->CreateSolidColorBrush(D2D1::ColorF(0x37644c),&accent),"Create drop marker");
      if(drag->insertion)target->FillRoundedRectangle(D2D1::RoundedRect(*drag->insertion,2,2),accent.Get());
      // Paint the moving card last, in the same logical coordinates as the
      // pointer. Keep its grab point fixed instead of snapping it to a slot.
      const auto r=drag->rectangle;
      target->FillRoundedRectangle(D2D1::RoundedRect({r.left-3,r.top+3,r.right+3,r.bottom+9},16,16),shadow.Get());
      auto layer=D2D1::LayerParameters();layer.opacity=.94f;
      target->PushLayer(layer,nullptr);
      Card(target,rows,drag->game,r,drag->game,-1,library&&library->Hidden(drag->game),sdoj_external);
      target->PopLayer();
    }
    target->PopAxisAlignedClip();
    const bool has_games=layout.game_count!=0;
    const auto& inspection=rows[selected].inspection;
    const bool external=has_games && LauncherCatalogue()[selected].id=="sdoj" && sdoj_external;
    const bool setup=has_games && DonPachiNeedsSetup(inspection);
    const auto project=has_games && !busy && !running?
        RecompProject(LauncherCatalogue()[selected],sdoj_external):std::wstring_view{};
    const bool show_project=!project.empty();
    const auto project_link=layout.project_link(project);
    const bool stacked=show_project && project_link.left==margin;
    const float offset=stacked?13.f:0.f;
    Text(target,has_games?Wide(LauncherCatalogue()[selected].DisplayName()):T(L"没有显示的游戏"),
        {margin,layout.height-105-offset,layout.play().left-18,layout.height-76-offset},18,0x23312d);
    const auto status=running?T(L"请先退出正在运行的游戏"):busy?T(L"正在读取／校验所选游戏…"):
        external?L"SDOJ-Recomp (eandis)":has_games?Status(inspection):L"";
    const auto status_right=show_project && !stacked?project_link.left-8:layout.play().left-18;
    Text(target,status,{margin,layout.height-80-offset,status_right,layout.height-64-offset},
        11,CanPlay(inspection)?0x356c4b:0x90754f);
    if(show_project && project_link.right>project_link.left)
      Text(target,project,project_link,11,external_hover.control==ExternalControl::ProjectLink?0x254b35:0x37644c,
          DWRITE_TEXT_ALIGNMENT_LEADING,false,true);
    Button(target,layout.play(),running?T(L"游戏运行中"):setup?T(L"安装首領蜂"):T(L"启动游戏"),true,
        busy||running||!has_games||(!external&&!setup&&!CanPlay(inspection)),button_hover==8);
    Button(target,layout.data(),T(L"游戏数据…"),false,
        busy||running||!has_games||LauncherCatalogue()[selected].coming_soon,button_hover==6);
    Button(target,layout.file_upload(),T(L"上传文件"),false,busy||running||!has_games,button_hover==7);
    Button(target,layout.controls(),T(L"输入设置"),false,
        busy||running||!has_games||LauncherCatalogue()[selected].id==cave::launcher::kDonPachi.id,button_hover==13);
    Button(target,layout.config(),T(L"配置文件"),false,false,button_hover==4);
    Button(target,layout.logs(),T(L"上传日志"),false,busy||running,button_hover==5);
    Button(target,layout.big_picture_button(),T(L"大屏模式"),false,busy||running,button_hover==9);
    if ((focus>=4 && focus<10) || focus==13 || focus==14 || (focus==15 && update_ready) ||
        (focus==kProjectLinkFocus && show_project)) {
      const auto rectangle=focus==kProjectLinkFocus?project_link:layout.focus_button(focus);
      ComPtr<ID2D1SolidColorBrush> brush;Check(target->CreateSolidColorBrush(D2D1::ColorF(0x37644c),&brush),"Create focus indicator");
      target->DrawRoundedRectangle(D2D1::RoundedRect({rectangle.left-3,rectangle.top-3,rectangle.right+3,rectangle.bottom+3},10,10),brush.Get(),2);
    }
    const auto music_warning=cave::launcher::ArrangeWarning(rows[selected].arrange,language);
    Text(target,!busy && !music_warning.empty()?music_warning:busy||running||override_hint.empty()?message:override_hint,
        {margin,layout.height-62,layout.play().left-18,layout.height-46},11,0x7d8881);
    if (preview) Text(target,T(L"视觉预览 · 安装状态为示例"),{margin+200,layout.height-12,layout.width-margin,layout.height},10,0x939d94,DWRITE_TEXT_ALIGNMENT_TRAILING);
  }
  ComPtr<ID2D1Factory> factory;
  ComPtr<IWICImagingFactory> wic;
 private:
  ComPtr<IDWriteFactory> write;
  std::map<float,ComPtr<IDWriteTextFormat>> formats;
  std::array<ComPtr<ID2D1Bitmap>,cave::launcher::kCoverArtwork.size()> covers,grey_covers;
  ComPtr<ID2D1Bitmap> logo;
  ID2D1RenderTarget* last_target{};
};
struct Work {
  std::atomic<bool> alive{true};
  std::atomic<bool> cancelled{false};
  std::atomic<uint64_t> copied{},total{};
  std::vector<Row> rows;
  std::optional<LaunchSpec> launch;
  std::optional<bool> donpachi_fullscreen;
  std::optional<GameExitShortcuts> game_exit_shortcuts;
  std::optional<CopyResult> copy_result;
  std::string error;
  bool starting{},copying{},external{};
  size_t selected{};
};
class App {
 public:
  explicit App(Options settings):options(std::move(settings)),rows(LauncherCatalogue().size()) {
    const auto ini=Ini::Load(EnsureConfig(options.root));for(size_t i=0;i<rows.size();++i) rows[i].inspection.game=LauncherCatalogue()[i].id;
    sdoj_external=cave::launcher::ReadSdojRecomp(ini);
    selected=cave::launcher::InitialSelection(ini,options.selection);
    library=cave::launcher::GameLibrary(ini);
    if(!options.selection.empty() && library.Hidden(selected)) {
      library.show_hidden=true;
      library.Rebuild();
    }
    if(library.Hidden(selected) && !library.show_hidden && !library.visible.empty())selected=library.visible.front();
    layout.game_count=library.visible.size();
    options.big_picture=options.big_picture||cave::launcher::ReadBigPicture(ini);
    big_picture_mode=cave::launcher::ReadBigPictureMode(ini);
    saved_window_size=cave::launcher::ReadWindowSize(ini);
    language=options.language.value_or(cave::launcher::ReadLanguage(ini));
    message=T(L"正在检测游戏数据…");
  }
  ~App() {if(work)work->alive=false;if(worker.joinable())worker.detach();}
  void SaveUpdateState() noexcept {
    try {update::Save(options.root,update_state);}
    catch(const std::exception& error){Log(options.root,error.what());}
  }
  void RefreshUpdates() {
    update_auto=false;
    try {
      update_auto=update::Enabled(Ini::Load(ConfigPath(options.root)));
      if(update_auto && !update_request && update::ParseVersion(cave::launcher::kProductVersion) && update::Due(update_state,update::Now()))
        update_request.emplace(update_fetch);
    }catch(const std::exception& error){Log(options.root,error.what());}
  }
  void StartUpdates() {
    packaged_update=update::PackagedInstallation(options.root,cave::launcher::kProductVersion);
    update_state=update::Load(options.root);RefreshUpdates();
    SetTimer(hwnd,kUpdateTimer,500,nullptr);
  }
  bool HasUpdate() const {return update_state.latest && update::Newer(cave::launcher::kProductVersion,*update_state.latest);}
  bool ProgramReady() const {
    return update_auto && HasUpdate() && ready_program && ready_program->tag==update_state.latest->tag &&
           ready_program->program==update_state.latest->program;
  }
  void ShowUpdate() {
    if(update_dialog || child || busy || close_requested || !HasUpdate())return;
    const bool ready=ProgramReady();
    if(ready && GameIsRunning(options.root))return;
    update_dialog=true;struct Guard{bool& value;~Guard(){value=false;}} guard{update_dialog};
    const auto release=*update_state.latest;
    const auto title=std::wstring(ready?T(L"可更新到 "):T(L"发现新版本 "))+Wide(release.tag);
    const auto instruction=ready?T(L"重启启动器以安装更新。"):T(L"发布页提供更新说明和下载。");
    auto notes=Wide(release.notes);
    size_t cut=0,lines=0;
    for(;cut<notes.size() && cut<800 && lines<12;++cut)if(notes[cut]==L'\n')++lines;
    if(cut<notes.size()) {
      if(cut && notes[cut-1]>=0xd800 && notes[cut-1]<=0xdbff)--cut;
      notes.resize(cut);notes+=L"\n…";
    }
    const TASKDIALOG_BUTTON buttons[]{{IDYES,ready?T(L"重启并更新"):T(L"打开发布页")},{IDCANCEL,T(L"关闭")}};
    TASKDIALOGCONFIG dialog{};dialog.cbSize=sizeof(dialog);dialog.hwndParent=hwnd;
    dialog.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;
    dialog.pszWindowTitle=kWindowTitle;dialog.pszMainInstruction=title.c_str();dialog.pszContent=instruction;
    if(!notes.empty()) {
      dialog.pszExpandedInformation=notes.c_str();
      dialog.pszExpandedControlText=T(L"收起更新说明");
      dialog.pszCollapsedControlText=T(L"查看更新说明");
    }
    dialog.cButtons=UINT(std::size(buttons));dialog.pButtons=buttons;dialog.nDefaultButton=IDCANCEL;
    dialog.cxWidth=420;int result=IDCANCEL;
    Check(TaskDialogIndirect(&dialog,&result,nullptr,nullptr),"Show update notice");
    if(ready) {
      if(result==IDYES && !GameIsRunning(options.root) && SavePreferences()) {
        update::Save(options.root,update_state);
        start_update_worker(options.root,Wide(release.tag));
        close_requested=true;
        PostMessageW(hwnd,WM_CLOSE,0,0);
      }
      return;
    }
    // Only a successfully displayed dialog counts as a reminder. Fetching,
    // caching and deferring while a game runs never mark the release as shown.
    update_state.notified.insert(release.tag);SaveUpdateState();
    if(result==IDYES && update::Trusted(release))OpenPath(hwnd,FromUtf8(release.url));
  }
  void UpdateTick() {
    if(update_dialog)return;
    if(update_request) {
      if(auto result=update_request->Poll()) {
        update_request.reset();update::Apply(update_state,*result,update::Now());SaveUpdateState();
        if(!result->okay)Log(options.root,"update-check: "+result->error);
        InvalidateRect(hwnd,nullptr,FALSE);
      }
    }
    if(program_download) {
      if(auto result=program_download->Poll()) {
        program_download.reset();
        if(result->okay) {
          ready_program=std::move(result->release);
          download_failed_tag.clear();
        }
        else {
          download_failed_tag=result->release ? result->release->tag : std::string{};
          program_retry=std::chrono::steady_clock::now()+std::chrono::minutes(5);
          Log(options.root,"program-update: "+result->error);
        }
        InvalidateRect(hwnd,nullptr,FALSE);
      }
    }
    const bool downloadable=packaged_update && HasUpdate() && update::TrustedProgram(*update_state.latest);
    if(update_auto && downloadable && !ProgramReady() && !program_download && !close_requested &&
       !child && !busy && std::chrono::steady_clock::now()>=program_retry) {
      program_download.emplace([root=options.root,release=*update_state.latest,download=download_program]{
        try {
          download(root,release);
          return update::Result{true,release,{}};
        }catch(const std::exception& error) {return update::Result{false,release,error.what()};}
      });
    }
    // Do not interrupt a running game, another application, minimization or an
    // in-progress nickname edit. Leave the reminder pending until the launcher
    // is in use again.
    if(BigPicture() || child || busy || close_requested || preferences_dirty || !IsWindowVisible(hwnd) || IsIconic(hwnd) || GetForegroundWindow()!=hwnd)return;
    const bool download_failed=downloadable && download_failed_tag==update_state.latest->tag;
    if(update_auto && (!downloadable || download_failed) && !update_request &&
       update::ShouldNotify(update_state,cave::launcher::kProductVersion))ShowUpdate();
  }
  void ShowDetails(size_t index) {
    const auto& game=LauncherCatalogue()[index];
    if(busy || child || game.coming_soon)return;
    const auto& row=rows[index];
    std::wstring body=Wide(game.DisplayName())+L"\n"+Status(row.inspection)+L"\n"+row.inspection.data_dir.native();
    if(game.id==cave::launcher::kDonPachi.id)body+=L"\n"+std::wstring(T(L"街机美版 · 1995"))+L"\nDonPachi Recomp · CapsLock4284";
    if(!CanPlay(row.inspection))body+=L"\n"+Wide(Details(row.inspection));
    if(!row.inspection.missing_runtime.empty()) {
      body+=L"\n\n"+std::wstring(T(L"无法读取以下运行组件："));
      for(const auto& issue:InspectRuntime(game,options.root)) {
        const auto* kind=issue.kind=="missing"?T(L"文件不存在"):issue.kind=="not_file"?T(L"路径是文件夹"):T(L"文件无法访问");
        body+=L"\n"+DataChild(options.root,issue.path).native()+L"\n  "+kind+L" (Windows "+std::to_wstring(issue.error)+L")";
      }
      body+=L"\n\n"+std::wstring(T(L"请重新解压程序包并替换同名文件，然后按 F5。若文件存在但无法访问，请检查文件权限或占用。仍有问题可点击上传日志。"));
    }
    if(!game.content.empty()) {
      const auto configured=Ini::Load(ConfigPath(options.root)).ContentDirectory(game,row.inspection.data_dir);
      const auto source=configured.empty()?fs::path{}:Resolve(options.root,configured);
      body+=L"\n\n"+std::wstring(T(L"DLC 查找目录："))+(source.empty()?T(L"已禁用"):source.native());
      for(const auto& item:InspectContent(game,source,CheckLevel::Quick)) {
        const auto* status=item.state==DataState::Verified?T(L"已就绪"):
          item.state==DataState::Unverified?T(L"数据已找到 · 启动时校验"):
          item.state==DataState::Missing?T(L"未安装"):
          item.state==DataState::Nested?T(L"解压目录层级不正确"):
          item.state==DataState::Incomplete?T(L"资源不完整或不匹配"):T(L"数据路径不可用");
        body+=L"\n\n"+Wide(item.name)+L" — "+status;
        if(!item.container.empty())body+=L"\n"+item.container.native();
        else if(!item.directory.empty())body+=L"\n"+item.directory.native();
        if(!item.problems.empty())body+=L"\n"+Wide(item.problems.front());
      }
    }
    const auto music_warning=cave::launcher::ArrangeWarning(row.arrange,language);
    if(!music_warning.empty())body+=L"\n\n"+music_warning;
    MessageBoxW(hwnd,body.c_str(),T(L"游戏数据详情"),MB_OK|MB_ICONINFORMATION);
  }
  void PersistSelection() noexcept {
    try {cave::launcher::SaveSelection(options.root,selected,normal_window_size,BigPicture());}
    catch(const std::exception& error) {Log(options.root,error.what());}
    catch(...) {Log(options.root,"selected-game-save-failed");}
  }
  void Activate() {
    ShowWindow(hwnd,IsIconic(hwnd)?SW_RESTORE:SW_SHOW);
    if(!SetForegroundWindow(hwnd)) {
      FLASHWINFO flash{sizeof(flash),hwnd,FLASHW_TRAY,2,0};FlashWindowEx(&flash);
    }
  }
  void CreatePreferences() {
    const auto instance=GetModuleHandleW(nullptr);
    fullscreen_control=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_HASSTRINGS|WS_VSCROLL,
        0,0,0,0,hwnd,reinterpret_cast<HMENU>(kFullscreenControl),instance,nullptr);
    nickname_control=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
        0,0,0,0,hwnd,reinterpret_cast<HMENU>(kNicknameControl),instance,nullptr);
    save_control=CreateWindowExW(0,L"BUTTON",T(L"保存"),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON|BS_NOTIFY,
        0,0,0,0,hwnd,reinterpret_cast<HMENU>(kSaveControl),instance,nullptr);
    if(!fullscreen_control||!nickname_control||!save_control)throw std::runtime_error("Cannot create quick settings");
    SendMessageW(nickname_control,EM_SETLIMITTEXT,64,0);
    form_background=CreateSolidBrush(RGB(243,244,240));
    PositionPreferences();ReloadPreferences();
  }
  void PositionPreferences(UINT dpi=0) {
    if(!nickname_control)return;
    if(!dpi)dpi=GetDpiForWindow(hwnd);const auto scale=float(dpi)/96;
    const auto font=CreateFontW(-MulDiv(13,int(dpi),96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    for(const auto pair:std::array{std::pair{fullscreen_control,layout.fullscreen()},
                                  std::pair{nickname_control,layout.nickname()},std::pair{save_control,layout.save()}}) {
      const auto r=pair.second;
      if(font)SendMessageW(pair.first,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
      int left=int(r.left*scale),top=int(r.top*scale),width=int((r.right-r.left)*scale),height=int((r.bottom-r.top)*scale);
      if(pair.first==nickname_control) {
        // Keep a real single-line edit (IME, selection, horizontal scrolling).
        // Center its font-height client area inside the separately drawn field.
        const auto dc=GetDC(nickname_control);
        if(!dc)throw std::runtime_error("Cannot measure nickname font");
        const auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(nickname_control,WM_GETFONT,0,0)));
        TEXTMETRICW metrics{};const auto measured=GetTextMetricsW(dc,&metrics);
        SelectObject(dc,old);ReleaseDC(nickname_control,dc);
        if(!measured)throw std::runtime_error("Cannot measure nickname text height");
        const auto inset=MulDiv(6,int(dpi),96);
        const auto line=std::min(height,static_cast<int>(metrics.tmHeight));
        left+=inset;width-=2*inset;top+=(height-line)/2;height=line;
        SendMessageW(nickname_control,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);
      }
      if(pair.first==fullscreen_control) {
        SendMessageW(fullscreen_control,CB_SETITEMHEIGHT,WPARAM(-1),MulDiv(22,int(dpi),96));
        SendMessageW(fullscreen_control,CB_SETITEMHEIGHT,0,MulDiv(26,int(dpi),96));
        height=MulDiv(230,int(dpi),96);
      }
      SetWindowPos(pair.first,nullptr,left,top,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    if(font){if(form_font)DeleteObject(form_font);form_font=font;}
  }
  void EnablePreferences() {
    const bool enabled=preferences_available&&!busy&&!child;
    if(preferences_available && (display_choices.size()==2)!=DonPachiSelected())
      PopulateDisplay(ResolveSharedSettings(Ini::Parse(preferences_text)));
    EnableWindow(fullscreen_control,enabled);EnableWindow(nickname_control,enabled);
    EnableWindow(save_control,enabled&&preferences_dirty);
  }
  void PopulateDisplay(const Settings& settings) {
    display_choices={{true,settings.window_width,settings.window_height}};
    const DisplaySelection current{false,settings.window_width,settings.window_height};
    const bool arcade=DonPachiSelected();
    if(!arcade)display_choices.insert(display_choices.end(),kWindowSelections.begin(),kWindowSelections.end());
    auto found=std::find(display_choices.begin()+1,display_choices.end(),current);
    if(found==display_choices.end()){display_choices.push_back(current);found=display_choices.end()-1;}
    const auto selected=settings.fullscreen?0:std::distance(display_choices.begin(),found);
    SendMessageW(fullscreen_control,CB_RESETCONTENT,0,0);
    for(const auto& choice:display_choices) {
      const auto label=arcade&&!choice.fullscreen?std::wstring(T(L"窗口模式")):DisplayLabel(choice);
      if(SendMessageW(fullscreen_control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()))<0)
        throw std::runtime_error("Cannot populate display choices");
    }
    SendMessageW(fullscreen_control,CB_SETCURSEL,WPARAM(selected),0);
  }
  std::optional<DisplaySelection> CurrentDisplay() const {
    const auto index=SendMessageW(fullscreen_control,CB_GETCURSEL,0,0);
    if(index<0 || size_t(index)>=display_choices.size())return {};
    return display_choices[size_t(index)];
  }
  DisplaySelection SelectedDisplay() const {
    const auto current=CurrentDisplay();
    if(!current)throw std::runtime_error("Choose a display mode");
    return *current;
  }
  void DisplayChanged() {
    if(loading_preferences || !preferences_available)return;
    const auto selection=CurrentDisplay();
    if(!selection)return;
    const auto saved=ResolveSharedSettings(Ini::Parse(preferences_text));
    if(*selection==DisplaySelection{saved.fullscreen,saved.window_width,saved.window_height})return;
    SaveSharedDisplay(options.root,preferences_text,*selection);
    preferences_text=Ini::ReadText(ConfigPath(options.root));
    const auto count=GetWindowTextLengthW(nickname_control);
    std::wstring name(size_t(count)+1,L'\0');GetWindowTextW(nickname_control,name.data(),int(name.size()));name.resize(count);
    preferences_dirty=Utf8(name)!=ResolveSharedSettings(Ini::Parse(preferences_text)).nickname;
    message.clear();EnablePreferences();InvalidateRect(hwnd,nullptr,FALSE);
  }
  void ReloadPreferences() {
    loading_preferences=true;
    try {
      preferences_text=Ini::ReadText(EnsureConfig(options.root));
      const auto ini=Ini::Parse(preferences_text);const auto settings=ResolveSharedSettings(ini);
      sdoj_external=cave::launcher::ReadSdojRecomp(ini);
      big_picture_mode=cave::launcher::ReadBigPictureMode(ini);
      language=options.language.value_or(cave::launcher::ReadLanguage(ini));
      PopulateDisplay(settings);SetWindowTextW(save_control,T(L"保存"));
      SetWindowTextW(nickname_control,Wide(settings.nickname).c_str());
      preferences_available=true;preferences_dirty=false;UpdateOverrideHint(ini);
    } catch(const std::exception& error) {
      preferences_available=false;preferences_dirty=false;
      message=T(L"快捷设置读取失败，请打开配置文件修正后刷新");Log(options.root,error.what());
    }
    loading_preferences=false;EnablePreferences();InvalidateRect(hwnd,nullptr,FALSE);
  }
  void UpdateOverrideHint(const Ini& ini) {
    override_hint.clear();
    if(library.visible.empty())return;
    if(DonPachiSelected()) {
      override_hint=T(L"按键在首領蜂内设置");
      return;
    }
    const auto section="game."+std::string(LauncherCatalogue()[selected].id);
    if(ini.Get(section+".display","fullscreen") || ini.Get(section+".display","window_width") ||
       ini.Get(section+".display","window_height") || ini.Get(section+".community","nickname"))
      override_hint=T(L"此游戏有独立的显示／昵称设置，请在配置文件中调整");
  }
  bool SavePreferences() {
    if(!preferences_dirty)return true;
    if(!preferences_available)throw std::runtime_error("Fix config.ini and refresh before saving quick settings.");
    const auto count=GetWindowTextLengthW(nickname_control);
    std::wstring name(size_t(count)+1,L'\0');GetWindowTextW(nickname_control,name.data(),int(name.size()));name.resize(count);
    const auto nickname=Utf8(name);const auto display=SelectedDisplay();
    (void)PreviewSharedPreferences(preferences_text,display,nickname);
    if(nickname!=ResolveSharedSettings(Ini::Parse(preferences_text)).nickname &&
        !ConfirmNickname(hwnd,cave::launcher::NicknameIssues(Ini::Parse(preferences_text),nickname,options.root))) {
      SetFocus(nickname_control);return false;
    }
    SaveSharedPreferences(options.root,preferences_text,display,nickname);
    ReloadPreferences();message=T(L"快捷设置已保存，下次启动生效");
    Log(options.root,"shared-preferences-updated");
    return true;
  }
  void ManualRefresh() {
    if(busy||child)return;
    if(preferences_dirty && MessageBoxW(hwnd,T(L"刷新将放弃尚未保存的快捷设置，重新读取 config.ini。继续吗？"),
        T(L"刷新配置"),MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
    ReloadPreferences();RefreshUpdates();Refresh();
  }
  void Select(size_t index,bool refresh_data=true) {
    if(work && (work->starting||work->copying))return;
    if(std::ranges::find(library.visible,index)==library.visible.end())return;
    selected=index;focus=0;SetFocus(hwnd);EnsureVisible();
    try {UpdateOverrideHint(Ini::Load(ConfigPath(options.root)));}catch(...){override_hint.clear();}
    EnablePreferences();
    if(refresh_data && !busy && !child)Refresh();
    else InvalidateRect(hwnd,nullptr,FALSE);
  }
  void MoveDesktopSelection(MenuKey key,bool refresh_data=true) {
    const auto position=VisiblePosition();
    if(!position)return;
    const auto columns=layout.columns();
    const auto last=library.visible.size()-1;
    auto next=*position;
    switch(key) {
      case MenuKey::Up: if(next>=columns)next-=columns;break;
      case MenuKey::Down: next=std::min(next+columns,last);break;
      case MenuKey::Left: if(next>0)--next;break;
      case MenuKey::Right: if(next<last)++next;break;
      default: return;
    }
    if(next==*position)return;
    if(!refresh_data && !busy)message.clear();
    Select(library.visible[next],refresh_data);
  }
  void LaunchSelected() {
    if(close_requested)return;
    if(busy||child||library.visible.empty())return;
    if(DonPachiSelected()) {
      const auto ini=Ini::Load(ConfigPath(options.root));
      const auto data=options.data.empty()?ini.DataDirectory(cave::launcher::kDonPachi):options.data;
      rows[selected].inspection=cave::launcher::InspectDonPachi(options.root,data);
      if(DonPachiNeedsSetup(rows[selected].inspection)){ShowDonPachiSetup();return;}
    } else if(ExternalSelected()) {
      if(!fs::is_regular_file(cave::launcher::SdojRecompExecutable(options.root))) {ShowSdojSetup();return;}
    } else if(!CanPlay(rows[selected].inspection))return;
    if(SavePreferences())Refresh(true);
  }
  bool DonPachiSelected() const {return LauncherCatalogue()[selected].id==cave::launcher::kDonPachi.id;}
  bool ExternalSelected() const {return sdoj_external && LauncherCatalogue()[selected].id=="sdoj";}
  std::wstring_view SelectedProjectUrl() const {
    if(BigPicture() || busy || child || library.visible.empty())return {};
    return RecompProject(LauncherCatalogue()[selected],sdoj_external);
  }
  void ShowDonPachiSetup() {
    const auto& data=rows[selected].inspection;
    const bool install=data.state=="donpachi_program_missing";
    std::wstring content;
    if(install) {
      content=T(L"从官方发布页下载 Windows ZIP，完整解压到");
      content+=L"\n"+DataChild(options.root,"external/DonPachi-Recomp").make_preferred().native()+L"\n\n";
    }
    content+=T(L"将美版 donpachi.zip 放到下面的目录，或直接选择 ZIP。");
    content+=L"\n"+fs::path(data.data_dir).make_preferred().native();
    const TASKDIALOG_BUTTON buttons[]{{IDOK,T(L"选择 donpachi.zip…")},{IDYES,T(L"打开官方发布页")}};
    TASKDIALOGCONFIG dialog{};dialog.cbSize=sizeof(dialog);dialog.hwndParent=hwnd;
    dialog.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;
    dialog.pszWindowTitle=kWindowTitle;dialog.pszMainInstruction=T(L"安装首領蜂");
    dialog.pszContent=content.c_str();dialog.dwCommonButtons=TDCBF_CLOSE_BUTTON;
    dialog.cButtons=install?2:1;dialog.pButtons=buttons;dialog.nDefaultButton=IDCLOSE;
    int choice{};
    Check(TaskDialogIndirect(&dialog,&choice,nullptr,nullptr),"Show DonPachi Recomp setup");
    if(choice==IDYES)OpenPath(hwnd,cave::launcher::kDonPachiRelease);
    if(choice==IDOK)Choose();
    InvalidateRect(hwnd,nullptr,FALSE);
  }
  void ShowSdojSetup() {
    const auto directory=(options.root/cave::launcher::kSdojRecompDirectory).make_preferred().native();
    const auto content=std::wstring(T(L"从 eandis 的官方发布页下载 Windows x64 ZIP，然后完整解压到："))+
        L"\n\n"+directory+L"\n\nDoDonPachi SaiDaiOuJou.exe";
    TASKDIALOGCONFIG dialog{};dialog.cbSize=sizeof(dialog);
    dialog.hwndParent=hwnd;dialog.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;
    dialog.pszWindowTitle=kWindowTitle;dialog.pszMainInstruction=T(L"安装 SDOJ-Recomp");
    dialog.pszContent=content.c_str();dialog.dwCommonButtons=TDCBF_CLOSE_BUTTON;
    const TASKDIALOG_BUTTON buttons[]{{IDYES,T(L"打开官方发布页")}};
    dialog.cButtons=UINT(std::size(buttons));dialog.pButtons=buttons;dialog.nDefaultButton=IDCLOSE;
    int choice{};
    Check(TaskDialogIndirect(&dialog,&choice,nullptr,nullptr),"Show SDOJ-Recomp setup");
    if(choice==IDYES)OpenPath(hwnd,cave::launcher::kSdojRecompRelease);
  }
  void ToggleSdoj() {
    if(busy||child)return;
    if(!sdoj_external && !fs::is_regular_file(cave::launcher::SdojRecompExecutable(options.root))) {
      ShowSdojSetup();return;
    }
    const auto before=Ini::ReadText(EnsureConfig(options.root));
    const std::array changes{IniEdit{"game.sdoj","eandis",sdoj_external?"false":"true"}};
    UpdateConfig(options.root,before,changes);
    preferences_text=Ini::ReadText(ConfigPath(options.root));
    sdoj_external=!sdoj_external;
    message.clear();InvalidateRect(hwnd,nullptr,FALSE);
  }
  void SelectLanguage(size_t index) {
    if(busy||child||!preferences_available)return;
    const std::array choices{Language::Chinese,Language::English,Language::Japanese};
    const auto next=choices.at(index);if(next==language)return;
    const std::array changes{IniEdit{"collection","language",cave::launcher::LanguageCode(next)}};
    UpdateConfig(options.root,preferences_text,changes);
    preferences_text=Ini::ReadText(ConfigPath(options.root));
    language=next;options.language.reset();
    PopulateDisplay(ResolveSharedSettings(Ini::Parse(preferences_text)));SetWindowTextW(save_control,T(L"保存"));
    UpdateOverrideHint(Ini::Parse(preferences_text));message.clear();
    InvalidateRect(hwnd,nullptr,FALSE);
  }
  bool BigPicture() const { return desktop_window.has_value(); }
  BigPictureLayout BigView() const {
    return {layout.width,layout.height,library.visible.size(),VisiblePosition().value_or(0),big_picture_mode};
  }
  void FitBigPicture() {
    MONITORINFO monitor{};
    monitor.cbSize=sizeof(monitor);
    if(!GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor))return;
    const auto area=monitor.rcMonitor;
    SetWindowPos(hwnd,HWND_TOP,area.left,area.top,area.right-area.left,area.bottom-area.top,
                 SWP_NOACTIVATE|SWP_FRAMECHANGED);
  }
  void SetBigPicture(bool enabled) {
    const auto style=GetWindowLongPtrW(hwnd,GWL_STYLE);
    if(BigPicture()==enabled || child || (work && (work->starting || work->copying)))return;
    menu_open=false;
    if(enabled) {
      WINDOWPLACEMENT old{};
      old.length=sizeof(old);
      if(!GetWindowPlacement(hwnd,&old))return;
      SendMessageW(fullscreen_control,CB_SHOWDROPDOWN,FALSE,0);
      CancelPointerPress();
      if(IsZoomed(hwnd))ShowWindow(hwnd,SW_RESTORE);
      desktop_window=old;
      desktop_style=style & ~(WS_MINIMIZE|WS_MAXIMIZE);
      SetWindowLongPtrW(hwnd,GWL_STYLE,WS_POPUP|WS_CLIPCHILDREN|(style&WS_VISIBLE));
      FitBigPicture();
    } else {
      const auto placement=*std::exchange(desktop_window,std::nullopt);
      SetWindowLongPtrW(hwnd,GWL_STYLE,desktop_style|WS_VISIBLE);
      SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED);
      SetWindowPlacement(hwnd,&placement);
    }
    for(auto control:{fullscreen_control,nickname_control,save_control})
      ShowWindow(control,enabled?SW_HIDE:SW_SHOWNA);
    if(menu_controller)menu_controller->Reset();
    focus=0;
    SetFocus(hwnd);
    EnsureVisible();
  }
  void ChooseBigPictureGame(size_t position) {
    if(position>=library.visible.size() || child || (work && (work->starting || work->copying)))return;
    if(selected==library.visible[position])return;
    if(!busy)message.clear();
    Select(library.visible[position],false);
  }
  void BigPictureAction(MenuKey key) {
    if(key==MenuKey::None || child || close_requested || (work && (work->starting || work->copying)))return;
    if(menu_open) {
      switch(key) {
        case MenuKey::Up: case MenuKey::Left: menu_selection=0;break;
        case MenuKey::Down: case MenuKey::Right: menu_selection=1;break;
        case MenuKey::Back: menu_open=false;break;
        case MenuKey::Confirm:
          if(menu_selection==0)SetBigPicture(false);
          else PostMessageW(hwnd,WM_CLOSE,0,0);
          break;
        default: break;
      }
      InvalidateRect(hwnd,nullptr,FALSE);
    } else if(key==MenuKey::Back) {
      menu_open=true;
      menu_selection=0;
      InvalidateRect(hwnd,nullptr,FALSE);
    } else if(key==MenuKey::Confirm) {
      LaunchSelected();
    } else {
      ChooseBigPictureGame(BigView().Move(key));
    }
  }
  void StartMenuController() {
    if(!SetTimer(hwnd,kMenuControllerTimer,kMenuPollInterval,nullptr)) {
      Log(options.root,"Cannot start launcher controller timer");
      return;
    }
    try {
      menu_controller=std::make_unique<cave::launcher::MenuController>(options.root);
    } catch(const std::exception& error) {
      Log(options.root,"launcher-controller: "+std::string(error.what()));
    }
  }
  void ResetGameExit() {
    game_exit_window=nullptr;
    if(menu_controller)menu_controller->ResetGameExit();
  }
  HWND FocusedGameExitWindow() const {
    if(!game_exit_shortcuts || !child)return nullptr;
    const auto foreground=GetForegroundWindow();
    return IsGameWindow(foreground,child->pid())?foreground:nullptr;
  }
  void RequestGameExit(HWND game,std::string_view source) {
    if(PostMessageW(game,WM_CLOSE,0,0))
      Log(options.root,"game-shortcut-close game="+active_game+" input="+std::string(source)+" pid="+std::to_string(child->pid()));
    else Log(options.root,"Cannot close game window (Windows error "+std::to_string(GetLastError())+")");
    ResetGameExit();
  }
  void SetGameExitKeyboard(bool enabled) {
    if(game_exit_keyboard==enabled)return;
    const RAWINPUTDEVICE device{HID_USAGE_PAGE_GENERIC,HID_USAGE_GENERIC_KEYBOARD,
                                DWORD(enabled?RIDEV_INPUTSINK:RIDEV_REMOVE),enabled?hwnd:nullptr};
    if(RegisterRawInputDevices(&device,1,sizeof(device)))game_exit_keyboard=enabled;
    else Log(options.root,"Cannot change game exit keyboard registration (Windows error "+std::to_string(GetLastError())+")");
  }
  void GameExitKey(std::uint16_t key,bool released) {
    if(!game_exit_shortcuts || !key)return;
    const auto& keys=game_exit_shortcuts->keyboard;
    const auto bound=std::ranges::find(keys,cave::launcher::ExitVirtualKey(key));
    if(bound==keys.end())return;
    const auto pressed=[&]{return exit_keys_down[0] && (!keys[1] || exit_keys_down[1]);};
    const bool was_down=pressed();
    exit_keys_down[bound-keys.begin()]=!released;
    if(!was_down && pressed()) {
      if(const auto game=FocusedGameExitWindow())RequestGameExit(game,"keyboard");
    }
  }
  void RawGameExitInput(HRAWINPUT handle) {
    if(!game_exit_keyboard || !game_exit_shortcuts)return;
    RAWINPUT input{};
    UINT size=sizeof(input);
    if(GetRawInputData(handle,RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER))!=UINT(-1) && input.header.dwType==RIM_TYPEKEYBOARD)
      GameExitKey(input.data.keyboard.VKey,(input.data.keyboard.Flags&RI_KEY_BREAK)!=0);
  }
  void GameExitTick() {
    if(!menu_controller || !game_exit_shortcuts ||
        (game_exit_shortcuts->buttons.empty() && game_exit_shortcuts->controller_bindings.empty()))return;
    const auto game=FocusedGameExitWindow();
    if(game!=game_exit_window) {
      ResetGameExit();
      game_exit_window=game;
    }
    if(game && menu_controller->PollGameExit(*game_exit_shortcuts))RequestGameExit(game,"controller");
  }
  void MenuControllerTick() {
    if(child) {GameExitTick();return;}
    for(auto& key:exit_keys_awaiting_release)if(key && !KeyHeld(key))key=0;
    if(!menu_controller)return;
    if(close_requested || (work && (work->starting || work->copying)) ||
       GetForegroundWindow()!=hwnd || !IsWindowEnabled(hwnd) || GetCapture() ||
       (!BigPicture() && SendMessageW(fullscreen_control,CB_GETDROPPEDSTATE,0,0))) {
      menu_controller->Reset();
      return;
    }
    const auto key=menu_controller->Poll();
    if(key==MenuKey::None)return;
    if(BigPicture())BigPictureAction(key);
    else if(key==MenuKey::Confirm)LaunchSelected();
    else MoveDesktopSelection(key,false);
  }
  void BigPictureKeyboard(WPARAM key, bool repeated) {
    if(key && std::ranges::find(exit_keys_awaiting_release,key)!=exit_keys_awaiting_release.end())return;
    switch(key) {
      case VK_UP:case VK_NUMPAD8:BigPictureAction(MenuKey::Up);break;
      case VK_DOWN:case VK_NUMPAD2:BigPictureAction(MenuKey::Down);break;
      case VK_LEFT:case VK_NUMPAD4:BigPictureAction(MenuKey::Left);break;
      case VK_RIGHT:case VK_NUMPAD6:BigPictureAction(MenuKey::Right);break;
      case VK_RETURN:case VK_SPACE:if(!repeated)BigPictureAction(MenuKey::Confirm);break;
      case VK_ESCAPE:if(!repeated)BigPictureAction(MenuKey::Back);break;
      case VK_HOME:ChooseBigPictureGame(0);break;
      case VK_END:if(!library.visible.empty())ChooseBigPictureGame(library.visible.size()-1);break;
      case VK_F5:ManualRefresh();break;
      case VK_F11:if(!repeated)SetBigPicture(false);break;
    }
  }
  void BigPictureClick(float x,float y) {
    if(child || (work && (work->starting || work->copying)))return;
    const auto view=BigView();
    if(menu_open) {
      for(size_t item=0;item<2;++item) {
        if(!Inside(view.MenuItem(item),x,y))continue;
        menu_selection=item;
        BigPictureAction(MenuKey::Confirm);
        break;
      }
    } else if(Inside(view.DesktopButton(),x,y)) {
      SetBigPicture(false);
    } else {
      for(size_t position=view.First();position<view.Last();++position) {
        if(!Inside(view.Card(position),x,y))continue;
        if(position==view.selected)LaunchSelected();
        else ChooseBigPictureGame(position);
        break;
      }
    }
  }
  bool Input(MSG& input) {
    if(BigPicture())return false;
    if(input.message!=WM_KEYDOWN || (input.hwnd!=hwnd && !IsChild(hwnd,input.hwnd)))return false;
    try {
      if(card_press || external_pressed.control!=ExternalControl::None) {if(input.wParam==VK_ESCAPE)CancelPointerPress();return true;}
      if(input.wParam==VK_TAB) {
        const auto current=GetFocus();
        int slot=current==fullscreen_control?1:(current==nickname_control?2:(current==save_control?3:focus));
        constexpr int slots=kProjectLinkFocus+1;
        for(int step=0;step<slots;++step) {
          slot=(slot+((GetKeyState(VK_SHIFT)&0x8000)?slots-1:1))%slots;
          const auto control=slot==1?fullscreen_control:(slot==2?nickname_control:(slot==3?save_control:hwnd));
          if(!IsWindowEnabled(control))continue;
          if((slot==6 || slot==8 || slot==13) && library.visible.empty())continue;
          if(slot==6 && (busy||child||LauncherCatalogue()[selected].coming_soon))continue;
          if(slot==7 && (busy||child))continue;
          if(slot==8 && (busy||child||(!CanPlay(rows[selected].inspection)&&!DonPachiNeedsSetup(rows[selected].inspection))))continue;
          if(slot==13 && DonPachiSelected())continue;
          if(slot==9 && (busy||child))continue;
          if(slot>=10 && (busy||child||!preferences_available))continue;
          if(slot==15 && !ProgramReady())continue;
          if(slot==kProjectLinkFocus && SelectedProjectUrl().empty())continue;
          focus=slot;SetFocus(control);InvalidateRect(hwnd,nullptr,FALSE);break;
        }
        return true;
      }
      if(input.hwnd==nickname_control && input.wParam==VK_RETURN) {
        if(SavePreferences()) {SetFocus(hwnd);focus=0;Refresh();}return true;
      }
      if(input.hwnd==save_control && input.wParam==VK_RETURN) {if(SavePreferences())Refresh();return true;}
      if(input.hwnd!=hwnd && input.wParam==VK_F11) {
        SendMessageW(hwnd,input.message,input.wParam,input.lParam);
        return true;
      }
      if(input.hwnd!=hwnd && input.wParam==VK_F5) {ManualRefresh();return true;}
    }catch(const std::exception& error){Log(options.root,error.what());Error(hwnd,error.what());return true;}
    return false;
  }
  void Refresh(bool launch=false,const fs::path& copy_source={}) {
    if (busy || child) return;
    if (worker.joinable()) worker.join();
    busy=true;EnablePreferences();work=std::make_shared<Work>();work->starting=launch;
    work->copying=!copy_source.empty();
    work->external=launch&&(ExternalSelected()||DonPachiSelected());
    work->selected=selected;
    const auto task=work;const auto root=options.root;const auto id=std::string(LauncherCatalogue()[selected].id);const auto profile=options.profile;
    const auto window=hwnd;const auto data=options.data;
    worker=std::thread([task,root,id,profile,window,data,copy_source] {
      try {
        const auto progress=[task,window](uint64_t done,uint64_t total){
          task->copied=done;task->total=total;if(task->alive)PostMessageW(window,kCopyProgress,0,0);
        };
        const auto cancelled=[task]{return task->cancelled.load();};
        if(task->copying)task->copy_result=CopyDataToCollection(root,FindGame(id),copy_source,progress,cancelled);
        task->rows=InspectRows(root,task->selected,true);
        if (task->starting) {
          const auto ini=Ini::Load(ConfigPath(root));
          if(id==cave::launcher::kDonPachi.id) {
            task->launch=cave::launcher::PrepareDonPachi(root,ini,profile,data);
            task->donpachi_fullscreen=ResolveSharedSettings(ini).fullscreen;
          }
          else if(task->external)task->launch=cave::launcher::PrepareSdojRecomp(root,ini,data,GetDpiForSystem());
          else task->launch=PrepareLaunch(FindGame(id),root,ini,profile,data);
          auto shortcuts=ReadGameExitShortcuts(ini,root,id);
          if(shortcuts.keyboard[0] || !shortcuts.buttons.empty() || !shortcuts.controller_bindings.empty())
            task->game_exit_shortcuts=std::move(shortcuts);
          Log(root,"launch-verification "+task->launch->verification);
          if(id=="daioujou") {
            VerificationCache cache(root);
            task->rows[task->selected].arrange=daioujou::InspectArrangeMusic(task->launch->paths.data,CheckLevel::Full,&cache);
          }
        }
      } catch(const std::exception& error) {task->error=error.what();}
      if(task->alive) PostMessageW(window,kWorkDone,0,0);
    });
    InvalidateRect(hwnd,nullptr,FALSE);
  }
  void Complete() {
    if(worker.joinable())worker.join();busy=false;EnablePreferences();
    if (!work) return;
    if(close_requested){work.reset();DestroyWindow(hwnd);return;}
    if (!work->rows.empty()) rows=std::move(work->rows);
    const bool selection_changed=selected!=work->selected;
    try {
      if(!work->error.empty())throw std::runtime_error(work->error);
      if(work->launch) {
        if(work->external)external_session.emplace(options.root);
        if(work->donpachi_fullscreen)cave::launcher::SetDonPachiFullscreen(*work->launch,*work->donpachi_fullscreen);
        child.emplace(StartGame(*work->launch,work->external?work->launch->executable.parent_path():fs::path{}));
        game_exit_shortcuts=std::move(work->game_exit_shortcuts);
        if(game_exit_shortcuts) {
          ResetGameExit();
          const auto& keys=game_exit_shortcuts->keyboard;
          std::ranges::transform(keys,exit_keys_down.begin(),KeyHeld);
          SetGameExitKeyboard(keys[0]!=0);
        }
        game_focus_pending=BigPicture();
        if(game_focus_pending && GetForegroundWindow()==hwnd)AllowSetForegroundWindow(child->pid());
        active_game=work->launch->game;
        message=T(L"正在运行：")+Wide(FindLauncherGame(work->launch->game).DisplayName());SetTimer(hwnd,kChildTimer,250,nullptr);
        const char* engine=!work->external?"":(active_game==cave::launcher::kDonPachi.id?" engine=donpachi":" engine=eandis");
        Log(options.root,"launch game="+work->launch->game+engine+" pid="+std::to_string(child->pid()));
      } else if(work->copy_result) {
        message=T(L"数据已复制并逐文件校验，配置已更新");
        Log(options.root,"data-copy-complete files="+std::to_string(work->copy_result->files)+" bytes="+std::to_string(work->copy_result->bytes));
        if(!work->copy_result->guide_backup.empty())Log(options.root,"placement-guide-backup="+Utf8(work->copy_result->guide_backup));
      } else {message.clear();}
    }catch(const std::exception& error){
      if(!child)external_session.reset();
      message=work->copying?T(L"数据复制未完成，请查看详细信息"):T(L"无法启动，请检查数据和运行组件");
      Log(options.root,error.what());
      if(!BigPicture())Error(hwnd,error.what());
    }
    const bool copied=work->copying;work.reset();
    if(copied)ReloadPreferences();EnablePreferences();
    if(selection_changed&&!child)Refresh();
    InvalidateRect(hwnd,nullptr,FALSE);
  }
  void ChildTick() {
    if(!child)return;
    if(const auto code=child->ExitCode()) {
      const bool external=bool(external_session);
      if(!external)cave::launcher::reports::RecordProcessExit(options.root,active_game,child->pid(),child->CreationTime(),*code);
      const bool requested_close=!external && child->RequestedWindowClose(options.root,active_game,*code);
      const bool failed=*code && !requested_close;
      const auto detail=T(L"游戏异常退出（退出码 ")+std::to_wstring(*code)+T(L"）。\n\n日志目录：logs/")+Wide(active_game)+T(L"/\n启动器可以继续使用。");
      Log(options.root,"child-exit code="+std::to_string(*code));
      if(failed && active_game==cave::launcher::kDonPachi.id)message=T(L"DonPachi Recomp 已退出，请查看其安装目录内的日志");
      else message=failed?(external?T(L"SDOJ-Recomp 已退出，请查看其目录内的日志"):T(L"游戏异常退出，请查看该游戏的日志")):L"";
      const auto exited_game=active_game;
      if(game_exit_shortcuts) {
        std::ranges::transform(game_exit_shortcuts->keyboard,exit_keys_awaiting_release.begin(),
            [](std::uint16_t key){return KeyHeld(key)?key:0;});
        SetGameExitKeyboard(false);
        game_exit_shortcuts.reset();
        ResetGameExit();
      }
      child.reset();external_session.reset();active_game.clear();game_focus_pending=false;KillTimer(hwnd,kChildTimer);
      if(menu_controller)menu_controller->Reset();
      ShowWindow(hwnd,SW_SHOWNORMAL);SetForegroundWindow(hwnd);EnablePreferences();InvalidateRect(hwnd,nullptr,FALSE);
      if(!external && !BigPicture() && failed && MessageBoxW(hwnd,(detail+L"\n\n"+T(L"现在上传日志？")).c_str(),kWindowTitle,MB_YESNO|MB_ICONWARNING)==IDYES)
        show_reports(hwnd,options.root,language,exited_game);
    } else if(game_focus_pending) {
      // Leave the launcher behind the game so Windows can return focus when the game closes.
      if(const auto game=VisibleGameWindow(child->pid());game)
        game_focus_pending=GetForegroundWindow()==hwnd && !SetForegroundWindow(game);
    }
  }
  void Choose(bool copy=false) {
    if(busy||child||LauncherCatalogue()[selected].coming_soon)return;
    const bool arcade=DonPachiSelected();
    ComPtr<IFileOpenDialog> dialog;Check(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)),"Create data directory picker");
    DWORD flags{};Check(dialog->GetOptions(&flags),"Read directory picker options");
    Check(dialog->SetOptions(flags|(arcade?FOS_FILEMUSTEXIST:FOS_PICKFOLDERS)|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST),"Configure game data picker");
    if(arcade) {
      const COMDLG_FILTERSPEC filter[]{{L"donpachi.zip",L"donpachi.zip"}};
      Check(dialog->SetFileTypes(1,filter),"Set DonPachi data filter");
      dialog->SetTitle(T(L"选择 donpachi.zip…"));
    } else {
      dialog->SetTitle(copy?T(L"选择要复制到合集的游戏原始数据目录"):T(L"选择游戏数据目录"));
    }
    const auto result=dialog->Show(hwnd);if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return;Check(result,"Select game data directory");
    ComPtr<IShellItem> item;Check(dialog->GetResult(&item),"Read selected directory");
    PWSTR value{};Check(item->GetDisplayName(SIGDN_FILESYSPATH,&value),"Read selected directory path");
    fs::path path(value);CoTaskMemFree(value);
    if(arcade) {
      if(_wcsicmp(path.filename().c_str(),L"donpachi.zip")) {
        MessageBoxW(hwnd,T(L"请放置 donpachi.zip（美版）"),kWindowTitle,MB_OK|MB_ICONWARNING);
        return;
      }
      path=path.parent_path();
    }
    if(copy) {
      const auto& game=LauncherCatalogue()[selected];
      const auto target=DataChild(options.root,game.data_dir);
      const auto prompt=T(L"将完整复制并校验游戏数据。\n\n来源：")+path.native()+T(L"\n目标：")+target.native()+
          (PreservesXArrangeOnImport(game.id)?T(L"\n\n导入会保留目标中的放置说明和 X Arrange 曲包。\n继续吗？"):
          T(L"\n\n源文件保持不变。目标须为空或只含放置说明；已有游戏数据不会被覆盖。\n继续吗？"));
      if(MessageBoxW(hwnd,prompt.c_str(),T(L"复制到合集"),MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
      if(!SavePreferences())return;message=T(L"正在校验源数据…");Refresh(false,path);return;
    }
    if(!SavePreferences())return;
    SetDataDirectory(options.root,LauncherCatalogue()[selected],path);
    ReloadPreferences();Log(options.root,"data-directory-updated game="+std::string(LauncherCatalogue()[selected].id));Refresh();
  }
  void DataMenu() {
    if(busy||child||library.visible.empty()||LauncherCatalogue()[selected].coming_soon)return;
    if(DonPachiSelected()) {
      if(DonPachiNeedsSetup(rows[selected].inspection))ShowDonPachiSetup();
      else Choose();
      return;
    }
    const auto menu=CreatePopupMenu();if(!menu)throw std::runtime_error("Cannot create data menu");
    AppendMenuW(menu,MF_STRING,1,T(L"选择数据目录…"));
    AppendMenuW(menu,LauncherCatalogue()[selected].integrated?MF_STRING:MF_GRAYED,2,T(L"复制到合集…"));
    const auto rectangle=layout.data();const auto scale=float(GetDpiForWindow(hwnd))/96;
    POINT anchor{LONG(rectangle.left*scale),LONG(rectangle.top*scale)};ClientToScreen(hwnd,&anchor);
    const auto selection=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN|TPM_BOTTOMALIGN,anchor.x,anchor.y,0,hwnd,nullptr);
    DestroyMenu(menu);
    if(selection)Choose(selection==2);
  }
  std::optional<size_t> VisiblePosition() const {
    const auto found=std::ranges::find(library.visible,selected);
    if(found==library.visible.end())return {};
    return size_t(found-library.visible.begin());
  }
  std::optional<size_t> GameAt(float x,float y) const {
    if(y<layout.header() || y>=layout.footer())return {};
    for(size_t position=0;position<library.visible.size();++position)
      if(Inside(layout.card(position),x,y))return library.visible[position];
    return {};
  }
  ExternalHit ExternalControlAt(float x,float y) const {
    if(BigPicture())return {};
    const auto selected_project=SelectedProjectUrl();
    if(!selected_project.empty() && Inside(layout.project_link(selected_project),x,y))
      return {ExternalControl::ProjectLink,selected_project};
    if(y<layout.header() || y>=layout.footer())return {};
    for(size_t position=0;position<library.visible.size();++position) {
      const auto& game=LauncherCatalogue()[library.visible[position]];
      const auto project=RecompProject(game,sdoj_external);
      const bool toggle=game.id=="sdoj";
      if(project.empty() && !toggle)continue;
      const auto card=layout.card(position);
      if(!project.empty() && Inside(LayoutProjectIcon(card),x,y))return {ExternalControl::ProjectIcon,project};
      if(toggle && Inside(LayoutSdojSwitch(card),x,y))return {ExternalControl::SdojToggle};
    }
    return {};
  }
  std::optional<size_t> DropGameAt(float x,float y) const {
    if(library.visible.empty() || x<layout.margin() || x>=layout.width-layout.margin() ||
       y<layout.header() || y>=layout.footer())return {};
    const auto row=size_t(std::max(0.f,y-layout.header()+layout.scroll+layout.row_gap()/2)/
                          (layout.row_height()+layout.row_gap()));
    const auto column=std::min(layout.columns()-1,size_t(
        (x-layout.margin()+cave::launcher::kCoverColumnGap/2)/
        (layout.cover_width()+cave::launcher::kCoverColumnGap)));
    return library.visible[std::min(row*layout.columns()+column,library.visible.size()-1)];
  }
  std::optional<CardDrag> DragPreview() const {
    if(!card_press || !card_press->dragged)return {};
    const auto& press=*card_press;
    const auto scale=float(GetDpiForWindow(hwnd))/96;
    const auto x=press.current.x/scale,y=press.current.y/scale;
    const auto source=std::ranges::find(library.visible,press.game);
    if(source==library.visible.end())return {};
    const auto bounds=layout.card(size_t(source-library.visible.begin()));
    const auto left=x-press.grab.x,top=y-press.grab.y;
    CardDrag preview{press.game,{left,top,left+bounds.right-bounds.left,top+bounds.bottom-bounds.top},{}};
    if(const auto game=DropGameAt(x,y);game && *game!=press.game) {
      const auto target=std::ranges::find(library.visible,*game);
      const auto rectangle=layout.card(size_t(target-library.visible.begin()));
      const bool after=target>source;
      const auto edge=after?rectangle.right+6:rectangle.left-6;
      preview.insertion=D2D1_RECT_F{edge-2,rectangle.top+8,edge+2,rectangle.bottom-8};
    }
    return preview;
  }
  void CancelPointerPress() {
    if(!card_press && external_pressed.control==ExternalControl::None)return;
    ignore_card_release=true;card_press.reset();hover=-1;
    external_pressed={};
    if(GetCapture()==hwnd)ReleaseCapture();
    InvalidateGameList();
  }
  void RefreshLibraryView() {
    layout.game_count=library.visible.size();
    hover=-1;
    if(library.visible.empty())override_hint.clear();
    if(!VisiblePosition() && !library.visible.empty())Select(library.visible.front());
    else EnsureVisible();
    InvalidateRect(hwnd,nullptr,FALSE);
  }
  void SaveLibrary(cave::launcher::GameLibrary next,bool order_changed) {
    const auto& indexes=order_changed?next.order:next.hidden;
    const std::array edits{IniEdit{"collection",order_changed?"game_order":"hidden_games",
                                  cave::launcher::GameLibrary::Encode(indexes),indexes.empty()}};
    const auto before=Ini::ReadText(EnsureConfig(options.root));
    UpdateConfig(options.root,before,edits);
    preferences_text=Ini::ReadText(ConfigPath(options.root));
    library=std::move(next);
    RefreshLibraryView();
  }
  void MoveGame(size_t from,size_t to) {
    if(busy || child || from==to)return;
    auto next=library;
    next.Move(from,to);
    SaveLibrary(std::move(next),true);
  }
  void ToggleGameHidden(size_t index) {
    if(busy || child)return;
    auto next=library;
    next.SetHidden(index,!next.Hidden(index));
    SaveLibrary(std::move(next),false);
  }
  void ToggleHiddenGames() {
    if(busy || child)return;
    library.show_hidden=!library.show_hidden;
    library.Rebuild();
    RefreshLibraryView();
  }
  void GameContextMenu(POINT screen) {
    if(busy || child)return;
    std::optional<size_t> game;
    if(screen.x==-1 && screen.y==-1) {
      const auto position=VisiblePosition();
      if(!position)return;
      game=selected;
      const auto rectangle=layout.card(*position);
      const auto scale=float(GetDpiForWindow(hwnd))/96;
      screen={LONG(rectangle.left*scale),LONG(rectangle.top*scale)};
      ClientToScreen(hwnd,&screen);
    } else {
      auto client=screen;
      ScreenToClient(hwnd,&client);
      const auto scale=float(GetDpiForWindow(hwnd))/96;
      game=GameAt(client.x/scale,client.y/scale);
    }
    if(!game)return;
    const auto menu=CreatePopupMenu();
    if(!menu)throw std::runtime_error("Cannot create game menu");
    AppendMenuW(menu,MF_STRING|(LauncherCatalogue()[*game].coming_soon?MF_GRAYED:0),2,T(L"游戏数据详情"));
    AppendMenuW(menu,MF_STRING,1,T(library.Hidden(*game)?L"取消隐藏":L"隐藏游戏"));
    const auto action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,screen.x,screen.y,0,hwnd,nullptr);
    DestroyMenu(menu);
    if(action==1)ToggleGameHidden(*game);
    else if(action==2)ShowDetails(*game);
  }
  void EnsureVisible() {
    if(BigPicture()){InvalidateRect(hwnd,nullptr,FALSE);return;}
    if(const auto position=VisiblePosition()) {
      const auto top=float(*position/layout.columns())*(layout.row_height()+layout.row_gap()),bottom=top+layout.row_height();
      if(top<layout.scroll)layout.scroll=top;
      if(bottom>layout.scroll+layout.viewport_height())layout.scroll=bottom-layout.viewport_height();
    }
    Scroll(0);
  }
  void Scroll(float delta) {
    layout.scroll=std::clamp(layout.scroll+delta,0.f,layout.max_scroll());
    // Changing a standard scrollbar can recalculate the window frame. Run
    // after WM_SIZE has returned so it cannot interrupt that calculation.
    if(!scrollbar_pending)scrollbar_pending=PostMessageW(hwnd,kUpdateScrollBar,0,0)!=FALSE;
    InvalidateGameList();
  }
  void UpdateScrollBar() {
    scrollbar_pending=false;
    if(BigPicture() || IsIconic(hwnd))return;
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,int(std::ceil(layout.content_height()))-1),
                    UINT(std::ceil(layout.viewport_height())),int(layout.scroll),0};
    if(UINT(info.nMax+1)<=info.nPage && (GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_VSCROLL)) {
      auto wider=layout;
      const auto dpi=GetDpiForWindow(hwnd);
      wider.width+=GetSystemMetricsForDpi(SM_CXVSCROLL,dpi)*96.f/dpi;
      // Removing the bar can make the wider covers tall enough to need it again.
      if(std::ceil(wider.content_height())>info.nPage)info.fMask|=SIF_DISABLENOSCROLL;
    }
    SetScrollInfo(hwnd,SB_VERT,&info,TRUE);
  }
  void InvalidateGameList() {
    const auto scale=float(GetDpiForWindow(hwnd))/96;
    const RECT area{0,LONG(std::floor((layout.header()-2)*scale)),LONG(std::ceil(layout.width*scale)),
                    LONG(std::ceil(layout.footer()*scale))};
    InvalidateRect(hwnd,&area,FALSE);
  }
  void SetButtonHover(int slot) {
    if(slot==button_hover)return;
    const auto scale=float(GetDpiForWindow(hwnd))/96;
    for(int changed:{std::exchange(button_hover,slot),slot}) {
      if(changed<0)continue;
      const auto r=layout.focus_button(changed);
      const RECT area{LONG(std::floor(r.left*scale))-1,LONG(std::floor(r.top*scale))-1,
                      LONG(std::ceil(r.right*scale))+1,LONG(std::ceil(r.bottom*scale))+1};
      InvalidateRect(hwnd,&area,FALSE);
    }
  }
  void SetExternalHover(ExternalHit hit) {
    const auto previous=std::exchange(external_hover,hit);
    if(previous==hit)return;
    if(previous.control==ExternalControl::ProjectLink || hit.control==ExternalControl::ProjectLink)InvalidateRect(hwnd,nullptr,FALSE);
    else InvalidateGameList();
  }
  void Paint() {
    PAINTSTRUCT paint{};BeginPaint(hwnd,&paint);
    try {
      RECT rectangle{};GetClientRect(hwnd,&rectangle);
      // The launcher redraws only on UI events. A DC target avoids a persistent
      // GPU swap chain and also behaves correctly under capture/remote desktops.
      if (!surface) {
        const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE));
        Check(renderer.factory->CreateDCRenderTarget(&properties,&surface),"Create launcher surface");
      }
      Check(surface->BindDC(paint.hdc,&rectangle),"Bind launcher paint surface");
      const float dpi=float(GetDpiForWindow(hwnd));surface->SetDpi(dpi,dpi);
      const auto drag=DragPreview();
      surface->BeginDraw();
      if(BigPicture())renderer.DrawBigPicture(surface.Get(),BigView(),rows,library,message,busy,menu_open,menu_selection,sdoj_external);
      else renderer.Draw(surface.Get(),layout,rows,selected,hover,busy,message,active_game,false,focus,
          override_hint,&library,ProgramReady(),drag?&*drag:nullptr,button_hover,sdoj_external,external_hover);
      const auto status=surface->EndDraw();if(status==D2DERR_RECREATE_TARGET)surface.Reset();else Check(status,"Draw launcher");
      if (!paint_recorded) {
        Log(options.root,"first-paint width="+std::to_string(rectangle.right)+" height="+std::to_string(rectangle.bottom)+
            " dpi="+std::to_string(dpi)+" result="+std::to_string(status));
        paint_recorded=true;
      }
    }catch(const std::exception& error){EndPaint(hwnd,&paint);throw;}
    EndPaint(hwnd,&paint);
  }
  void Click(float x,float y) {
    focus=0;SetFocus(hwnd);
    InvalidateRect(hwnd,nullptr,FALSE);
    if(Inside(layout.big_picture_button(),x,y)){if(!busy&&!child)SetBigPicture(true);return;}
    if(Inside(layout.nickname(),x,y)) {
      if(IsWindowEnabled(nickname_control)){focus=2;SetFocus(nickname_control);}return;
    }
    if(Inside(layout.config(),x,y)) {OpenPath(hwnd,EnsureConfig(options.root));return;}
    if(Inside(layout.controls(),x,y)) {
      if(!busy && !child && !library.visible.empty() && !DonPachiSelected() && show_controls(hwnd,options.root,LauncherCatalogue()[selected],language)) {
        preferences_text=Ini::ReadText(ConfigPath(options.root));
        Log(options.root,"input-settings-updated");
      }
      return;
    }
    if(ProgramReady() && Inside(layout.update_badge(),x,y)){ShowUpdate();return;}
    if(Inside(layout.hidden_games(),x,y)){ToggleHiddenGames();return;}
    if(Inside(layout.logs(),x,y)) {if(!busy&&!child&&!GameIsRunning(options.root))show_reports(hwnd,options.root,language,{});return;}
    for(size_t index=0;index<3;++index)
      if(Inside(layout.language_choice(index),x,y)) {SelectLanguage(index);return;}
    if(Inside(layout.data(),x,y)) {DataMenu();return;}
    if(Inside(layout.file_upload(),x,y)) {
      if(!busy&&!child&&!library.visible.empty()&&!GameIsRunning(options.root))
        show_file_upload(hwnd,options.root,language,LauncherCatalogue()[selected].id);
      return;
    }
    if(Inside(layout.play(),x,y)) {LaunchSelected();return;}
    if(const auto game=GameAt(x,y))Select(*game);
  }
  static LRESULT CALLBACK Window(HWND window,UINT message,WPARAM first,LPARAM second) {
    App* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(second)->lpCreateParams);app->hwnd=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(window,message,first,second);
    try {
      if(message==cave::launcher::ActivationMessage()) {
        app->Activate();
        if(first)app->SetBigPicture(true);
        return 1;
      }
      switch(message) {
        case WM_CREATE:{
          SetWindowTextW(window,LauncherCaption().c_str());
          app->CreatePreferences();
          app->Refresh();
          app->StartUpdates();
          app->StartMenuController();
          return 0;
        }
        case WM_GETMINMAXINFO:{
          if(app->BigPicture())break;
          auto info=reinterpret_cast<MINMAXINFO*>(second);const auto scale=float(GetDpiForWindow(window))/96;
          info->ptMinTrackSize={LONG(720*scale),LONG(520*scale)};
          MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
          if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)) {
            info->ptMinTrackSize.x=std::min(info->ptMinTrackSize.x,(monitor.rcWork.right-monitor.rcWork.left)*9/10);
            info->ptMinTrackSize.y=std::min(info->ptMinTrackSize.y,(monitor.rcWork.bottom-monitor.rcWork.top)*9/10);
          }
          return 0;}
        case WM_SIZE:{app->SetButtonHover(-1);app->SetExternalHover({});if(first==SIZE_MINIMIZED)return 0;
          const auto scale=float(GetDpiForWindow(window))/96;app->layout.width=LOWORD(second)/scale;app->layout.height=HIWORD(second)/scale;
          if(!app->BigPicture() && first==SIZE_RESTORED && !IsZoomed(window) && LOWORD(second) && HIWORD(second)) {
            RECT bounds{};
            if(GetWindowRect(window,&bounds))app->normal_window_size=cave::launcher::WindowSize{
              int(std::lround((bounds.right-bounds.left)/scale)),int(std::lround((bounds.bottom-bounds.top)/scale))};
          }
          if(!app->BigPicture())app->PositionPreferences();
          app->EnsureVisible();return 0;}
        case WM_DPICHANGED:{
          if(app->BigPicture()){app->FitBigPicture();return 0;}
          auto bounds=*reinterpret_cast<const RECT*>(second);
          if(!IsZoomed(window) && !IsIconic(window))bounds=FitToWorkArea(bounds);
          SetWindowPos(window,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
                       SWP_NOACTIVATE|SWP_NOZORDER);
          return 0;}
        case WM_DISPLAYCHANGE:
          if(app->BigPicture())app->FitBigPicture();
          break;
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:app->Paint();return 0;
        case WM_LBUTTONDOWN:{
          if(app->BigPicture())return 0;
          const auto scale=float(GetDpiForWindow(window))/96;
          const POINT point{GET_X_LPARAM(second),GET_Y_LPARAM(second)};
          app->ignore_card_release=false;
          app->external_pressed=app->ExternalControlAt(point.x/scale,point.y/scale);
          if(app->external_pressed.control!=ExternalControl::None){SetCapture(window);return 0;}
          if(!app->busy && !app->child) {
            if(const auto game=app->GameAt(point.x/scale,point.y/scale)) {
              const auto position=std::ranges::find(app->library.visible,*game)-app->library.visible.begin();
              const auto bounds=app->layout.card(size_t(position));
              app->card_press=CardPress{*game,point,point,{point.x/scale-bounds.left,point.y/scale-bounds.top}};
              app->focus=0;SetFocus(window);
              SetCapture(window);
            }
          }
          return 0;}
        case WM_LBUTTONUP:{
          if(std::exchange(app->ignore_card_release,false))return 0;
          const auto scale=float(GetDpiForWindow(window))/96;
          const float x=GET_X_LPARAM(second)/scale,y=GET_Y_LPARAM(second)/scale;
          if(app->BigPicture()){app->BigPictureClick(x,y);return 0;}
          if(const auto hit=std::exchange(app->external_pressed,ExternalHit{});hit.control!=ExternalControl::None) {
            if(GetCapture()==window)ReleaseCapture();
            if(app->ExternalControlAt(x,y)==hit) {
              if(hit.control==ExternalControl::SdojToggle)app->ToggleSdoj();
              else OpenPath(window,hit.url);
            }
            return 0;
          }
          const auto press=std::exchange(app->card_press,std::nullopt);
          if(GetCapture()==window)ReleaseCapture();
          if(press && press->dragged) {
            app->InvalidateGameList();
            if(const auto target=app->DropGameAt(x,y))app->MoveGame(press->game,*target);
          } else app->Click(x,y);
          return 0;}
        case WM_CAPTURECHANGED:case WM_CANCELMODE:app->CancelPointerPress();return 0;
        case WM_ACTIVATEAPP:
          if(!first){app->CancelPointerPress();app->SetButtonHover(-1);app->SetExternalHover({});}break;
        case WM_CONTEXTMENU:
          if(app->BigPicture())return 0;
          if(reinterpret_cast<HWND>(first)==window) {
            app->GameContextMenu({GET_X_LPARAM(second),GET_Y_LPARAM(second)});
            return 0;
          }
          break;
        case WM_SETCURSOR:
          if(reinterpret_cast<HWND>(first)==window && LOWORD(second)==HTCLIENT && !app->card_press) {
            POINT point{};
            if(GetCursorPos(&point) && ScreenToClient(window,&point)) {
              const auto scale=float(GetDpiForWindow(window))/96;
              const auto hit=app->ExternalControlAt(point.x/scale,point.y/scale);
              if(!hit.url.empty()) {
                SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;
              }
            }
          }
          break;
        case WM_MOUSEMOVE:{
          if(app->BigPicture())return 0;
          const auto scale=float(GetDpiForWindow(window))/96;const float x=GET_X_LPARAM(second)/scale,y=GET_Y_LPARAM(second)/scale;
          if(!app->tracking_mouse_leave) {
            TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0};
            app->tracking_mouse_leave=TrackMouseEvent(&tracking)!=FALSE;
          }
          if(auto& press=app->card_press;press) {
            press->current={GET_X_LPARAM(second),GET_Y_LPARAM(second)};
            const auto dpi=GetDpiForWindow(window);
            press->dragged=press->dragged || std::abs(GET_X_LPARAM(second)-press->origin.x)>GetSystemMetricsForDpi(SM_CXDRAG,dpi) ||
                           std::abs(GET_Y_LPARAM(second)-press->origin.y)>GetSystemMetricsForDpi(SM_CYDRAG,dpi);
            if(press->dragged) {
              app->SetButtonHover(-1);
              app->SetExternalHover({});
              app->hover=-1;app->InvalidateGameList();
              SetCursor(LoadCursorW(nullptr,app->DropGameAt(x,y)?IDC_SIZEALL:IDC_NO));
              return 0;
            }
          }
          app->SetButtonHover(app->layout.footer_button_at(x,y));
          const auto hit=app->ExternalControlAt(x,y);
          app->SetExternalHover(hit.control==ExternalControl::SdojToggle && (app->busy||app->child)?ExternalHit{}:hit);
          const auto game=app->GameAt(x,y);
          const auto hover=game?int(*game):-1;
          if(hover!=app->hover){app->hover=hover;app->InvalidateGameList();}return 0;}
        case WM_MOUSELEAVE:
          app->tracking_mouse_leave=false;
          app->SetButtonHover(-1);
          app->SetExternalHover({});
          if(app->hover!=-1){app->hover=-1;app->InvalidateGameList();}
          return 0;
        case WM_MOUSEWHEEL:
          if(app->BigPicture()){
            const bool down=GET_WHEEL_DELTA_WPARAM(first)<0;
            const bool vertical=app->menu_open || app->BigView().Portrait();
            app->BigPictureAction(vertical?(down?MenuKey::Down:MenuKey::Up):(down?MenuKey::Right:MenuKey::Left));
            return 0;
          }
          app->Scroll(-float(GET_WHEEL_DELTA_WPARAM(first))/WHEEL_DELTA*90);return 0;
        case kUpdateScrollBar:app->UpdateScrollBar();return 0;
        case WM_VSCROLL:{SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_ALL;GetScrollInfo(window,SB_VERT,&info);float value=app->layout.scroll;
          switch(LOWORD(first)){case SB_THUMBTRACK:value=float(info.nTrackPos);break;case SB_LINEUP:value-=30;break;case SB_LINEDOWN:value+=30;break;case SB_PAGEUP:value-=app->layout.viewport_height();break;case SB_PAGEDOWN:value+=app->layout.viewport_height();break;}app->Scroll(value-app->layout.scroll);return 0;}
        case WM_KEYDOWN:{
          if(app->BigPicture()){app->BigPictureKeyboard(first,(second&(1L<<30))!=0);return 0;}
          if(first==VK_F11 && !(second&(1L<<30))){app->SetBigPicture(true);return 0;}
          if(app->card_press || app->external_pressed.control!=ExternalControl::None) {if(first==VK_ESCAPE)app->CancelPointerPress();return 0;}
          const auto& visible=app->library.visible;
          if(first==VK_UP || first==VK_NUMPAD8)app->MoveDesktopSelection(MenuKey::Up);
          else if(first==VK_DOWN || first==VK_NUMPAD2)app->MoveDesktopSelection(MenuKey::Down);
          else if(first==VK_LEFT)app->MoveDesktopSelection(MenuKey::Left);
          else if(first==VK_RIGHT)app->MoveDesktopSelection(MenuKey::Right);
          else if(first==VK_HOME && !visible.empty())app->Select(visible.front());
          else if(first==VK_END && !visible.empty())app->Select(visible.back());
          else if(first==VK_APPS || (first==VK_F10 && (GetKeyState(VK_SHIFT)&0x8000)))app->GameContextMenu({-1,-1});
          else if(first==VK_RETURN || first==VK_SPACE) {
            if(app->focus==kProjectLinkFocus) {
              if(const auto project=app->SelectedProjectUrl();!project.empty())OpenPath(window,project);
            } else if(app->focus>=4) {
              if(app->focus>=10 && app->focus<=12) {app->SelectLanguage(size_t(app->focus-10));return 0;}
              const auto rectangle=app->layout.focus_button(app->focus);
              app->Click((rectangle.left+rectangle.right)/2,(rectangle.top+rectangle.bottom)/2);
            } else app->LaunchSelected();
          }
          else if(first==VK_F5)app->ManualRefresh();
          else if(first==VK_PRIOR)app->Scroll(-app->layout.viewport_height());
          else if(first==VK_NEXT)app->Scroll(app->layout.viewport_height());
          return 0;}
        case WM_COMMAND:
          if(LOWORD(first)==kFullscreenControl) {
            const auto notification=HIWORD(first);
            if(notification==CBN_DROPDOWN) {
              // The native combo positions the selection after this notification.
              PostMessageW(window,kShowFirstDisplayChoice,0,0);
              return 0;
            }
            if(notification==CBN_SELENDOK ||
               (notification==CBN_SELCHANGE && !SendMessageW(app->fullscreen_control,CB_GETDROPPEDSTATE,0,0))) {
              app->DisplayChanged();
              return 0;
            }
          }
          if(LOWORD(first)==kSaveControl && HIWORD(first)==BN_CLICKED) {if(app->SavePreferences())app->Refresh();return 0;}
          if(LOWORD(first)==kNicknameControl && HIWORD(first)==EN_CHANGE) {
            if(!app->loading_preferences){app->preferences_dirty=true;app->EnablePreferences();InvalidateRect(window,nullptr,FALSE);}return 0;
          }
          if((LOWORD(first)==kNicknameControl && HIWORD(first)==EN_SETFOCUS) ||
             (LOWORD(first)==kFullscreenControl && HIWORD(first)==CBN_SETFOCUS) || HIWORD(first)==BN_SETFOCUS) {
            app->focus=LOWORD(first)==kFullscreenControl?1:(LOWORD(first)==kNicknameControl?2:3);InvalidateRect(window,nullptr,FALSE);
          }break;
        case kShowFirstDisplayChoice:
          if(SendMessageW(app->fullscreen_control,CB_GETDROPPEDSTATE,0,0))
            SendMessageW(app->fullscreen_control,CB_SETTOPINDEX,0,0);
          return 0;
        case WM_CTLCOLORLISTBOX:
          SetBkColor(reinterpret_cast<HDC>(first),RGB(255,255,255));SetTextColor(reinterpret_cast<HDC>(first),RGB(35,49,45));
          return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        case WM_CTLCOLORBTN:case WM_CTLCOLORSTATIC:
          SetBkColor(reinterpret_cast<HDC>(first),RGB(243,244,240));SetTextColor(reinterpret_cast<HDC>(first),RGB(73,90,80));
          return reinterpret_cast<LRESULT>(app->form_background);
        case kWorkDone:app->Complete();return 0;
        case kCopyProgress:
          if(app->work && app->work->copying && !app->close_requested) {
            const auto total=app->work->total.load(),copied=app->work->copied.load();
            app->message=std::wstring(T(L"正在复制并校验数据… "))+
                std::to_wstring(total?uint64_t(double(copied)/double(total)*100):0)+L"%";
            InvalidateRect(window,nullptr,FALSE);
          }return 0;
        case WM_INPUT:
          app->RawGameExitInput(reinterpret_cast<HRAWINPUT>(second));
          return DefWindowProcW(window,message,first,second);
        case WM_TIMER:
          if(first==kUpdateTimer)app->UpdateTick();
          else if(first==kChildTimer)app->ChildTick();
          else if(first==kMenuControllerTimer)app->MenuControllerTick();
          return 0;
        case WM_CLOSE:
          if(app->work && app->work->copying) {app->close_requested=true;app->work->cancelled=true;app->message=T(L"正在取消复制，请稍候…");InvalidateRect(window,nullptr,FALSE);return 0;}
          if(app->child){ShowWindow(window,SW_HIDE);return 0;}DestroyWindow(window);return 0;
        case WM_DESTROY:if(app->work)app->work->alive=false;app->PersistSelection();
          if(app->form_font)DeleteObject(app->form_font);if(app->form_background)DeleteObject(app->form_background);
          PostQuitMessage(0);return 0;
      }
    }catch(const std::exception& error){Log(app->options.root,error.what());Error(window,error.what());return 0;}
    return DefWindowProcW(window,message,first,second);
  }
  Options options;
  HWND hwnd{};
  std::optional<WINDOWPLACEMENT> desktop_window;
  cave::launcher::BigPictureMode big_picture_mode{};
  LONG_PTR desktop_style{};
  bool game_focus_pending{};
  std::unique_ptr<cave::launcher::MenuController> menu_controller;
  std::optional<GameExitShortcuts> game_exit_shortcuts;
  HWND game_exit_window{};
  bool game_exit_keyboard{};
  std::array<bool,2> exit_keys_down{};
  ExitKeys exit_keys_awaiting_release{};
  bool menu_open{};
  size_t menu_selection{};
  HWND fullscreen_control{},nickname_control{},save_control{};
  std::vector<DisplaySelection> display_choices;
  HFONT form_font{};
  HBRUSH form_background{};
  bool preferences_available{},preferences_dirty{},loading_preferences{};
  std::string preferences_text;
  std::wstring override_hint;
  Renderer renderer;
  Layout layout;
  cave::launcher::GameLibrary library;
  struct CardPress { size_t game; POINT origin,current; D2D1_POINT_2F grab; bool dragged{}; };
  std::optional<CardPress> card_press;
  bool ignore_card_release{};
  bool sdoj_external{};
  ExternalHit external_hover{},external_pressed{};
  std::optional<cave::launcher::WindowSize> saved_window_size,normal_window_size;
  bool scrollbar_pending{};
  std::vector<Row> rows;
  size_t selected{};
  int hover{-1};
  int button_hover{-1};
  bool tracking_mouse_leave{};
  int focus{};
  bool busy{};
  bool paint_recorded{};
  bool close_requested{};
  std::wstring message=T(L"正在检测游戏数据…");
  ComPtr<ID2D1DCRenderTarget> surface;
  std::shared_ptr<Work> work;
  std::thread worker;
  std::optional<ChildProcess> child;
  std::optional<GameSession> external_session;
  std::string active_game;
  update::State update_state;
  std::function<update::Result()> update_fetch=update::Fetch;
  decltype(&cave::launcher::reports::Show) show_reports=cave::launcher::reports::Show;
  decltype(&cave::launcher::reports::ShowFile) show_file_upload=cave::launcher::reports::ShowFile;
  decltype(&cave::launcher::controls::Show) show_controls=cave::launcher::controls::Show;
  std::optional<update::Request> update_request,program_download;
  std::string download_failed_tag;
  std::optional<update::Release> ready_program;
  std::chrono::steady_clock::time_point program_retry{};
  decltype(&update::DownloadProgram) download_program=update::DownloadProgram;
  decltype(&update::StartProgramWorker) start_update_worker=update::StartProgramWorker;
  bool update_auto{},update_dialog{},packaged_update{};
};
void Preview(const Options& options) {
  if(fs::exists(options.preview))throw std::runtime_error("Preserve the existing launcher preview");
  const auto ini=Ini::Load(EnsureConfig(options.root));
  language=options.language.value_or(cave::launcher::ReadLanguage(ini));
  const auto sdoj_external=cave::launcher::ReadSdojRecomp(ini);
  const auto big_picture_mode=cave::launcher::ReadBigPictureMode(ini);
  const bool portrait=options.big_picture && big_picture_mode==cave::launcher::BigPictureMode::Vertical;
  const UINT width=portrait?720:1080, height=portrait?1280:900;
  Renderer renderer;
  ComPtr<IWICBitmap> bitmap;Check(renderer.wic->CreateBitmap(width,height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap),"Create preview bitmap");
  ComPtr<ID2D1RenderTarget> target;Check(renderer.factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(),&target),"Create preview surface");
  size_t selected=1;
  if(!options.selection.empty())for(size_t index=0;index<LauncherCatalogue().size();++index)
    if(LauncherCatalogue()[index].id==options.selection)selected=index;
  auto rows=InspectRows(options.root,selected,false);
  if(options.selection.empty()) {
    rows[0].inspection.state="missing_data";rows[1].inspection.state="ready";rows[2].inspection.state="not_integrated";
    rows[0].inspection.data_state=DataState::Missing;rows[1].inspection.data_state=DataState::Verified;
  }
  Layout layout{1080,900,0};
  const auto caption=std::to_wstring(LauncherCatalogue().size())+T(L" 部作品 · 按需安装游戏数据");
  const cave::launcher::GameLibrary library(Ini::Parse(""));
  const auto position=std::size_t(std::ranges::find(library.visible,selected)-library.visible.begin());
  layout.scroll=std::clamp(layout.card(position).bottom-layout.footer(),0.f,layout.max_scroll());
  target->BeginDraw();
  if(options.big_picture)renderer.DrawBigPicture(target.Get(),{float(width),float(height),library.visible.size(),position,big_picture_mode},
      rows,library,{},false,false,0,sdoj_external);
  else renderer.Draw(target.Get(),layout,rows,selected,-1,false,caption,{},true,0,{},&library,
      false,nullptr,-1,sdoj_external);
  Check(target->EndDraw(),"Render preview");
  ComPtr<IWICStream> stream;Check(renderer.wic->CreateStream(&stream),"Create preview stream");
  if(!options.preview.parent_path().empty())fs::create_directories(options.preview.parent_path());
  Check(stream->InitializeFromFilename(options.preview.c_str(),GENERIC_WRITE),"Open preview file");
  ComPtr<IWICBitmapEncoder> encoder;Check(renderer.wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"Create preview encoder");
  Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Initialize preview encoder");
  ComPtr<IWICBitmapFrameEncode> frame;Check(encoder->CreateNewFrame(&frame,nullptr),"Create preview frame");Check(frame->Initialize(nullptr),"Initialize preview frame");
  Check(frame->SetSize(width,height),"Set preview size");WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;Check(frame->SetPixelFormat(&format),"Set preview format");
  Check(frame->WriteSource(bitmap.Get(),nullptr),"Write preview");Check(frame->Commit(),"Commit preview frame");Check(encoder->Commit(),"Commit preview");
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
  bool render_preview=false;
  try {
    Check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Initialize launcher COM");
    struct Apartment {~Apartment(){CoUninitialize();}} apartment;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto options=Arguments();
    render_preview=!options.preview.empty();
    if(render_preview){Preview(options);return 0;}
    if(!options.update_action.empty()) {
      try {
        language=cave::launcher::ReadLanguage(Ini::Load(ConfigPath(options.root)));
      }catch(...){language=Language::English;}
      try {return update::RunProgramWorker(options.root,options.update_action,options.update_parent);}
      catch(const std::exception& error) {
        Log(options.root,"program-update-install: "+std::string(error.what()));
        MessageBoxW(nullptr,T(L"更新未完成。请关闭正在使用合集文件的程序，然后重新打开启动器。"),
                    kWindowTitle,MB_OK|MB_ICONWARNING);
        return 2;
      }
    }
    const auto recovery=update::RecoveryPending(options.root);
    if(!options.game.empty()) {
      if(recovery)throw std::runtime_error("Open the launcher to finish update recovery.");
      EnsureConfig(options.root);
      const auto ini=Ini::Load(ConfigPath(options.root));
      const bool arcade=options.game==cave::launcher::kDonPachi.id;
      const auto spec=arcade?cave::launcher::PrepareDonPachi(options.root,ini,options.profile,options.data):
                             PrepareLaunch(FindGame(options.game),options.root,ini,options.profile,options.data);
      std::optional<GameSession> session;
      if(arcade) {
        session.emplace(options.root);
        cave::launcher::SetDonPachiFullscreen(spec,ResolveSharedSettings(ini).fullscreen);
      }
      auto child=StartGame(spec,arcade?spec.executable.parent_path():fs::path{});
      child.Wait();const auto code=child.ExitCode().value_or(1);
      if(!arcade)cave::launcher::reports::RecordProcessExit(options.root,spec.game,child.pid(),child.CreationTime(),code);
      return int(code);
    }
    cave::launcher::LauncherInstance instance_guard(options.root);
    if(!instance_guard.ActivateOrAcquire(options.big_picture))return 0;
    if(recovery){update::StartProgramWorker(options.root,L"recover");return 0;}
    App app(std::move(options));
    WNDCLASSEXW definition{};definition.cbSize=sizeof(definition);definition.style=CS_HREDRAW|CS_VREDRAW;definition.lpfnWndProc=App::Window;definition.hInstance=instance;
    definition.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    definition.hIcon=BrandIcon(instance,GetSystemMetrics(SM_CXICON));
    definition.hIconSm=BrandIcon(instance,GetSystemMetrics(SM_CXSMICON));definition.lpszClassName=kWindowClass;
    if(!RegisterClassExW(&definition))throw std::runtime_error("Cannot register launcher window");
    RECT rectangle{0,0,1080,900};AdjustWindowRectEx(&rectangle,WS_OVERLAPPEDWINDOW|WS_VSCROLL,FALSE,0);
    const auto window=CreateWindowExW(0,kWindowClass,kWindowTitle,WS_OVERLAPPEDWINDOW|WS_VSCROLL|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,
        rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,nullptr,nullptr,instance,&app);
    if(!window)throw std::runtime_error("Cannot create launcher window");
    instance_guard.Publish(window);
    FitInitialWindow(window,app.saved_window_size,app.layout.game_count);
    if(app.options.big_picture)app.SetBigPicture(true);
    ShowWindow(window,app.BigPicture()?SW_SHOWNORMAL:show);UpdateWindow(window);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(app.Input(message))continue;TranslateMessage(&message);DispatchMessageW(&message);}
    return 0;
  }catch(const std::exception& error){
    if(render_preview)std::fprintf(stderr,"Launcher preview: %s\n",error.what());
    else Error(nullptr,error.what());
    return 2;
  }
}
