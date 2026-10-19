#include "common/catalogue.h"
#include "common/ini.h"
#include "common/launch.h"
#include "common/data_import.h"
#include "common/settings.h"
#include "artwork.h"
#include "launcher/window_size.h"
#include "launcher/strings.h"
#include "common/config_update.h"
#include <Windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <shobjidl.h>
#include <shellapi.h>
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

using Microsoft::WRL::ComPtr;
using namespace cave;
namespace {
constexpr UINT kWorkDone=WM_APP+1;
constexpr UINT kCopyProgress=WM_APP+2;
constexpr UINT_PTR kChildTimer=1;
constexpr int kFullscreenControl=101,kNicknameControl=102,kSaveControl=103;
constexpr wchar_t kWindowClass[]=L"CavePCCollection.Launcher.v1";
using cave::launcher::Language;
Language language=Language::Chinese;
const wchar_t* T(std::wstring_view key) {return cave::launcher::Translate(language,key);}
void Check(HRESULT status,const char* operation) {
  if (FAILED(status)) throw std::runtime_error(std::string(operation)+" (HRESULT "+std::to_string(static_cast<unsigned>(status))+")");
}
std::wstring Wide(std::string_view text) {return FromUtf8(text).native();}
void Error(HWND window,std::string_view message) {
  std::wstring value;
  try {value=Wide(message.size()<16000?message:std::string_view("Error message is too long; check the log."));}
  catch (...) {value=T(L"无法显示详细信息，请检查日志。");}
  value=std::wstring(T(L"操作失败。详细信息：\n\n"))+value;
  MessageBoxW(window,value.c_str(),L"CAVE PC Collection",MB_OK|MB_ICONERROR);
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
  const auto ini=Ini::Load(root/"config.ini");
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
  std::string game,profile="default";
  std::optional<Language> language;
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
      else if (key==L"--profile") options.profile=Utf8(value);
      else if (key==L"--data-dir") options.data=value;
      else if (key==L"--render-preview") options.preview=value;
      else if (key==L"--language") options.language=cave::launcher::ParseLanguage(Utf8(value));
      else throw std::runtime_error("Unknown launcher option");
    }
  } catch (...) {LocalFree(arguments);throw;}
  LocalFree(arguments);
  options.root=fs::canonical(options.root);
  if (!options.data.empty() && options.game.empty()) throw std::runtime_error("--data-dir requires --game");
  if (!options.game.empty()) (void)FindGame(options.game);
  return options;
}
struct Row {Inspection inspection;};
std::vector<Row> InspectRows(const fs::path& root,size_t selected) {
  std::vector<Row> rows(Catalogue().size());
  for(size_t index=0;index<rows.size();++index) {
    rows[index].inspection.game=Catalogue()[index].id;
    rows[index].inspection.state=Catalogue()[index].coming_soon?"coming_soon":"checking";
  }
  try {
    const auto ini=Ini::Load(root/"config.ini");
    {
      const auto index=selected;
      const auto& game=Catalogue()[index];
      try {
        if (game.integrated) (void)ResolveSettings(ini,game.id);
        rows[index].inspection=Inspect(game,root,ini.DataDirectory(game),CheckLevel::Quick);
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
  return rows;
}
std::wstring Status(const Inspection& inspection) {
  const auto& state=inspection.state;
  if (state=="ready") return T(L"已就绪");
  if (state=="needs_verification") return T(L"数据已找到 · 启动时校验");
  if (state=="missing_data") return T(L"未找到游戏数据");
  if (state=="incomplete_data") return T(L"游戏数据不完整");
  if (state=="unsupported_version") return T(L"数据版本不支持");
  if (state=="missing_runtime") return T(L"缺少运行组件");
  if (state=="invalid_config") return T(L"配置有误 · 请检查 config.ini");
  if (state=="invalid_path") return T(L"数据路径不可用");
  if (state=="not_integrated") return T(L"正在接入");
  if (state=="coming_soon") return T(L"敬请期待");
  return T(L"正在检测…");
}
bool CanPlay(const Inspection& inspection) {return inspection.state=="ready" || inspection.state=="needs_verification";}
std::string Details(const Inspection& inspection) {
  auto result=inspection.reason.empty()?inspection.state:inspection.reason;
  if (!inspection.problems.empty()) result+="\n"+inspection.problems.front();
  if (!inspection.missing_runtime.empty()) result+="\n"+inspection.missing_runtime.front();
  return result;
}
struct Layout {
  float width{},height{},scroll{};
  bool narrow() const {return width<800;}
  float margin() const {return std::max(narrow()?21.f:32.f,(width-1120.f)/2);}
  float header() const {return cave::launcher::kHeaderHeight;}
  float footer() const {return height-cave::launcher::kFooterHeight;}
  float row_height() const {
    const auto count=float(Catalogue().size());
    return std::clamp((viewport_height()-cave::launcher::kCardGap*(count-1))/count,
                      float(cave::launcher::kMinimumCardHeight),180.f);
  }
  float viewport_height() const {return std::max(0.f,footer()-header());}
  float content_height() const {return float(Catalogue().size())*(row_height()+12)-12;}
  float max_scroll() const {return std::max(0.f,content_height()-viewport_height());}
  D2D1_RECT_F card(size_t index) const {const auto top=header()+float(index)*(row_height()+12)-scroll;return {margin(),top,width-margin(),top+row_height()};}
  D2D1_RECT_F play() const {return {width-margin()-176,height-99,width-margin(),height-49};}
  D2D1_RECT_F config() const {return {width-margin()-177,28,width-margin()-79,64};}
  D2D1_RECT_F logs() const {return {width-margin()-68,28,width-margin(),64};}
  D2D1_RECT_F language_button() const {return {width-margin()-177,71,width-margin(),93};}
  D2D1_RECT_F data() const {return {margin(),height-45,margin()+100,height-14};}
  D2D1_RECT_F refresh() const {return {margin()+110,height-45,margin()+175,height-14};}
  D2D1_RECT_F detail() const {return {margin()+185,height-45,margin()+250,height-14};}
  D2D1_RECT_F fullscreen() const {return {margin(),99,margin()+132,127};}
  D2D1_RECT_F nickname() const {return {margin()+220,98,margin()+410,128};}
  D2D1_RECT_F save() const {return {margin()+421,98,margin()+483,128};}
};
bool Inside(D2D1_RECT_F r,float x,float y) {return x>=r.left && x<r.right && y>=r.top && y<r.bottom;}
void FitInitialWindow(HWND window) {
  MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
  if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))
    throw std::runtime_error("Cannot read launcher monitor work area");
  const auto dpi=GetDpiForWindow(window);
  RECT frame{};
  if(!AdjustWindowRectExForDpi(&frame,WS_OVERLAPPEDWINDOW|WS_VSCROLL,FALSE,0,dpi))
    throw std::runtime_error("Cannot size launcher window frame");
  RECT current{};GetWindowRect(window,&current);
  const auto& work=monitor.rcWork;
  const auto size=cave::launcher::InitialWindowSize(current.right-current.left,
      work.right-work.left,work.bottom-work.top,dpi,frame.bottom-frame.top,Catalogue().size());
  SetWindowPos(window,nullptr,work.left+(work.right-work.left-size.width)/2,
      work.top+(work.bottom-work.top-size.height)/2,size.width,size.height,SWP_NOZORDER|SWP_NOACTIVATE);
}
class Renderer {
 public:
  Renderer() {
    Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()),"Create Direct2D factory");
    Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write.GetAddressOf())),"Create text factory");
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"Create image factory");
    for (const auto size:{10.f,11.f,12.f,14.f,16.f,18.f,20.f,24.f,27.f}) {
      ComPtr<IDWriteTextFormat> format;
      Check(write->CreateTextFormat(L"Segoe UI",nullptr,size>=20?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-CN",&format),"Create launcher font");
      format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
      formats.emplace(size,std::move(format));
    }
  }
  void Text(ID2D1RenderTarget* target,std::wstring_view value,D2D1_RECT_F rectangle,float size,UINT32 color,
            DWRITE_TEXT_ALIGNMENT align=DWRITE_TEXT_ALIGNMENT_LEADING) {
    auto format=formats.at(size);format->SetTextAlignment(align);
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
  ID2D1Bitmap* Image(ID2D1RenderTarget* target,size_t index) {
    if (last_target!=target) {for(auto& bitmap:bitmaps)bitmap.Reset();last_target=target;}
    if (bitmaps[index]) return bitmaps[index].Get();
    const auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(cave::launcher::kArtwork[index].resource),RT_RCDATA);
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
    Check(target->CreateBitmapFromWicBitmap(converter.Get(),nullptr,&bitmaps[index]),"Upload banner");return bitmaps[index].Get();
  }
  void Draw(ID2D1RenderTarget* target,const Layout& layout,const std::vector<Row>& rows,size_t selected,int hover,bool busy,
            std::wstring_view message,std::string_view running_game={},bool preview=false,int focus=0,
            bool preferences_dirty=false,std::wstring_view override_hint={}) {
    const bool running=!running_game.empty();
    const auto margin=layout.margin();target->Clear(D2D1::ColorF(0xf3f4f0));
    Text(target,L"C A V E",{margin,25,margin+130,65},27,0x23312d);
    Text(target,L"PC COLLECTION",{margin+(layout.narrow()?0:140),layout.narrow()?61.f:40.f,margin+320,80},10,0x6f7c76);
    if (!layout.narrow()) Text(target,T(L"熟悉的弹幕，新的入口。"),{margin,70,margin+360,92},12,0x78827c);
    Button(target,layout.config(),T(L"配置文件"));Button(target,layout.logs(),T(L"日志"));
    Button(target,layout.language_button(),cave::launcher::LanguageLabel(language),false,busy||running);
    if(preview) {
      Button(target,layout.fullscreen(),T(L"□ 启动时全屏"));
      Button(target,layout.nickname(),L"Player");Button(target,layout.save(),T(L"保存"),false,true);
    }
    Text(target,T(L"排行榜昵称"),{margin+153,105,margin+225,128},11,0x627368);
    if(!layout.narrow()) Text(target,preferences_dirty?T(L"未保存 · 启动前会自动保存"):T(L"共用设置 · 下次启动生效"),
        {margin+498,106,layout.width-margin,128},11,preferences_dirty?0x90754f:0x7d8881);
    target->PushAxisAlignedClip({0,layout.header()-2,layout.width,layout.footer()},D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t index=0;index<rows.size();++index) {
      const auto rectangle=layout.card(index);if(rectangle.bottom<layout.header() || rectangle.top>layout.footer())continue;
      const auto& game=Catalogue()[index];const bool soon=game.coming_soon;
      const auto color=soon?0xf6f7f4:0xffffff;
      ComPtr<ID2D1SolidColorBrush> background,border;
      Check(target->CreateSolidColorBrush(D2D1::ColorF(color),&background),"Create card fill");
      Check(target->CreateSolidColorBrush(D2D1::ColorF(selected==index?0x37644c:(hover==int(index)?0xa0b1a3:0xdee3db)),&border),"Create card border");
      auto round=D2D1::RoundedRect(rectangle,13,13);target->FillRoundedRectangle(round,background.Get());
      ComPtr<ID2D1RoundedRectangleGeometry> clip;Check(factory->CreateRoundedRectangleGeometry(round,&clip),"Create card clip");
      target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),clip.Get()),nullptr);
      const float art_width=(rectangle.right-rectangle.left)*(layout.narrow()?.61f:.66f);
      const D2D1_RECT_F art{rectangle.right-art_width,rectangle.top,rectangle.right,rectangle.bottom};
      auto bitmap=Image(target,index);const auto size=bitmap->GetSize();
      const bool contain=cave::launcher::kArtwork[index].contain;
      const auto scale=contain?std::min(art_width/size.width,layout.row_height()/size.height):
                               std::max(art_width/size.width,layout.row_height()/size.height);
      const float y=rectangle.top-(size.height*scale-layout.row_height())*cave::launcher::kArtwork[index].focus_y;
      const float art_left=contain?art.right-size.width*scale:art.left;
      target->PushAxisAlignedClip(art,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
      target->DrawBitmap(bitmap,{art_left,y,art_left+size.width*scale,y+size.height*scale},soon?.26f:1.f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
      target->PopAxisAlignedClip();
      const std::array stops{D2D1_GRADIENT_STOP{0,D2D1::ColorF(color,1)},D2D1_GRADIENT_STOP{.14f,D2D1::ColorF(color,.96f)},
          D2D1_GRADIENT_STOP{.35f,D2D1::ColorF(color,.47f)},D2D1_GRADIENT_STOP{.63f,D2D1::ColorF(color,0)},D2D1_GRADIENT_STOP{1,D2D1::ColorF(color,0)}};
      ComPtr<ID2D1GradientStopCollection> colors;Check(target->CreateGradientStopCollection(stops.data(),UINT32(stops.size()),&colors),"Create card gradient");
      ComPtr<ID2D1LinearGradientBrush> gradient;Check(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({art.left,art.top},{art.right,art.top}),colors.Get(),&gradient),"Create card mask");
      target->FillRectangle(art,gradient.Get());
      if (selected==index) {ComPtr<ID2D1SolidColorBrush> accent;Check(target->CreateSolidColorBrush(D2D1::ColorF(0x37644c),&accent),"Create selection accent");target->FillRectangle({rectangle.left,rectangle.top,rectangle.left+4,rectangle.bottom},accent.Get());}
      target->PopLayer();target->DrawRoundedRectangle(round,border.Get(),selected==index?2.f:1.f);
      const float left=rectangle.left+(layout.narrow()?20:25);
      const float block_height=layout.row_height()<100.f?65.f:83.f;
      const float text_top=rectangle.top+(layout.row_height()-block_height)/2;
      Text(target,soon?T(layout.row_height()<100.f?L"敬请期待":L"后续作品"):Wide(cave::launcher::kArtwork[index].kicker),{left,text_top,rectangle.right-16,text_top+14},10,soon?0x9ca59e:0x829087);
      const auto title=Wide(game.DisplayName());
      const auto title_size=layout.narrow()?(title.size()>19?16.f:18.f):(title.size()>24?18.f:20.f);
      Text(target,title,{left,text_top+16,rectangle.right-16,text_top+43},title_size,soon?0x737e76:0x23312d);
      Text(target,Wide(game.name),{left,text_top+43,rectangle.right-16,text_top+60},11,soon?0x8c978f:0x748179);
      if(layout.row_height()>=100.f) {
        const float label_top=text_top+65;
        if(soon) Text(target,T(L"敬请期待"),{left,label_top,rectangle.right-20,rectangle.bottom-6},10,0x8c978f);
        else if(selected==index) Text(target,T(L"已选择"),{left,label_top,rectangle.right-20,rectangle.bottom-6},11,0x37644c);
      }
    }
    target->PopAxisAlignedClip();
    const auto& inspection=rows[selected].inspection;
    Text(target,Wide(Catalogue()[selected].DisplayName()),{margin,layout.height-105,layout.play().left-18,layout.height-76},18,0x23312d);
    const auto status=running?T(L"请先退出正在运行的游戏"):(busy?T(L"正在读取／校验所选游戏…"):Status(inspection));
    Text(target,status,{margin,layout.height-77,layout.play().left-18,layout.height-56},11,CanPlay(inspection)?0x356c4b:0x90754f);
    Button(target,layout.play(),running?T(L"游戏运行中"):T(L"启动游戏"),true,busy||running||!CanPlay(inspection));
    Button(target,layout.data(),T(L"游戏数据…"),false,busy||running||Catalogue()[selected].coming_soon);
    Button(target,layout.refresh(),T(L"刷新"),false,busy||running);
    Button(target,layout.detail(),T(L"详情"),false,busy||running||CanPlay(inspection)||Catalogue()[selected].coming_soon);
    if (focus>=4) {
      const auto rectangle=focus==10?layout.language_button():(focus==4?layout.config():(focus==5?layout.logs():(focus==6?layout.data():(focus==7?layout.refresh():(focus==8?layout.play():layout.detail())))));
      ComPtr<ID2D1SolidColorBrush> brush;Check(target->CreateSolidColorBrush(D2D1::ColorF(0x37644c),&brush),"Create focus indicator");
      target->DrawRoundedRectangle(D2D1::RoundedRect({rectangle.left-3,rectangle.top-3,rectangle.right+3,rectangle.bottom+3},10,10),brush.Get(),2);
    }
    Text(target,override_hint.empty()?message:override_hint,{margin+266,layout.height-36,layout.width-margin,layout.height-14},11,0x7d8881,DWRITE_TEXT_ALIGNMENT_TRAILING);
    if (preview) Text(target,T(L"视觉预览 · 安装状态为示例"),{margin+200,layout.height-17,layout.width-margin,layout.height},10,0x939d94,DWRITE_TEXT_ALIGNMENT_TRAILING);
  }
  ComPtr<ID2D1Factory> factory;
  ComPtr<IWICImagingFactory> wic;
 private:
  ComPtr<IDWriteFactory> write;
  std::map<float,ComPtr<IDWriteTextFormat>> formats;
  std::array<ComPtr<ID2D1Bitmap>,cave::launcher::kArtwork.size()> bitmaps;
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
    EnsureConfig(options.root);for(size_t i=0;i<rows.size();++i) rows[i].inspection.game=Catalogue()[i].id;
    language=options.language.value_or(cave::launcher::ReadLanguage(Ini::Load(options.root/"config.ini")));
    message=T(L"正在检测游戏数据…");
  }
  ~App() {if(work)work->alive=false;if(worker.joinable())worker.detach();}
  void CreatePreferences() {
    const auto instance=GetModuleHandleW(nullptr);
    fullscreen_control=CreateWindowExW(0,L"BUTTON",T(L"启动时全屏"),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX|BS_NOTIFY,
        0,0,0,0,hwnd,reinterpret_cast<HMENU>(kFullscreenControl),instance,nullptr);
    nickname_control=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
        0,0,0,0,hwnd,reinterpret_cast<HMENU>(kNicknameControl),instance,nullptr);
    save_control=CreateWindowExW(0,L"BUTTON",T(L"保存"),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON|BS_NOTIFY,
        0,0,0,0,hwnd,reinterpret_cast<HMENU>(kSaveControl),instance,nullptr);
    if(!fullscreen_control||!nickname_control||!save_control)throw std::runtime_error("Cannot create quick settings");
    SendMessageW(nickname_control,EM_SETLIMITTEXT,64,0);
    form_background=CreateSolidBrush(RGB(243,244,240));
    PositionPreferences();ReloadPreferences();
  }
  void PositionPreferences() {
    if(!nickname_control)return;
    const auto dpi=GetDpiForWindow(hwnd);const auto scale=float(dpi)/96;
    const auto font=CreateFontW(-MulDiv(13,int(dpi),96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    for(const auto pair:std::array{std::pair{fullscreen_control,layout.fullscreen()},
                                  std::pair{nickname_control,layout.nickname()},std::pair{save_control,layout.save()}}) {
      const auto r=pair.second;
      SetWindowPos(pair.first,nullptr,int(r.left*scale),int(r.top*scale),int((r.right-r.left)*scale),int((r.bottom-r.top)*scale),SWP_NOZORDER|SWP_NOACTIVATE);
      if(font)SendMessageW(pair.first,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    }
    if(font){if(form_font)DeleteObject(form_font);form_font=font;}
  }
  void EnablePreferences() {
    const bool enabled=preferences_available&&!busy&&!child;
    EnableWindow(fullscreen_control,enabled);EnableWindow(nickname_control,enabled);
    EnableWindow(save_control,enabled&&preferences_dirty);
  }
  void ReloadPreferences() {
    loading_preferences=true;
    try {
      preferences_text=Ini::ReadText(EnsureConfig(options.root));
      const auto ini=Ini::Parse(preferences_text);const auto settings=ResolveSharedSettings(ini);
      language=options.language.value_or(cave::launcher::ReadLanguage(ini));
      SetWindowTextW(fullscreen_control,T(L"启动时全屏"));SetWindowTextW(save_control,T(L"保存"));
      SendMessageW(fullscreen_control,BM_SETCHECK,settings.fullscreen?BST_CHECKED:BST_UNCHECKED,0);
      SetWindowTextW(nickname_control,Wide(settings.nickname).c_str());
      preferences_available=true;preferences_dirty=false;UpdateOverrideHint(ini);
    } catch(const std::exception& error) {
      preferences_available=false;preferences_dirty=false;
      message=T(L"快捷设置读取失败，请打开配置文件修正后刷新");Log(options.root,error.what());
    }
    loading_preferences=false;EnablePreferences();InvalidateRect(hwnd,nullptr,FALSE);
  }
  void UpdateOverrideHint(const Ini& ini) {
    override_hint.clear();const auto section="game."+std::string(Catalogue()[selected].id);
    if(ini.Get(section+".display","fullscreen") || ini.Get(section+".community","nickname"))
      override_hint=T(L"此游戏有独立的全屏／昵称设置，请在配置文件中调整");
  }
  void SavePreferences() {
    if(!preferences_dirty)return;
    if(!preferences_available)throw std::runtime_error("Fix config.ini and refresh before saving quick settings.");
    const auto count=GetWindowTextLengthW(nickname_control);
    std::wstring name(size_t(count)+1,L'\0');GetWindowTextW(nickname_control,name.data(),int(name.size()));name.resize(count);
    SaveSharedPreferences(options.root,preferences_text,SendMessageW(fullscreen_control,BM_GETCHECK,0,0)==BST_CHECKED,Utf8(name));
    ReloadPreferences();message=T(L"快捷设置已保存，下次启动生效");
    Log(options.root,"shared-preferences-updated");
  }
  void ManualRefresh() {
    if(busy||child)return;
    if(preferences_dirty && MessageBoxW(hwnd,T(L"刷新将放弃尚未保存的快捷设置，重新读取 config.ini。继续吗？"),
        T(L"刷新配置"),MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
    ReloadPreferences();Refresh();
  }
  void Select(size_t index) {
    if(work && (work->starting||work->copying))return;
    selected=index;focus=0;SetFocus(hwnd);EnsureVisible();
    try {UpdateOverrideHint(Ini::Load(options.root/"config.ini"));}catch(...){override_hint.clear();}
    if(!busy&&!child)Refresh();
  }
  void LaunchSelected() {
    if(busy||child||!CanPlay(rows[selected].inspection))return;
    SavePreferences();Refresh(true);
  }
  void LanguageMenu() {
    if(busy||child||!preferences_available)return;
    const auto menu=CreatePopupMenu();if(!menu)throw std::runtime_error("Cannot create language menu");
    const std::array names{L"中文",L"日本語",L"English"};
    for(size_t index=0;index<names.size();++index)
      AppendMenuW(menu,MF_STRING|(size_t(language)==index?MF_CHECKED:0),index+1,names[index]);
    const auto rectangle=layout.language_button();const auto scale=float(GetDpiForWindow(hwnd))/96;
    POINT anchor{LONG(rectangle.left*scale),LONG(rectangle.bottom*scale)};ClientToScreen(hwnd,&anchor);
    const auto selection=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,anchor.x,anchor.y,0,hwnd,nullptr);
    DestroyMenu(menu);if(!selection)return;
    const auto next=Language(selection-1);
    const std::array changes{IniEdit{"collection","language",cave::launcher::LanguageCode(next)}};
    UpdateConfig(options.root,preferences_text,changes);
    preferences_text=Ini::ReadText(options.root/"config.ini");
    language=next;options.language.reset();
    SetWindowTextW(fullscreen_control,T(L"启动时全屏"));SetWindowTextW(save_control,T(L"保存"));
    UpdateOverrideHint(Ini::Parse(preferences_text));message=T(L"↑ ↓ 选择 · Enter 启动");
    InvalidateRect(hwnd,nullptr,FALSE);
  }
  bool Input(MSG& input) {
    if(input.message!=WM_KEYDOWN || (input.hwnd!=hwnd && !IsChild(hwnd,input.hwnd)))return false;
    try {
      if(input.wParam==VK_TAB) {
        const auto current=GetFocus();
        int slot=current==fullscreen_control?1:(current==nickname_control?2:(current==save_control?3:focus));
        for(int step=0;step<11;++step) {
          slot=(slot+((GetKeyState(VK_SHIFT)&0x8000)?10:1))%11;
          const auto control=slot==1?fullscreen_control:(slot==2?nickname_control:(slot==3?save_control:hwnd));
          if(!IsWindowEnabled(control))continue;
          if(slot==6 && (busy||child||Catalogue()[selected].coming_soon))continue;
          if(slot==7 && (busy||child))continue;
          if(slot==8 && (busy||child||!CanPlay(rows[selected].inspection)))continue;
          if(slot==9 && (busy||child||CanPlay(rows[selected].inspection)||Catalogue()[selected].coming_soon))continue;
          if(slot==10 && (busy||child||!preferences_available))continue;
          focus=slot;SetFocus(control);InvalidateRect(hwnd,nullptr,FALSE);break;
        }
        return true;
      }
      if(input.hwnd==nickname_control && input.wParam==VK_RETURN) {
        SavePreferences();SetFocus(hwnd);focus=0;Refresh();return true;
      }
      if(input.hwnd==save_control && input.wParam==VK_RETURN) {SavePreferences();Refresh();return true;}
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
        if (task->starting) task->launch=PrepareLaunch(FindGame(id),root,Ini::Load(root/"config.ini"),profile,data);
      } catch(const std::exception& error) {task->error=error.what();}
      if(task->alive) PostMessageW(window,kWorkDone,0,0);
    });
    InvalidateRect(hwnd,nullptr,FALSE);
  }
  void Complete() {
    if(worker.joinable())worker.join();busy=false;EnablePreferences();
    if (!work) return;
    if(close_requested){work.reset();DestroyWindow(hwnd);return;}
    if (!work->rows.empty()) rows[work->selected]=std::move(work->rows[work->selected]);
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
      } else {message=T(L"↑ ↓ 选择 · Enter 启动");}
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
      child.reset();active_game.clear();KillTimer(hwnd,kChildTimer);ShowWindow(hwnd,SW_SHOWNORMAL);SetForegroundWindow(hwnd);EnablePreferences();InvalidateRect(hwnd,nullptr,FALSE);
      if(*code && !requested_close)MessageBoxW(hwnd,detail.c_str(),L"CAVE PC Collection",MB_OK|MB_ICONWARNING);
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
          T(L"\n\n不会移动或修改源文件。目标必须为空，不会合并或覆盖已有文件。\n继续吗？");
      if(MessageBoxW(hwnd,prompt.c_str(),T(L"复制到合集"),MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
      SavePreferences();message=T(L"正在校验源数据…");Refresh(false,path);return;
    }
    SavePreferences();
    SetDataDirectory(options.root,Catalogue()[selected],path);
    ReloadPreferences();Log(options.root,"data-directory-updated game="+std::string(Catalogue()[selected].id));Refresh();
  }
  void DataMenu() {
    if(busy||child||Catalogue()[selected].coming_soon)return;
    const auto menu=CreatePopupMenu();if(!menu)throw std::runtime_error("Cannot create data menu");
    AppendMenuW(menu,MF_STRING,1,T(L"选择数据目录…"));
    AppendMenuW(menu,Catalogue()[selected].integrated?MF_STRING:MF_GRAYED,2,T(L"复制到合集…"));
    const auto rectangle=layout.data();const auto scale=float(GetDpiForWindow(hwnd))/96;
    POINT anchor{LONG(rectangle.left*scale),LONG(rectangle.top*scale)};ClientToScreen(hwnd,&anchor);
    const auto selection=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN|TPM_BOTTOMALIGN,anchor.x,anchor.y,0,hwnd,nullptr);
    DestroyMenu(menu);if(selection)Choose(selection==2);
  }
  void EnsureVisible() {
    const auto top=float(selected)*(layout.row_height()+12),bottom=top+layout.row_height();
    if(top<layout.scroll)layout.scroll=top;
    if(bottom>layout.scroll+layout.viewport_height())layout.scroll=bottom-layout.viewport_height();
    Scroll(0);
  }
  void Scroll(float delta) {
    layout.scroll=std::clamp(layout.scroll+delta,0.f,layout.max_scroll());
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,int(std::ceil(layout.content_height()))-1),
                    UINT(std::ceil(layout.viewport_height())),int(layout.scroll),0};
    SetScrollInfo(hwnd,SB_VERT,&info,TRUE);InvalidateRect(hwnd,nullptr,FALSE);
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
      surface->BeginDraw();renderer.Draw(surface.Get(),layout,rows,selected,hover,busy,message,active_game,false,focus,preferences_dirty,override_hint);
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
    if(Inside(layout.config(),x,y)) {OpenPath(hwnd,EnsureConfig(options.root));return;}
    if(Inside(layout.logs(),x,y)) {const auto logs=LogDirectory(options.root);if(!fs::exists(logs))fs::create_directories(logs);OpenPath(hwnd,logs);return;}
    if(Inside(layout.language_button(),x,y)) {LanguageMenu();return;}
    if(Inside(layout.data(),x,y)) {DataMenu();return;}
    if(Inside(layout.refresh(),x,y)) {ManualRefresh();return;}
    if(Inside(layout.play(),x,y)) {LaunchSelected();return;}
    if(Inside(layout.detail(),x,y)) {
      if(!busy&&!child&&!CanPlay(rows[selected].inspection)&&!Catalogue()[selected].coming_soon)Error(hwnd,Details(rows[selected].inspection));
      return;
    }
    if(y<layout.header() || y>layout.footer())return;
    for(size_t index=0;index<rows.size();++index) if(Inside(layout.card(index),x,y)) {
      Select(index);return;
    }
  }
  static LRESULT CALLBACK Window(HWND window,UINT message,WPARAM first,LPARAM second) {
    App* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(second)->lpCreateParams);app->hwnd=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(window,message,first,second);
    try {
      switch(message) {
        case WM_CREATE:app->CreatePreferences();app->Refresh();return 0;
        case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(second);const auto scale=float(GetDpiForWindow(window))/96;info->ptMinTrackSize={LONG(720*scale),LONG(520*scale)};return 0;}
        case WM_SIZE:{const auto scale=float(GetDpiForWindow(window))/96;app->layout.width=LOWORD(second)/scale;app->layout.height=HIWORD(second)/scale;
          app->PositionPreferences();app->Scroll(0);return 0;}
        case WM_DPICHANGED:{const auto rectangle=reinterpret_cast<RECT*>(second);SetWindowPos(window,nullptr,rectangle->left,rectangle->top,rectangle->right-rectangle->left,rectangle->bottom-rectangle->top,SWP_NOACTIVATE|SWP_NOZORDER);return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:app->Paint();return 0;
        case WM_LBUTTONUP:{const auto scale=float(GetDpiForWindow(window))/96;app->Click(GET_X_LPARAM(second)/scale,GET_Y_LPARAM(second)/scale);return 0;}
        case WM_MOUSEMOVE:{const auto scale=float(GetDpiForWindow(window))/96;const float x=GET_X_LPARAM(second)/scale,y=GET_Y_LPARAM(second)/scale;
          int hover=-1;if(y>=app->layout.header() && y<app->layout.footer())for(size_t index=0;index<app->rows.size();++index)if(Inside(app->layout.card(index),x,y))hover=int(index);
          if(hover!=app->hover){app->hover=hover;InvalidateRect(window,nullptr,FALSE);}return 0;}
        case WM_MOUSEWHEEL:app->Scroll(-float(GET_WHEEL_DELTA_WPARAM(first))/WHEEL_DELTA*90);return 0;
        case WM_VSCROLL:{SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_ALL;GetScrollInfo(window,SB_VERT,&info);float value=app->layout.scroll;
          switch(LOWORD(first)){case SB_THUMBTRACK:value=float(info.nTrackPos);break;case SB_LINEUP:value-=30;break;case SB_LINEDOWN:value+=30;break;case SB_PAGEUP:value-=app->layout.viewport_height();break;case SB_PAGEDOWN:value+=app->layout.viewport_height();break;}app->Scroll(value-app->layout.scroll);return 0;}
        case WM_KEYDOWN:
          if((first==VK_UP || first==VK_NUMPAD8) && app->selected){app->Select(app->selected-1);}
          else if((first==VK_DOWN || first==VK_NUMPAD2) && app->selected+1<app->rows.size()){app->Select(app->selected+1);}
          else if(first==VK_RETURN || first==VK_SPACE) {
            if(app->focus>=4) {
              const auto rectangle=app->focus==10?app->layout.language_button():(app->focus==4?app->layout.config():(app->focus==5?app->layout.logs():(app->focus==6?app->layout.data():(app->focus==7?app->layout.refresh():(app->focus==8?app->layout.play():app->layout.detail())))));
              app->Click((rectangle.left+rectangle.right)/2,(rectangle.top+rectangle.bottom)/2);
            } else app->LaunchSelected();
          }
          else if(first==VK_F5)app->ManualRefresh();
          else if(first==VK_PRIOR)app->Scroll(-app->layout.viewport_height());
          else if(first==VK_NEXT)app->Scroll(app->layout.viewport_height());
          return 0;
        case WM_COMMAND:
          if(LOWORD(first)==kSaveControl && HIWORD(first)==BN_CLICKED) {app->SavePreferences();app->Refresh();return 0;}
          if((LOWORD(first)==kNicknameControl && HIWORD(first)==EN_CHANGE) ||
             (LOWORD(first)==kFullscreenControl && HIWORD(first)==BN_CLICKED)) {
            if(!app->loading_preferences){app->preferences_dirty=true;app->EnablePreferences();InvalidateRect(window,nullptr,FALSE);}return 0;
          }
          if((LOWORD(first)==kNicknameControl && HIWORD(first)==EN_SETFOCUS) || HIWORD(first)==BN_SETFOCUS) {
            app->focus=LOWORD(first)==kFullscreenControl?1:(LOWORD(first)==kNicknameControl?2:3);InvalidateRect(window,nullptr,FALSE);
          }break;
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
        case WM_TIMER:app->ChildTick();return 0;
        case WM_CLOSE:
          if(app->work && app->work->copying) {app->close_requested=true;app->work->cancelled=true;app->message=T(L"正在取消复制，请稍候…");InvalidateRect(window,nullptr,FALSE);return 0;}
          if(app->child){ShowWindow(window,SW_HIDE);return 0;}DestroyWindow(window);return 0;
        case WM_DESTROY:if(app->work)app->work->alive=false;
          if(app->form_font)DeleteObject(app->form_font);if(app->form_background)DeleteObject(app->form_background);
          PostQuitMessage(0);return 0;
      }
    }catch(const std::exception& error){Log(app->options.root,error.what());Error(window,error.what());return 0;}
    return DefWindowProcW(window,message,first,second);
  }
  Options options;
  HWND hwnd{};
  HWND fullscreen_control{},nickname_control{},save_control{};
  HFONT form_font{};
  HBRUSH form_background{};
  bool preferences_available{},preferences_dirty{},loading_preferences{};
  std::string preferences_text;
  std::wstring override_hint;
  Renderer renderer;
  Layout layout;
  std::vector<Row> rows;
  size_t selected{1};
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
};
void Preview(const Options& options) {
  if(fs::exists(options.preview))throw std::runtime_error("Preserve the existing launcher preview");
  language=options.language.value_or(cave::launcher::ReadLanguage(Ini::Load(options.root/"config.ini")));
  Renderer renderer;
  ComPtr<IWICBitmap> bitmap;Check(renderer.wic->CreateBitmap(1080,900,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap),"Create preview bitmap");
  ComPtr<ID2D1RenderTarget> target;Check(renderer.factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(),&target),"Create preview surface");
  auto rows=InspectRows(options.root,1);
  rows[0].inspection.state="missing_data";rows[1].inspection.state="ready";rows[2].inspection.state="not_integrated";
  const auto caption=std::to_wstring(Catalogue().size())+T(L" 部作品 · 按需安装游戏数据");
  target->BeginDraw();renderer.Draw(target.Get(),Layout{1080,900,0},rows,1,-1,false,caption,{},true);Check(target->EndDraw(),"Render preview");
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
  try {
    Check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"Initialize launcher COM");
    struct Apartment {~Apartment(){CoUninitialize();}} apartment;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto options=Arguments();
    if(!options.preview.empty()){Preview(options);return 0;}
    EnsureConfig(options.root);
    if(!options.game.empty()) {
      auto spec=PrepareLaunch(FindGame(options.game),options.root,Ini::Load(options.root/"config.ini"),options.profile,options.data);
      auto child=StartGame(spec);child.Wait();const auto code=child.ExitCode().value_or(1);return int(code);
    }
    App app(std::move(options));
    WNDCLASSEXW definition{};definition.cbSize=sizeof(definition);definition.style=CS_HREDRAW|CS_VREDRAW;definition.lpfnWndProc=App::Window;definition.hInstance=instance;
    definition.hCursor=LoadCursorW(nullptr,IDC_ARROW);definition.hIcon=LoadIconW(nullptr,IDI_APPLICATION);definition.lpszClassName=kWindowClass;
    if(!RegisterClassExW(&definition))throw std::runtime_error("Cannot register launcher window");
    RECT rectangle{0,0,1080,900};AdjustWindowRectEx(&rectangle,WS_OVERLAPPEDWINDOW|WS_VSCROLL,FALSE,0);
    const auto window=CreateWindowExW(0,kWindowClass,L"CAVE PC Collection",WS_OVERLAPPEDWINDOW|WS_VSCROLL,CW_USEDEFAULT,CW_USEDEFAULT,
        rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,nullptr,nullptr,instance,&app);
    if(!window)throw std::runtime_error("Cannot create launcher window");
    FitInitialWindow(window);
    ShowWindow(window,show);UpdateWindow(window);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(app.Input(message))continue;TranslateMessage(&message);DispatchMessageW(&message);}
    return 0;
  }catch(const std::exception& error){Error(nullptr,error.what());return 2;}
}
