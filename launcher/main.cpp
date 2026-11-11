#include "common/catalogue.h"
#include "common/verification_cache.h"
#include "common/ini.h"
#include "common/launch.h"
#include "common/data_import.h"
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
#include "common/config_update.h"
#include <Windows.h>
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
#include <optional>
#include <string>
#include <stdexcept>
#include <thread>
#include <utility>

using Microsoft::WRL::ComPtr;
using namespace cave;
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
namespace {
constexpr UINT kWorkDone=WM_APP+1;
constexpr UINT kCopyProgress=WM_APP+2;
constexpr UINT kUpdateScrollBar=WM_APP+3;
constexpr UINT_PTR kChildTimer=1;
constexpr UINT_PTR kUpdateTimer=2;
constexpr int kFullscreenControl=101,kNicknameControl=102,kSaveControl=103;
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
Language language=Language::Chinese;
const wchar_t* T(std::wstring_view key) {return cave::launcher::Translate(language,key);}
std::wstring DisplayLabel(DisplaySelection display) {
  return display.fullscreen?T(L"全屏"):std::to_wstring(display.width)+L" × "+std::to_wstring(display.height);
}
void Check(HRESULT status,const char* operation) {
  if (FAILED(status)) throw std::runtime_error(std::string(operation)+" (HRESULT "+std::to_string(static_cast<unsigned>(status))+")");
}
std::wstring Wide(std::string_view text) {return FromUtf8(text).native();}
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
fs::path LogDirectory(const fs::path& root) {
  const auto directory=DataChild(root,"logs");
  const auto ini=Ini::Load(ConfigPath(root));
  for (const auto& game:Catalogue()) {
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
struct Options {
  fs::path root,data,preview;
  std::string game,profile="default",selection;
  std::optional<Language> language;
  std::wstring update_action;
  unsigned long update_parent{};
};
Options Arguments() {
  Options options;options.root=DefaultRoot();
  int count{};auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);
  if (!arguments) throw std::runtime_error("Cannot parse launcher arguments");
  try {
    for (int index=1;index<count;++index) {
      const std::wstring_view key=arguments[index];
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
  if (!options.data.empty() && options.game.empty()) throw std::runtime_error("--data-dir requires --game");
  if (!options.game.empty()) (void)FindGame(options.game);
  if (!options.selection.empty()) (void)FindGame(options.selection);
  if (!options.game.empty() && !options.selection.empty())throw std::runtime_error("Use --game to launch directly, or --select to open the launcher");
  return options;
}
struct Row {Inspection inspection;fs::path content_source;std::vector<ContentInspection> content;};
std::vector<Row> InspectRows(const fs::path& root,size_t selected) {
  const auto started=std::chrono::steady_clock::now();
  VerificationCache cache(root);
  std::vector<Row> rows(Catalogue().size());
  for(size_t index=0;index<rows.size();++index) {
    rows[index].inspection.game=Catalogue()[index].id;
    rows[index].inspection.state=Catalogue()[index].coming_soon?"coming_soon":"checking";
  }
  try {
    const auto ini=Ini::Load(ConfigPath(root));
    // Inspect once on the existing worker, selected title first. Every card
    // needs a real base-data result; repaint/scroll never touch the filesystem.
    for(size_t step=0;step<rows.size();++step) {
      const auto index=step==0?selected:(step<=selected?step-1:step);
      const auto& game=Catalogue()[index];
      try {
        if (game.integrated) (void)ResolveSettings(ini,game.id);
        rows[index].inspection=Inspect(game,root,ini.DataDirectory(game),CheckLevel::Quick,&cache);
        if(!rows[index].inspection.missing_runtime.empty()) {
          try {cave::launcher::reports::RecordRuntimeFailure(root,game.id);} catch (...) {}
        }
        const auto content=ini.ContentDirectory(game,rows[index].inspection.data_dir);
        if(index==selected && !game.content.empty()) {
          rows[index].content_source=content.empty()?fs::path{}:Resolve(root,content);
          rows[index].content=InspectContent(game,rows[index].content_source,CheckLevel::Quick);
        }
      } catch (const std::exception& error) {
        rows[index].inspection.game=game.id;rows[index].inspection.state="invalid_config";rows[index].inspection.reason=error.what();
      }
    }
  } catch (const std::exception& error) {
    for (size_t index=0;index<rows.size();++index) {
      auto& result=rows[index].inspection;result.game=Catalogue()[index].id;
      result.state=Catalogue()[index].coming_soon?"coming_soon":"invalid_config";result.reason=error.what();
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
  if (state=="missing_runtime") return T(L"运行组件不可用 · 点击详情");
  if (state=="invalid_config") return T(L"配置有误 · 请检查 config.ini");
  if (state=="invalid_path") return T(L"数据路径不可用");
  if (state=="not_integrated") return T(L"正在接入");
  if (state=="coming_soon") return T(L"敬请期待");
  return T(L"正在检测…");
}
bool CanPlay(const Inspection& inspection) {return inspection.state=="ready" || inspection.state=="needs_verification";}
std::string Details(const Inspection& inspection) {
  auto result=inspection.reason.empty()?inspection.state:inspection.reason;
  if(!inspection.version.empty())result+="\n"+inspection.game+" "+inspection.version+" ("+inspection.layout+")";
  if (!inspection.problems.empty()) result+="\n"+inspection.problems.front();
  if (!inspection.missing_runtime.empty()) result+="\n"+inspection.missing_runtime.front();
  return result;
}
struct Layout {
  float width{},height{},scroll{};
  bool covers=false;
  size_t game_count=Catalogue().size();
  bool narrow() const {return width<800;}
  float margin() const {return covers?cave::launcher::CoverMargin(width):std::max(narrow()?21.f:32.f,(width-1120.f)/2);}
  float header() const {return covers?cave::launcher::kCoverHeaderHeight:cave::launcher::kHeaderHeight;}
  float footer() const {return height-cave::launcher::kFooterHeight;}
  float row_height() const {
    if(covers)return cave::launcher::CoverHeight(cover_width());
    const auto count=float(std::max(size_t{1},game_count));
    return std::clamp((viewport_height()-cave::launcher::kCardGap*(count-1))/count,
                      float(cave::launcher::kMinimumCardHeight),180.f);
  }
  float viewport_height() const {return std::max(0.f,footer()-header());}
  size_t columns() const {return covers?cave::launcher::CoverColumns(width):1;}
  float cover_width() const {return cave::launcher::CoverWidth(width);}
  float row_gap() const {return covers?cave::launcher::kCoverRowGap:cave::launcher::kCardGap;}
  float content_height() const {
    return game_count?float((game_count+columns()-1)/columns())*(row_height()+row_gap())-row_gap():0.f;
  }
  float max_scroll() const {return std::max(0.f,content_height()-viewport_height());}
  D2D1_RECT_F card(size_t index) const {
    const auto top=header()+float(index/columns())*(row_height()+row_gap())-scroll;
    const auto left=margin()+(covers?float(index%columns())*(cover_width()+cave::launcher::kCoverColumnGap):0);
    return {left,top,covers?left+cover_width():width-margin(),top+row_height()};
  }
  D2D1_RECT_F play() const {return {width-margin()-176,height-64,width-margin(),height-14};}
  D2D1_RECT_F controls() const {return {margin()+158,height-45,margin()+235,height-14};}
  D2D1_RECT_F config() const {return {margin()+242,height-45,margin()+319,height-14};}
  D2D1_RECT_F logs() const {return {margin()+326,height-45,margin()+415,height-14};}
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
  D2D1_RECT_F data() const {return {margin(),height-45,margin()+85,height-14};}
  D2D1_RECT_F refresh() const {return {margin()+92,height-45,margin()+151,height-14};}
  D2D1_RECT_F detail() const {return {margin()+422,height-45,margin()+479,height-14};}
  D2D1_RECT_F focus_button(int slot) const {
    switch(slot) {
    case 4:return config(); case 5:return logs(); case 6:return data();
    case 7:return refresh(); case 8:return play(); case 13:return controls(); case 14:return hidden_games(); case 15:return update_badge();
    default:return detail();
    }
  }
  D2D1_RECT_F fullscreen() const {const auto r=play();return {r.left,r.top-33,r.right,r.top-5};}
  D2D1_RECT_F nickname() const {const auto right=width-margin();return {right-263,66,right-73,96};}
  D2D1_RECT_F nickname_label() const {const auto r=nickname();return {r.left-80,r.top+7,r.left-6,r.bottom};}
  D2D1_RECT_F save() const {const auto right=width-margin();return {right-62,66,right,96};}
};
bool Inside(D2D1_RECT_F r,float x,float y) {return x>=r.left && x<r.right && y>=r.top && y<r.bottom;}
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
void FitInitialWindow(HWND window,bool covers,std::optional<cave::launcher::WindowSize> saved={}) {
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
  RECT current{};GetWindowRect(window,&current);
  const auto& work=monitor.rcWork;
  auto size=saved?cave::launcher::RestoreWindowSize(*saved,work.right-work.left,
      work.bottom-work.top,dpi):
      covers?cave::launcher::InitialCoverWindowSize(work.right-work.left,
      work.bottom-work.top,dpi,frame.right-frame.left,frame.bottom-frame.top,Catalogue().size()):
      cave::launcher::InitialWindowSize(current.right-current.left,
      work.right-work.left,work.bottom-work.top,dpi,frame.bottom-frame.top,Catalogue().size());
  if(covers && !saved) {
    const auto scale=float(dpi)/96;
    Layout planned{(size.width-frame.right+frame.left)/scale,(size.height-frame.bottom+frame.top)/scale};
    planned.covers=true;
    // A fitting library hides the native scrollbar. Keep its resulting client width
    // equal to the width used to calculate the card heights.
    if(planned.max_scroll()==0)size.width-=scrollbar;
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
            DWRITE_TEXT_ALIGNMENT align=DWRITE_TEXT_ALIGNMENT_LEADING,bool wrap=false) {
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
    target->DrawText(value.data(),UINT32(value.size()),format.Get(),rectangle,brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
  }
  void Button(ID2D1RenderTarget* target,D2D1_RECT_F rectangle,std::wstring_view text,bool play=false,bool disabled=false) {
    play=play&&!disabled;
    const auto background=play?0x2f5b42:(disabled?0xe7ebe5:0xffffff);
    ComPtr<ID2D1SolidColorBrush> fill,border;
    Check(target->CreateSolidColorBrush(D2D1::ColorF(background),&fill),"Create button fill");
    Check(target->CreateSolidColorBrush(D2D1::ColorF(play?0x2f5b42:0xd9dfd6),&border),"Create button border");
    const auto rounded=D2D1::RoundedRect(rectangle,8,8);target->FillRoundedRectangle(rounded,fill.Get());target->DrawRoundedRectangle(rounded,border.Get());
    Text(target,text,{rectangle.left+6,rectangle.top+(rectangle.bottom-rectangle.top-16)/2,rectangle.right-6,rectangle.bottom},12,play?0xffffff:(disabled?0x78857b:0x495a50),DWRITE_TEXT_ALIGNMENT_CENTER);
  }
  ID2D1Bitmap* Image(ID2D1RenderTarget* target,size_t index,bool grey=false,bool cover=false) {
    SelectTarget(target);
    auto& bitmap=cover?(grey?grey_covers[index]:covers[index]):(grey?grey_bitmaps[index]:bitmaps[index]);
    const auto& artwork=cover?cave::launcher::kCoverArtwork[index]:cave::launcher::kArtwork[index];
    if(!bitmap)bitmap=DecodeImage(target,artwork.resource,grey);
    return bitmap.Get();
  }
  ID2D1Bitmap* Logo(ID2D1RenderTarget* target) {
    SelectTarget(target);
    if(!logo)logo=DecodeImage(target,cave::launcher::kBrandLogoResource);
    return logo.Get();
  }
  void SelectTarget(ID2D1RenderTarget* target) {
    if(last_target!=target) {
      for(auto& bitmap:bitmaps)bitmap.Reset();
      for(auto& bitmap:grey_bitmaps)bitmap.Reset();
      for(auto& bitmap:covers)bitmap.Reset();
      for(auto& bitmap:grey_covers)bitmap.Reset();
      logo.Reset();last_target=target;
    }
  }
  ComPtr<ID2D1Bitmap> DecodeImage(ID2D1RenderTarget* target,unsigned identity,bool grey=false) {
    const auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(identity),RT_RCDATA);
    if (!resource) throw std::runtime_error("Missing embedded banner resource");
    const auto bytes=SizeofResource(nullptr,resource);auto data=LockResource(LoadResource(nullptr,resource));
    if (!bytes || bytes>5*1024*1024 || !data) throw std::runtime_error("Invalid embedded banner");
    ComPtr<IWICStream> stream;Check(wic->CreateStream(&stream),"Create artwork stream");
    Check(stream->InitializeFromMemory(static_cast<BYTE*>(data),bytes),"Read embedded artwork");
    ComPtr<IWICBitmapDecoder> decoder;Check(wic->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder),"Decode banner");
    ComPtr<IWICBitmapFrameDecode> frame;Check(decoder->GetFrame(0,&frame),"Read banner frame");
    UINT width{},height{};Check(frame->GetSize(&width,&height),"Read banner bounds");
    if (!width || !height || width>8192 || height>8192) throw std::runtime_error("Banner dimensions exceed bounds");
    ComPtr<IWICFormatConverter> converter;Check(wic->CreateFormatConverter(&converter),"Create artwork converter");
    Check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Convert banner");
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
  void Draw(ID2D1RenderTarget* target,const Layout& layout,const std::vector<Row>& rows,size_t selected,int hover,bool busy,
            std::wstring_view message,std::string_view running_game={},bool preview=false,int focus=0,
            std::wstring_view override_hint={},const cave::launcher::GameLibrary* library=nullptr,bool update_ready=false) {
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
      const auto& game=Catalogue()[index];const bool soon=game.coming_soon;
      const bool missing=cave::launcher::MissingGameData(rows[index].inspection);
      const bool hidden=library&&library->Hidden(index);
      const bool muted=soon||missing||hidden;
      const auto color=muted?0xf6f7f4:0xffffff;
      ComPtr<ID2D1SolidColorBrush> background,border;
      Check(target->CreateSolidColorBrush(D2D1::ColorF(color),&background),"Create card fill");
      Check(target->CreateSolidColorBrush(D2D1::ColorF(selected==index?0x37644c:(hover==int(index)?0xa0b1a3:0xdee3db)),&border),"Create card border");
      auto round=D2D1::RoundedRect(rectangle,13,13);target->FillRoundedRectangle(round,background.Get());
      ComPtr<ID2D1RoundedRectangleGeometry> clip;Check(factory->CreateRoundedRectangleGeometry(round,&clip),"Create card clip");
      target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),clip.Get()),nullptr);
      if(layout.covers) {
        const auto inset=cave::launcher::kCoverInset;
        const D2D1_RECT_F art{rectangle.left+inset,rectangle.top+inset,rectangle.right-inset,rectangle.bottom-74};
        auto bitmap=Image(target,index,muted,true);const auto size=bitmap->GetSize();
        const auto& artwork=cave::launcher::kCoverArtwork[index];
        const float scale=artwork.contain?std::min((art.right-art.left)/size.width,(art.bottom-art.top)/size.height):
                                          std::max((art.right-art.left)/size.width,(art.bottom-art.top)/size.height);
        const float x=art.left-((size.width*scale)-(art.right-art.left))*.5f;
        const float y=art.top-((size.height*scale)-(art.bottom-art.top))*artwork.focus_y;
        target->PushAxisAlignedClip(art,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        target->DrawBitmap(bitmap,{x,y,x+size.width*scale,y+size.height*scale},
            soon?.26f:(muted?.55f:1.f),D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        target->PopAxisAlignedClip();
        if(soon || hidden) {
          ComPtr<ID2D1SolidColorBrush> shade;
          Check(target->CreateSolidColorBrush(D2D1::ColorF(0xf3f4f0,.87f),&shade),"Create future-cover label");
          const D2D1_RECT_F label{art.left,art.bottom-29,art.right,art.bottom};
          target->FillRectangle(label,shade.Get());
          Text(target,T(hidden?L"已隐藏":L"敬请期待"),{label.left+10,label.top+6,label.right-10,label.bottom},11,0x647168);
        }
        const auto left=rectangle.left+12;
        Text(target,Wide(game.DisplayName()),{left,rectangle.bottom-64,rectangle.right-12,rectangle.bottom-27},
            13,muted?0x647168:(selected==index?0x2f5b42:0x23312d),DWRITE_TEXT_ALIGNMENT_LEADING,true);
        Text(target,Wide(game.name),{left,rectangle.bottom-24,rectangle.right-12,rectangle.bottom-10},10,0x748179);
        target->PopLayer();
        target->DrawRoundedRectangle(round,border.Get(),selected==index?2.5f:1.f);
        continue;
      }
      const float art_width=(rectangle.right-rectangle.left)*(layout.narrow()?.61f:.66f);
      const D2D1_RECT_F art{rectangle.right-art_width,rectangle.top,rectangle.right,rectangle.bottom};
      auto bitmap=Image(target,index,muted);const auto size=bitmap->GetSize();
      const bool contain=cave::launcher::kArtwork[index].contain;
      const auto scale=contain?std::min(art_width/size.width,layout.row_height()/size.height):
                               std::max(art_width/size.width,layout.row_height()/size.height);
      const float y=rectangle.top-(size.height*scale-layout.row_height())*cave::launcher::kArtwork[index].focus_y;
      const float art_left=contain?art.right-size.width*scale:art.left;
      target->PushAxisAlignedClip(art,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
      target->DrawBitmap(bitmap,{art_left,y,art_left+size.width*scale,y+size.height*scale},soon?.26f:(muted?.55f:1.f),D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
      target->PopAxisAlignedClip();
      const std::array stops{D2D1_GRADIENT_STOP{0,D2D1::ColorF(color,1)},D2D1_GRADIENT_STOP{.14f,D2D1::ColorF(color,.96f)},
          D2D1_GRADIENT_STOP{.35f,D2D1::ColorF(color,.47f)},D2D1_GRADIENT_STOP{.63f,D2D1::ColorF(color,0)},D2D1_GRADIENT_STOP{1,D2D1::ColorF(color,0)}};
      ComPtr<ID2D1GradientStopCollection> colors;Check(target->CreateGradientStopCollection(stops.data(),UINT32(stops.size()),&colors),"Create card gradient");
      const float fade_right=contain?art_left+size.width*scale*.35f:art.right;
      ComPtr<ID2D1LinearGradientBrush> gradient;Check(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({art_left,art.top},{fade_right,art.top}),colors.Get(),&gradient),"Create card mask");
      target->FillRectangle(art,gradient.Get());
      if (selected==index) {ComPtr<ID2D1SolidColorBrush> accent;Check(target->CreateSolidColorBrush(D2D1::ColorF(0x37644c),&accent),"Create selection accent");target->FillRectangle({rectangle.left,rectangle.top,rectangle.left+4,rectangle.bottom},accent.Get());}
      target->PopLayer();target->DrawRoundedRectangle(round,border.Get(),selected==index?2.f:1.f);
      const float left=rectangle.left+(layout.narrow()?20:25);
      const float block_height=layout.row_height()<100.f?65.f:83.f;
      const float text_top=rectangle.top+(layout.row_height()-block_height)/2;
      Text(target,hidden?T(L"已隐藏"):soon?T(layout.row_height()<100.f?L"敬请期待":L"后续作品"):Wide(cave::launcher::kArtwork[index].kicker),{left,text_top,rectangle.right-16,text_top+14},10,muted?0x9ca59e:0x829087);
      const auto title=Wide(game.DisplayName());
      const auto title_size=layout.narrow()?(title.size()>19?16.f:18.f):(title.size()>24?18.f:20.f);
      Text(target,title,{left,text_top+16,rectangle.right-16,text_top+43},title_size,muted?0x647168:0x23312d);
      Text(target,Wide(game.name),{left,text_top+43,rectangle.right-16,text_top+60},11,muted?0x8c978f:0x748179);
      if(layout.row_height()>=100.f) {
        const float label_top=text_top+65;
        if(soon) Text(target,T(L"敬请期待"),{left,label_top,rectangle.right-20,rectangle.bottom-6},10,0x8c978f);
        else if(selected==index) Text(target,T(L"已选择"),{left,label_top,rectangle.right-20,rectangle.bottom-6},11,0x37644c);
      }
    }
    target->PopAxisAlignedClip();
    const bool has_games=layout.game_count!=0;
    const auto& inspection=rows[selected].inspection;
    Text(target,has_games?Wide(Catalogue()[selected].DisplayName()):T(L"没有显示的游戏"),
        {margin,layout.height-105,layout.play().left-18,layout.height-76},18,0x23312d);
    const auto status=running?T(L"请先退出正在运行的游戏"):busy?T(L"正在读取／校验所选游戏…"):has_games?Status(inspection):L"";
    Text(target,status,{margin,layout.height-80,layout.play().left-18,layout.height-64},11,CanPlay(inspection)?0x356c4b:0x90754f);
    Button(target,layout.play(),running?T(L"游戏运行中"):T(L"启动游戏"),true,busy||running||!has_games||!CanPlay(inspection));
    Button(target,layout.data(),T(L"游戏数据…"),false,busy||running||!has_games||Catalogue()[selected].coming_soon);
    Button(target,layout.refresh(),T(L"刷新"),false,busy||running);
    Button(target,layout.controls(),T(L"输入设置"),false,busy||running||!has_games);
    Button(target,layout.config(),T(L"配置文件"));Button(target,layout.logs(),T(L"上传日志"),false,busy||running);
    Button(target,layout.detail(),T(L"详情"),false,busy||running||!has_games||Catalogue()[selected].coming_soon);
    if ((focus>=4 && focus<10) || focus==13 || focus==14 || (focus==15 && update_ready)) {
      const auto rectangle=layout.focus_button(focus);
      ComPtr<ID2D1SolidColorBrush> brush;Check(target->CreateSolidColorBrush(D2D1::ColorF(0x37644c),&brush),"Create focus indicator");
      target->DrawRoundedRectangle(D2D1::RoundedRect({rectangle.left-3,rectangle.top-3,rectangle.right+3,rectangle.bottom+3},10,10),brush.Get(),2);
    }
    Text(target,busy||running||override_hint.empty()?message:override_hint,
        {margin,layout.height-62,layout.play().left-18,layout.height-46},11,0x7d8881);
    if (preview) Text(target,T(L"视觉预览 · 安装状态为示例"),{margin+200,layout.height-12,layout.width-margin,layout.height},10,0x939d94,DWRITE_TEXT_ALIGNMENT_TRAILING);
  }
  ComPtr<ID2D1Factory> factory;
  ComPtr<IWICImagingFactory> wic;
 private:
  ComPtr<IDWriteFactory> write;
  std::map<float,ComPtr<IDWriteTextFormat>> formats;
  std::array<ComPtr<ID2D1Bitmap>,cave::launcher::kArtwork.size()> bitmaps;
  std::array<ComPtr<ID2D1Bitmap>,cave::launcher::kArtwork.size()> grey_bitmaps;
  std::array<ComPtr<ID2D1Bitmap>,cave::launcher::kArtwork.size()> covers,grey_covers;
  ComPtr<ID2D1Bitmap> logo;
  ID2D1RenderTarget* last_target{};
};
struct Work {
  std::atomic<bool> alive{true};
  std::atomic<bool> cancelled{false};
  std::atomic<uint64_t> copied{},total{};
  std::vector<Row> rows;
  std::optional<LaunchSpec> launch;
  std::optional<CopyResult> copy_result;
  std::string error;
  bool starting{},copying{};
  size_t selected{};
};
class App {
 public:
  explicit App(Options settings):options(std::move(settings)),rows(Catalogue().size()) {
    const auto ini=Ini::Load(EnsureConfig(options.root));for(size_t i=0;i<rows.size();++i) rows[i].inspection.game=Catalogue()[i].id;
    selected=cave::launcher::InitialSelection(ini,options.selection);
    library=cave::launcher::GameLibrary(ini);
    if(!options.selection.empty() && library.Hidden(selected)) {
      library.show_hidden=true;
      library.Rebuild();
    }
    if(library.Hidden(selected) && !library.show_hidden && !library.visible.empty())selected=library.visible.front();
    layout.game_count=library.visible.size();
    layout.covers=cave::launcher::CoverLayout(ini);
    saved_window_size=cave::launcher::ReadWindowSize(ini,layout.covers);
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
    auto notes=release.notes.empty()
        ?std::wstring(ready?T(L"重启启动器以安装更新。"):T(L"发布页提供更新说明和下载。"))
        :Wide(release.notes);
    size_t cut=0,lines=0;
    for(;cut<notes.size() && cut<800 && lines<12;++cut)if(notes[cut]==L'\n')++lines;
    if(cut<notes.size()) {
      if(cut && notes[cut-1]>=0xd800 && notes[cut-1]<=0xdbff)--cut;
      notes.resize(cut);notes+=L"\n…";
    }
    const TASKDIALOG_BUTTON buttons[]{{IDYES,ready?T(L"重启并更新"):T(L"打开发布页")},{IDCANCEL,T(L"关闭")}};
    TASKDIALOGCONFIG dialog{};dialog.cbSize=sizeof(dialog);dialog.hwndParent=hwnd;
    dialog.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;
    dialog.pszWindowTitle=kWindowTitle;dialog.pszMainInstruction=title.c_str();dialog.pszContent=notes.c_str();
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
        if(result->okay)ready_program=std::move(result->release);
        else {
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
        download(root,release);
        return update::Result{true,release,{}};
      });
    }
    // Do not interrupt a running game, another application, minimization or an
    // in-progress nickname edit. Leave the reminder pending until the launcher
    // is in use again.
    if(child || busy || close_requested || preferences_dirty || !IsWindowVisible(hwnd) || IsIconic(hwnd) || GetForegroundWindow()!=hwnd)return;
    if(update_auto && !downloadable && !update_request &&
       update::ShouldNotify(update_state,cave::launcher::kProductVersion))ShowUpdate();
  }
  void ShowDetails() {
    if(busy || child || library.visible.empty() || Catalogue()[selected].coming_soon)return;
    const auto& row=rows[selected];
    std::wstring body=Wide(Catalogue()[selected].DisplayName())+L"\n"+Status(row.inspection)+L"\n"+row.inspection.data_dir.native();
    if(!CanPlay(row.inspection))body+=L"\n"+Wide(Details(row.inspection));
    if(!row.inspection.missing_runtime.empty()) {
      body+=L"\n\n"+std::wstring(T(L"无法读取以下运行组件："));
      for(const auto& issue:InspectRuntime(Catalogue()[selected],options.root)) {
        const auto* kind=issue.kind=="missing"?T(L"文件不存在"):issue.kind=="not_file"?T(L"路径是文件夹"):T(L"文件无法访问");
        body+=L"\n"+DataChild(options.root,issue.path).native()+L"\n  "+kind+L" (Windows "+std::to_wstring(issue.error)+L")";
      }
      body+=L"\n\n"+std::wstring(T(L"请重新解压程序包并替换同名文件，然后点击刷新。若文件存在但无法访问，请检查文件权限或占用。仍有问题可点击上传日志。"));
    }
    if(!row.content.empty()) {
      body+=L"\n\n"+std::wstring(T(L"DLC 查找目录："))+(row.content_source.empty()?T(L"已禁用"):row.content_source.native());
      for(const auto& item:row.content) {
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
    MessageBoxW(hwnd,body.c_str(),T(L"游戏数据详情"),MB_OK|MB_ICONINFORMATION);
  }
  void PersistSelection() noexcept {
    try {cave::launcher::SaveSelection(options.root,selected,layout.covers,normal_window_size);}
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
    EnableWindow(fullscreen_control,enabled);EnableWindow(nickname_control,enabled);
    EnableWindow(save_control,enabled&&preferences_dirty);
  }
  void PopulateDisplay(const Settings& settings) {
    display_choices={{true,settings.window_width,settings.window_height}};
    display_choices.insert(display_choices.end(),kWindowSelections.begin(),kWindowSelections.end());
    const DisplaySelection current{false,settings.window_width,settings.window_height};
    auto found=std::find(display_choices.begin()+1,display_choices.end(),current);
    if(found==display_choices.end()){display_choices.push_back(current);found=display_choices.end()-1;}
    const auto selected=settings.fullscreen?0:std::distance(display_choices.begin(),found);
    SendMessageW(fullscreen_control,CB_RESETCONTENT,0,0);
    for(const auto& choice:display_choices) {
      const auto label=DisplayLabel(choice);
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
    const auto section="game."+std::string(Catalogue()[selected].id);
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
  void Select(size_t index) {
    if(work && (work->starting||work->copying))return;
    if(std::ranges::find(library.visible,index)==library.visible.end())return;
    selected=index;focus=0;SetFocus(hwnd);EnsureVisible();
    try {UpdateOverrideHint(Ini::Load(ConfigPath(options.root)));}catch(...){override_hint.clear();}
    if(!busy&&!child)Refresh();
  }
  void LaunchSelected() {
    if(close_requested)return;
    if(busy||child||library.visible.empty()||!CanPlay(rows[selected].inspection))return;
    if(SavePreferences())Refresh(true);
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
  bool Input(MSG& input) {
    if(input.message!=WM_KEYDOWN || (input.hwnd!=hwnd && !IsChild(hwnd,input.hwnd)))return false;
    try {
      if(input.wParam==VK_TAB) {
        const auto current=GetFocus();
        int slot=current==fullscreen_control?1:(current==nickname_control?2:(current==save_control?3:focus));
        constexpr int slots=16;
        for(int step=0;step<slots;++step) {
          slot=(slot+((GetKeyState(VK_SHIFT)&0x8000)?slots-1:1))%slots;
          const auto control=slot==1?fullscreen_control:(slot==2?nickname_control:(slot==3?save_control:hwnd));
          if(!IsWindowEnabled(control))continue;
          if((slot==6 || slot==8 || slot==9 || slot==13) && library.visible.empty())continue;
          if(slot==6 && (busy||child||Catalogue()[selected].coming_soon))continue;
          if(slot==7 && (busy||child))continue;
          if(slot==8 && (busy||child||!CanPlay(rows[selected].inspection)))continue;
          if(slot==9 && (busy||child||Catalogue()[selected].coming_soon))continue;
          if(slot>=10 && (busy||child||!preferences_available))continue;
          if(slot==15 && !ProgramReady())continue;
          focus=slot;SetFocus(control);InvalidateRect(hwnd,nullptr,FALSE);break;
        }
        return true;
      }
      if(input.hwnd==nickname_control && input.wParam==VK_RETURN) {
        if(SavePreferences()) {SetFocus(hwnd);focus=0;Refresh();}return true;
      }
      if(input.hwnd==save_control && input.wParam==VK_RETURN) {if(SavePreferences())Refresh();return true;}
      if(input.hwnd!=hwnd && input.wParam==VK_F5) {ManualRefresh();return true;}
    }catch(const std::exception& error){Log(options.root,error.what());Error(hwnd,error.what());return true;}
    return false;
  }
  void Refresh(bool launch=false,const fs::path& copy_source={}) {
    if (busy || child) return;
    if (worker.joinable()) worker.join();
    busy=true;EnablePreferences();work=std::make_shared<Work>();work->starting=launch;work->copying=!copy_source.empty();work->selected=selected;
    const auto task=work;const auto root=options.root;const auto id=std::string(Catalogue()[selected].id);const auto profile=options.profile;
    const auto window=hwnd;const auto data=options.data;
    worker=std::thread([task,root,id,profile,window,data,copy_source] {
      try {
        if(task->copying) task->copy_result=CopyDataToCollection(root,FindGame(id),copy_source,
            [task,window](uint64_t done,uint64_t total){task->copied=done;task->total=total;if(task->alive)PostMessageW(window,kCopyProgress,0,0);},
            [task]{return task->cancelled.load();});
        task->rows=InspectRows(root,task->selected);
        if (task->starting) {
          task->launch=PrepareLaunch(FindGame(id),root,Ini::Load(ConfigPath(root)),profile,data);
          Log(root,"launch-verification "+task->launch->verification);
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
        child.emplace(StartGame(*work->launch));
        active_game=work->launch->game;
        message=T(L"正在运行：")+Wide(FindGame(work->launch->game).DisplayName());SetTimer(hwnd,kChildTimer,250,nullptr);
        Log(options.root,"launch game="+work->launch->game+" pid="+std::to_string(child->pid()));
      } else if(work->copy_result) {
        message=T(L"数据已复制并逐文件校验，配置已更新");
        Log(options.root,"data-copy-complete files="+std::to_string(work->copy_result->files)+" bytes="+std::to_string(work->copy_result->bytes));
        if(!work->copy_result->guide_backup.empty())Log(options.root,"placement-guide-backup="+Utf8(work->copy_result->guide_backup));
      } else {message.clear();}
    }catch(const std::exception& error){message=work->copying?T(L"数据复制未完成，请查看详细信息"):T(L"无法启动，请检查数据和运行组件");Log(options.root,error.what());Error(hwnd,error.what());}
    const bool copied=work->copying;work.reset();
    if(copied)ReloadPreferences();EnablePreferences();
    if(selection_changed&&!child)Refresh();
    InvalidateRect(hwnd,nullptr,FALSE);
  }
  void ChildTick() {
    if(!child)return;
    if(const auto code=child->ExitCode()) {
      const bool requested_close=child->RequestedWindowClose(options.root,active_game,*code);
      const auto detail=T(L"游戏异常退出（退出码 ")+std::to_wstring(*code)+T(L"）。\n\n日志目录：logs/")+Wide(active_game)+T(L"/\n启动器可以继续使用。");
      Log(options.root,"child-exit code="+std::to_string(*code));
      message=requested_close?T(L"游戏已关闭（退出清理限制已记入日志）"):(*code?T(L"游戏异常退出，请查看该游戏的日志"):T(L"游戏已退出"));
      const auto exited_game=active_game;
      child.reset();active_game.clear();KillTimer(hwnd,kChildTimer);ShowWindow(hwnd,SW_SHOWNORMAL);SetForegroundWindow(hwnd);EnablePreferences();InvalidateRect(hwnd,nullptr,FALSE);
      if(*code && !requested_close && MessageBoxW(hwnd,(detail+L"\n\n"+T(L"现在上传日志？")).c_str(),kWindowTitle,MB_YESNO|MB_ICONWARNING)==IDYES)
        show_reports(hwnd,options.root,language,exited_game);
    }
  }
  void Choose(bool copy=false) {
    if(busy||child||Catalogue()[selected].coming_soon)return;
    ComPtr<IFileOpenDialog> dialog;Check(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)),"Create data directory picker");
    DWORD flags{};Check(dialog->GetOptions(&flags),"Read directory picker options");
    Check(dialog->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST),"Configure directory picker");
    dialog->SetTitle(copy?T(L"选择要复制到合集的游戏原始数据目录"):T(L"选择游戏原始数据目录（不会移动或修改文件）"));
    const auto result=dialog->Show(hwnd);if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return;Check(result,"Select game data directory");
    ComPtr<IShellItem> item;Check(dialog->GetResult(&item),"Read selected directory");
    PWSTR value{};Check(item->GetDisplayName(SIGDN_FILESYSPATH,&value),"Read selected directory path");
    fs::path path(value);CoTaskMemFree(value);
    if(copy) {
      const auto target=DataChild(options.root,Catalogue()[selected].data_dir);
      const auto prompt=T(L"将完整复制并校验游戏数据。\n\n来源：")+path.native()+T(L"\n目标：")+target.native()+
          T(L"\n\n源文件保持不变。目标须为空或只含放置说明；已有游戏数据不会被覆盖。\n继续吗？");
      if(MessageBoxW(hwnd,prompt.c_str(),T(L"复制到合集"),MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
      if(!SavePreferences())return;message=T(L"正在校验源数据…");Refresh(false,path);return;
    }
    if(!SavePreferences())return;
    SetDataDirectory(options.root,Catalogue()[selected],path);
    ReloadPreferences();Log(options.root,"data-directory-updated game="+std::string(Catalogue()[selected].id));Refresh();
  }
  void DataMenu() {
    if(busy||child||library.visible.empty()||Catalogue()[selected].coming_soon)return;
    const auto menu=CreatePopupMenu();if(!menu)throw std::runtime_error("Cannot create data menu");
    AppendMenuW(menu,MF_STRING,1,T(L"选择数据目录…"));
    AppendMenuW(menu,Catalogue()[selected].integrated?MF_STRING:MF_GRAYED,2,T(L"复制到合集…"));
    const auto rectangle=layout.data();const auto scale=float(GetDpiForWindow(hwnd))/96;
    POINT anchor{LONG(rectangle.left*scale),LONG(rectangle.top*scale)};ClientToScreen(hwnd,&anchor);
    const auto selection=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN|TPM_BOTTOMALIGN,anchor.x,anchor.y,0,hwnd,nullptr);
    DestroyMenu(menu);if(selection)Choose(selection==2);
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
    AppendMenuW(menu,MF_STRING,1,T(library.Hidden(*game)?L"取消隐藏":L"隐藏游戏"));
    const auto action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,screen.x,screen.y,0,hwnd,nullptr);
    DestroyMenu(menu);
    if(action==1)ToggleGameHidden(*game);
  }
  void EnsureVisible() {
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
    if(IsIconic(hwnd))return;
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,int(std::ceil(layout.content_height()))-1),
                    UINT(std::ceil(layout.viewport_height())),int(layout.scroll),0};
    if(layout.covers && UINT(info.nMax+1)<=info.nPage && (GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_VSCROLL)) {
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
      surface->BeginDraw();renderer.Draw(surface.Get(),layout,rows,selected,hover,busy,message,active_game,false,focus,override_hint,&library,ProgramReady());
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
    if(Inside(layout.nickname(),x,y)) {
      if(IsWindowEnabled(nickname_control)){focus=2;SetFocus(nickname_control);}return;
    }
    if(Inside(layout.config(),x,y)) {OpenPath(hwnd,EnsureConfig(options.root));return;}
    if(Inside(layout.controls(),x,y)) {
      if(!busy && !child && !library.visible.empty() && show_controls(hwnd,options.root,Catalogue()[selected],language)) {
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
    if(Inside(layout.refresh(),x,y)) {ManualRefresh();return;}
    if(Inside(layout.play(),x,y)) {LaunchSelected();return;}
    if(Inside(layout.detail(),x,y)) {
      ShowDetails();
      return;
    }
    if(const auto game=GameAt(x,y))Select(*game);
  }
  static LRESULT CALLBACK Window(HWND window,UINT message,WPARAM first,LPARAM second) {
    App* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(second)->lpCreateParams);app->hwnd=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(window,message,first,second);
    try {
      if(message==cave::launcher::ActivationMessage()) {app->Activate();return 1;}
      switch(message) {
        case WM_CREATE:app->CreatePreferences();app->Refresh();app->StartUpdates();return 0;
        case WM_GETMINMAXINFO:{
          auto info=reinterpret_cast<MINMAXINFO*>(second);const auto scale=float(GetDpiForWindow(window))/96;
          info->ptMinTrackSize={LONG(720*scale),LONG(520*scale)};
          MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
          if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)) {
            const auto limit=app->layout.covers?9:10;
            info->ptMinTrackSize.x=std::min(info->ptMinTrackSize.x,(monitor.rcWork.right-monitor.rcWork.left)*limit/10);
            info->ptMinTrackSize.y=std::min(info->ptMinTrackSize.y,(monitor.rcWork.bottom-monitor.rcWork.top)*limit/10);
          }
          return 0;}
        case WM_SIZE:{if(first==SIZE_MINIMIZED)return 0;
          const auto scale=float(GetDpiForWindow(window))/96;app->layout.width=LOWORD(second)/scale;app->layout.height=HIWORD(second)/scale;
          if(first==SIZE_RESTORED && !IsZoomed(window) && LOWORD(second) && HIWORD(second)) {
            RECT bounds{};
            if(GetWindowRect(window,&bounds))app->normal_window_size=cave::launcher::WindowSize{
              int(std::lround((bounds.right-bounds.left)/scale)),int(std::lround((bounds.bottom-bounds.top)/scale))};
          }
          app->PositionPreferences();app->EnsureVisible();return 0;}
        case WM_DPICHANGED:{
          auto bounds=*reinterpret_cast<const RECT*>(second);
          if(!IsZoomed(window) && !IsIconic(window))bounds=FitToWorkArea(bounds);
          SetWindowPos(window,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
                       SWP_NOACTIVATE|SWP_NOZORDER);
          return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:app->Paint();return 0;
        case WM_LBUTTONDOWN:{
          const auto scale=float(GetDpiForWindow(window))/96;
          const POINT point{GET_X_LPARAM(second),GET_Y_LPARAM(second)};
          if(!app->busy && !app->child) {
            if(const auto game=app->GameAt(point.x/scale,point.y/scale)) {
              app->card_press=CardPress{*game,point};
              SetCapture(window);
            }
          }
          return 0;}
        case WM_LBUTTONUP:{
          const auto scale=float(GetDpiForWindow(window))/96;
          const float x=GET_X_LPARAM(second)/scale,y=GET_Y_LPARAM(second)/scale;
          const auto press=std::exchange(app->card_press,std::nullopt);
          if(GetCapture()==window)ReleaseCapture();
          if(press && press->dragged) {
            if(const auto target=app->GameAt(x,y))app->MoveGame(press->game,*target);
          } else app->Click(x,y);
          return 0;}
        case WM_CAPTURECHANGED:app->card_press.reset();return 0;
        case WM_CONTEXTMENU:
          if(reinterpret_cast<HWND>(first)==window) {
            app->GameContextMenu({GET_X_LPARAM(second),GET_Y_LPARAM(second)});
            return 0;
          }
          break;
        case WM_MOUSEMOVE:{const auto scale=float(GetDpiForWindow(window))/96;const float x=GET_X_LPARAM(second)/scale,y=GET_Y_LPARAM(second)/scale;
          if(auto& press=app->card_press;press && !press->dragged) {
            const auto dpi=GetDpiForWindow(window);
            press->dragged=std::abs(GET_X_LPARAM(second)-press->origin.x)>GetSystemMetricsForDpi(SM_CXDRAG,dpi) ||
                           std::abs(GET_Y_LPARAM(second)-press->origin.y)>GetSystemMetricsForDpi(SM_CYDRAG,dpi);
          }
          const auto game=app->GameAt(x,y);
          const auto hover=game?int(*game):-1;
          if(hover!=app->hover){app->hover=hover;app->InvalidateGameList();}return 0;}
        case WM_MOUSEWHEEL:app->Scroll(-float(GET_WHEEL_DELTA_WPARAM(first))/WHEEL_DELTA*90);return 0;
        case kUpdateScrollBar:app->UpdateScrollBar();return 0;
        case WM_VSCROLL:{SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_ALL;GetScrollInfo(window,SB_VERT,&info);float value=app->layout.scroll;
          switch(LOWORD(first)){case SB_THUMBTRACK:value=float(info.nTrackPos);break;case SB_LINEUP:value-=30;break;case SB_LINEDOWN:value+=30;break;case SB_PAGEUP:value-=app->layout.viewport_height();break;case SB_PAGEDOWN:value+=app->layout.viewport_height();break;}app->Scroll(value-app->layout.scroll);return 0;}
        case WM_KEYDOWN:{
          const auto position=app->VisiblePosition();
          const auto& visible=app->library.visible;
          if(first==VK_UP || first==VK_NUMPAD8) {
            const auto step=app->layout.columns();
            if(position && *position>=step)app->Select(visible[*position-step]);
          }
          else if(first==VK_DOWN || first==VK_NUMPAD2) {
            if(position) {
              const auto next=std::min(*position+app->layout.columns(),visible.size()-1);
              if(next!=*position)app->Select(visible[next]);
            }
          }
          else if(app->layout.covers && first==VK_LEFT && position && *position)app->Select(visible[*position-1]);
          else if(app->layout.covers && first==VK_RIGHT && position && *position+1<visible.size())app->Select(visible[*position+1]);
          else if(first==VK_HOME && !visible.empty())app->Select(visible.front());
          else if(first==VK_END && !visible.empty())app->Select(visible.back());
          else if(first==VK_APPS || (first==VK_F10 && (GetKeyState(VK_SHIFT)&0x8000)))app->GameContextMenu({-1,-1});
          else if(first==VK_RETURN || first==VK_SPACE) {
            if(app->focus>=4) {
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
            app->message=T(L"正在复制并校验数据… ")+std::to_wstring(total?uint64_t(double(copied)/double(total)*100):0)+L"%";
            InvalidateRect(window,nullptr,FALSE);
          }return 0;
        case WM_TIMER:if(first==kUpdateTimer)app->UpdateTick();else if(first==kChildTimer)app->ChildTick();return 0;
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
  struct CardPress { size_t game; POINT origin; bool dragged{}; };
  std::optional<CardPress> card_press;
  std::optional<cave::launcher::WindowSize> saved_window_size,normal_window_size;
  bool scrollbar_pending{};
  std::vector<Row> rows;
  size_t selected{};
  int hover{-1};
  int focus{};
  bool busy{};
  bool paint_recorded{};
  bool close_requested{};
  std::wstring message=T(L"正在检测游戏数据…");
  ComPtr<ID2D1DCRenderTarget> surface;
  std::shared_ptr<Work> work;
  std::thread worker;
  std::optional<ChildProcess> child;
  std::string active_game;
  update::State update_state;
  std::function<update::Result()> update_fetch=update::Fetch;
  decltype(&cave::launcher::reports::Show) show_reports=cave::launcher::reports::Show;
  decltype(&cave::launcher::controls::Show) show_controls=cave::launcher::controls::Show;
  std::optional<update::Request> update_request,program_download;
  std::optional<update::Release> ready_program;
  std::chrono::steady_clock::time_point program_retry{};
  decltype(&update::DownloadProgram) download_program=update::DownloadProgram;
  decltype(&update::StartProgramWorker) start_update_worker=update::StartProgramWorker;
  bool update_auto{},update_dialog{},packaged_update{};
};
void Preview(const Options& options) {
  if(fs::exists(options.preview))throw std::runtime_error("Preserve the existing launcher preview");
  language=options.language.value_or(cave::launcher::ReadLanguage(Ini::Load(ConfigPath(options.root))));
  Renderer renderer;
  ComPtr<IWICBitmap> bitmap;Check(renderer.wic->CreateBitmap(1080,900,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap),"Create preview bitmap");
  ComPtr<ID2D1RenderTarget> target;Check(renderer.factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(),&target),"Create preview surface");
  size_t selected=1;
  if(!options.selection.empty())for(size_t index=0;index<Catalogue().size();++index)
    if(Catalogue()[index].id==options.selection)selected=index;
  auto rows=InspectRows(options.root,selected);
  if(options.selection.empty()) {
    rows[0].inspection.state="missing_data";rows[1].inspection.state="ready";rows[2].inspection.state="not_integrated";
    rows[0].inspection.data_state=DataState::Missing;rows[1].inspection.data_state=DataState::Verified;
  }
  Layout layout{1080,900,0};
  layout.covers=cave::launcher::CoverLayout(Ini::Load(EnsureConfig(options.root)));
  layout.scroll=std::clamp(layout.card(selected).bottom-layout.footer(),0.f,layout.max_scroll());
  const auto caption=std::to_wstring(Catalogue().size())+T(L" 部作品 · 按需安装游戏数据");
  const cave::launcher::GameLibrary library(Ini::Parse(""));
  target->BeginDraw();renderer.Draw(target.Get(),layout,rows,selected,-1,false,caption,{},true,0,{},&library);Check(target->EndDraw(),"Render preview");
  ComPtr<IWICStream> stream;Check(renderer.wic->CreateStream(&stream),"Create preview stream");
  if(!options.preview.parent_path().empty())fs::create_directories(options.preview.parent_path());
  Check(stream->InitializeFromFilename(options.preview.c_str(),GENERIC_WRITE),"Open preview file");
  ComPtr<IWICBitmapEncoder> encoder;Check(renderer.wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"Create preview encoder");
  Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Initialize preview encoder");
  ComPtr<IWICBitmapFrameEncode> frame;Check(encoder->CreateNewFrame(&frame,nullptr),"Create preview frame");Check(frame->Initialize(nullptr),"Initialize preview frame");
  Check(frame->SetSize(1080,900),"Set preview size");WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;Check(frame->SetPixelFormat(&format),"Set preview format");
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
    if(!options.update_action.empty())return update::RunProgramWorker(options.root,options.update_action,options.update_parent);
    const auto recovery=update::RecoveryPending(options.root);
    if(!options.game.empty()) {
      if(recovery)throw std::runtime_error("Open the launcher to finish update recovery.");
      EnsureConfig(options.root);
      auto spec=PrepareLaunch(FindGame(options.game),options.root,Ini::Load(ConfigPath(options.root)),options.profile,options.data);
      auto child=StartGame(spec);child.Wait();const auto code=child.ExitCode().value_or(1);return int(code);
    }
    cave::launcher::LauncherInstance instance_guard(options.root);
    if(!instance_guard.ActivateOrAcquire())return 0;
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
    FitInitialWindow(window,app.layout.covers,app.saved_window_size);
    ShowWindow(window,show);UpdateWindow(window);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(app.Input(message))continue;TranslateMessage(&message);DispatchMessageW(&message);}
    return 0;
  }catch(const std::exception& error){
    if(render_preview)std::fprintf(stderr,"Launcher preview: %s\n",error.what());
    else Error(nullptr,error.what());
    return 2;
  }
}
