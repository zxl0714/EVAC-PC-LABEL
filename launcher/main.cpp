#include "common/catalogue.h"
#include "common/ini.h"
#include "common/launch.h"
#include "common/settings.h"
#include "artwork.h"
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
constexpr UINT_PTR kChildTimer=1;
constexpr wchar_t kWindowClass[]=L"CavePCCollection.Launcher.v1";
void Check(HRESULT status,const char* operation) {
  if (FAILED(status)) throw std::runtime_error(std::string(operation)+" (HRESULT "+std::to_string(static_cast<unsigned>(status))+")");
}
std::wstring Wide(std::string_view text) {return FromUtf8(text).native();}
void Error(HWND window,std::string_view message) {
  std::wstring value;
  try {value=Wide(message.size()<16000?message:std::string_view("Error message is too long; check the log."));}
  catch (...) {value=L"无法显示详细信息，请检查日志。";}
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
std::vector<Row> InspectRows(const fs::path& root) {
  std::vector<Row> rows(Catalogue().size());
  try {
    const auto ini=Ini::Load(root/"config.ini");
    for (size_t index=0;index<rows.size();++index) {
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
  if (state=="ready") return L"已就绪";
  if (state=="needs_verification") return L"数据已找到 · 启动时校验";
  if (state=="missing_data") return L"未找到游戏数据";
  if (state=="incomplete_data") return L"游戏数据不完整";
  if (state=="unsupported_version") return L"数据版本不支持";
  if (state=="missing_runtime") return L"缺少运行组件";
  if (state=="invalid_config") return L"配置有误 · 请检查 config.ini";
  if (state=="invalid_path") return L"数据路径不可用";
  if (state=="not_integrated") return L"正在接入";
  if (state=="coming_soon") return L"敬请期待";
  return L"正在检测…";
}
bool CanPlay(const Inspection& inspection) {return inspection.state=="ready" || inspection.state=="needs_verification";}
bool CanChoose(const Game& game,const Inspection& inspection) {
  return game.integrated && !game.coming_soon && !CanPlay(inspection) && inspection.state!="missing_runtime";
}
std::string Details(const Inspection& inspection) {
  auto result=inspection.reason.empty()?inspection.state:inspection.reason;
  if (!inspection.problems.empty()) result+="\n"+inspection.problems.front();
  if (!inspection.missing_runtime.empty()) result+="\n"+inspection.missing_runtime.front();
  return result;
}
struct Layout {
  float width{},height{},scroll{};
  bool narrow() const {return width<800;}
  float margin() const {return narrow()?21.f:32.f;}
  float header() const {return 110;}
  float row_height() const {return narrow()?130.f:132.f;}
  float viewport_height() const {return std::max(0.f,height-header()-62.f);}
  float content_height() const {return float(Catalogue().size())*(row_height()+12)-12;}
  float max_scroll() const {return std::max(0.f,content_height()-viewport_height());}
  D2D1_RECT_F card(size_t index) const {const auto top=header()+float(index)*(row_height()+12)-scroll;return {margin(),top,width-margin(),top+row_height()};}
  D2D1_RECT_F action(size_t index) const {const auto r=card(index);return {r.right-133,r.bottom-56,r.right-23,r.bottom-20};}
  D2D1_RECT_F config() const {return {width-margin()-177,28,width-margin()-79,64};}
  D2D1_RECT_F logs() const {return {width-margin()-68,28,width-margin(),64};}
  D2D1_RECT_F data() const {return {margin(),height-45,margin()+100,height-14};}
  D2D1_RECT_F refresh() const {return {margin()+110,height-45,margin()+175,height-14};}
};
bool Inside(D2D1_RECT_F r,float x,float y) {return x>=r.left && x<r.right && y>=r.top && y<r.bottom;}
class Renderer {
 public:
  Renderer() {
    Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()),"Create Direct2D factory");
    Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write.GetAddressOf())),"Create text factory");
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"Create image factory");
    for (const auto size:{10.f,11.f,12.f,14.f,20.f,24.f,27.f}) {
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
    const auto background=play?0x2f5b42:(disabled?0xf3f5f1:0xffffff);
    ComPtr<ID2D1SolidColorBrush> fill,border;
    Check(target->CreateSolidColorBrush(D2D1::ColorF(background),&fill),"Create button fill");
    Check(target->CreateSolidColorBrush(D2D1::ColorF(play?0x2f5b42:0xd9dfd6),&border),"Create button border");
    const auto rounded=D2D1::RoundedRect(rectangle,8,8);target->FillRoundedRectangle(rounded,fill.Get());target->DrawRoundedRectangle(rounded,border.Get());
    Text(target,text,{rectangle.left+6,rectangle.top+10,rectangle.right-6,rectangle.bottom},12,play?0xffffff:(disabled?0x78857b:0x495a50),DWRITE_TEXT_ALIGNMENT_CENTER);
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
            std::wstring_view message,std::string_view running_game={},bool preview=false,int focus=0) {
    const bool running=!running_game.empty();
    const auto margin=layout.margin();target->Clear(D2D1::ColorF(0xf3f4f0));
    Text(target,L"C A V E",{margin,25,margin+130,65},27,0x23312d);
    Text(target,L"PC COLLECTION",{margin+(layout.narrow()?0:140),layout.narrow()?61.f:40.f,margin+320,80},10,0x6f7c76);
    if (!layout.narrow()) Text(target,L"熟悉的弹幕，新的入口。",{margin,70,margin+360,92},12,0x78827c);
    Button(target,layout.config(),L"配置文件");Button(target,layout.logs(),L"日志");
    target->PushAxisAlignedClip({0,layout.header()-2,layout.width,layout.height-62},D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t index=0;index<rows.size();++index) {
      const auto rectangle=layout.card(index);if(rectangle.bottom<layout.header() || rectangle.top>layout.height-62)continue;
      const auto& game=Catalogue()[index];const auto& inspection=rows[index].inspection;const bool soon=game.coming_soon;
      const auto color=soon?0xf6f7f4:0xffffff;
      ComPtr<ID2D1SolidColorBrush> background,border;
      Check(target->CreateSolidColorBrush(D2D1::ColorF(color),&background),"Create card fill");
      Check(target->CreateSolidColorBrush(D2D1::ColorF(selected==index?0x37644c:(hover==int(index)?0xa0b1a3:0xdee3db)),&border),"Create card border");
      auto round=D2D1::RoundedRect(rectangle,13,13);target->FillRoundedRectangle(round,background.Get());
      ComPtr<ID2D1RoundedRectangleGeometry> clip;Check(factory->CreateRoundedRectangleGeometry(round,&clip),"Create card clip");
      target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(),clip.Get()),nullptr);
      const float art_width=(rectangle.right-rectangle.left)*(layout.narrow()?.61f:.66f);
      const D2D1_RECT_F art{rectangle.right-art_width,rectangle.top,rectangle.right,rectangle.bottom};
      auto bitmap=Image(target,index);const auto size=bitmap->GetSize();const auto scale=std::max(art_width/size.width,layout.row_height()/size.height);
      const float y=rectangle.top-(size.height*scale-layout.row_height())*cave::launcher::kArtwork[index].focus_y;
      target->PushAxisAlignedClip(art,D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
      target->DrawBitmap(bitmap,{art.left,y,art.left+size.width*scale,y+size.height*scale},soon?.26f:1.f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
      target->PopAxisAlignedClip();
      const std::array stops{D2D1_GRADIENT_STOP{0,D2D1::ColorF(color,1)},D2D1_GRADIENT_STOP{.14f,D2D1::ColorF(color,.96f)},
          D2D1_GRADIENT_STOP{.35f,D2D1::ColorF(color,.47f)},D2D1_GRADIENT_STOP{.63f,D2D1::ColorF(color,0)},D2D1_GRADIENT_STOP{1,D2D1::ColorF(color,0)}};
      ComPtr<ID2D1GradientStopCollection> colors;Check(target->CreateGradientStopCollection(stops.data(),UINT32(stops.size()),&colors),"Create card gradient");
      ComPtr<ID2D1LinearGradientBrush> gradient;Check(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({art.left,art.top},{art.right,art.top}),colors.Get(),&gradient),"Create card mask");
      target->FillRectangle(art,gradient.Get());
      if (selected==index) {ComPtr<ID2D1SolidColorBrush> accent;Check(target->CreateSolidColorBrush(D2D1::ColorF(0x37644c),&accent),"Create selection accent");target->FillRectangle({rectangle.left,rectangle.top,rectangle.left+4,rectangle.bottom},accent.Get());}
      target->PopLayer();target->DrawRoundedRectangle(round,border.Get(),selected==index?2.f:1.f);
      const float left=rectangle.left+(layout.narrow()?20:25);
      Text(target,Wide(cave::launcher::kArtwork[index].kicker),{left,rectangle.top+20,rectangle.right-16,rectangle.top+36},10,soon?0x9ca59e:0x829087);
      Text(target,Wide(game.name),{left,rectangle.top+42,rectangle.right-16,rectangle.top+75},layout.narrow()?20.f:24.f,soon?0x737e76:0x23312d);
      const auto status=busy?L"正在检测…":Status(inspection);
      Text(target,status,{left,rectangle.top+83,rectangle.right-20,rectangle.bottom-14},11,CanPlay(inspection)?0x356c4b:(inspection.state=="missing_data"?0x90754f:0x7d8881));
      if (soon) Button(target,layout.action(index),L"COMING SOON",false,true);
      else {
        const bool playable=CanPlay(inspection),choose=CanChoose(game,inspection);
        const auto label=running && game.id==running_game?L"游戏运行中":(busy?L"请稍候…":(playable?(running?L"请先退出游戏":L"开始游戏"):(choose?L"选择数据目录":L"暂未开放")));
        Button(target,layout.action(index),label,playable&&!busy&&!running,busy||running||(!playable&&!choose));
      }
    }
    target->PopAxisAlignedClip();
    Button(target,layout.data(),L"游戏数据…",false,busy||running||Catalogue()[selected].coming_soon);
    Button(target,layout.refresh(),L"刷新",false,busy);
    if (focus) {
      const auto rectangle=focus==1?layout.config():(focus==2?layout.logs():(focus==3?layout.data():layout.refresh()));
      ComPtr<ID2D1SolidColorBrush> brush;Check(target->CreateSolidColorBrush(D2D1::ColorF(0x37644c),&brush),"Create focus indicator");
      target->DrawRoundedRectangle(D2D1::RoundedRect({rectangle.left-3,rectangle.top-3,rectangle.right+3,rectangle.bottom+3},10,10),brush.Get(),2);
    }
    Text(target,message,{margin+191,layout.height-36,layout.width-margin,layout.height-14},11,0x7d8881,DWRITE_TEXT_ALIGNMENT_TRAILING);
    if (preview) Text(target,L"视觉预览 · 安装状态为示例",{margin+200,layout.height-17,layout.width-margin,layout.height},10,0x939d94,DWRITE_TEXT_ALIGNMENT_TRAILING);
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
  std::vector<Row> rows;
  std::optional<LaunchSpec> launch;
  std::string error;
  bool starting{};
};
class App {
 public:
  explicit App(Options settings):options(std::move(settings)),rows(Catalogue().size()) {
    EnsureConfig(options.root);for(size_t i=0;i<rows.size();++i) rows[i].inspection.game=Catalogue()[i].id;
  }
  ~App() {if(work)work->alive=false;if(worker.joinable())worker.detach();}
  void Refresh(bool launch=false) {
    if (busy || child) return;
    if (worker.joinable()) worker.join();
    busy=true;work=std::make_shared<Work>();work->starting=launch;
    const auto task=work;const auto root=options.root;const auto id=std::string(Catalogue()[selected].id);const auto profile=options.profile;
    const auto window=hwnd;const auto data=options.data;
    worker=std::thread([task,root,id,profile,window,data] {
      try {
        task->rows=InspectRows(root);
        if (task->starting) task->launch=PrepareLaunch(FindGame(id),root,Ini::Load(root/"config.ini"),profile,data);
      } catch(const std::exception& error) {task->error=error.what();}
      if(task->alive) PostMessageW(window,kWorkDone,0,0);
    });
    InvalidateRect(hwnd,nullptr,FALSE);
  }
  void Complete() {
    if(worker.joinable())worker.join();busy=false;
    if (!work) return;
    if (!work->rows.empty()) rows=std::move(work->rows);
    try {
      if(!work->error.empty())throw std::runtime_error(work->error);
      if(work->launch) {
        child.emplace(StartGame(*work->launch));
        active_game=work->launch->game;
        message=L"正在运行："+Wide(FindGame(work->launch->game).name);SetTimer(hwnd,kChildTimer,250,nullptr);
        Log(options.root,"launch game="+work->launch->game+" pid="+std::to_string(child->pid()));
      } else {message=L"↑ ↓ 选择 · Enter 启动 · F5 刷新";}
    }catch(const std::exception& error){message=L"无法启动，请检查数据和运行组件";Log(options.root,error.what());Error(hwnd,error.what());}
    work.reset();InvalidateRect(hwnd,nullptr,FALSE);
  }
  void ChildTick() {
    if(!child)return;
    if(const auto code=child->ExitCode()) {
      Log(options.root,"child-exit code="+std::to_string(*code));
      message=*code?L"游戏异常退出，请查看该游戏的日志":L"游戏已退出";
      child.reset();active_game.clear();KillTimer(hwnd,kChildTimer);ShowWindow(hwnd,SW_SHOWNORMAL);SetForegroundWindow(hwnd);InvalidateRect(hwnd,nullptr,FALSE);
      if(*code)MessageBoxW(hwnd,L"游戏已退出。日志保存在 logs/<游戏 ID>/，启动器可以继续使用。",L"CAVE PC Collection",MB_OK|MB_ICONWARNING);
    }
  }
  void Choose() {
    if(busy||child||Catalogue()[selected].coming_soon)return;
    ComPtr<IFileOpenDialog> dialog;Check(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)),"Create data directory picker");
    DWORD flags{};Check(dialog->GetOptions(&flags),"Read directory picker options");
    Check(dialog->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST),"Configure directory picker");
    dialog->SetTitle(L"选择游戏原始数据目录（不会移动或修改文件）");
    const auto result=dialog->Show(hwnd);if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return;Check(result,"Select game data directory");
    ComPtr<IShellItem> item;Check(dialog->GetResult(&item),"Read selected directory");
    PWSTR value{};Check(item->GetDisplayName(SIGDN_FILESYSPATH,&value),"Read selected directory path");
    fs::path path(value);CoTaskMemFree(value);SetDataDirectory(options.root,Catalogue()[selected],path);
    Log(options.root,"data-directory-updated game="+std::string(Catalogue()[selected].id));Refresh();
  }
  void EnsureVisible() {
    const auto top=float(selected)*(layout.row_height()+12),bottom=top+layout.row_height();
    if(top<layout.scroll)layout.scroll=top;
    if(bottom>layout.scroll+layout.viewport_height())layout.scroll=bottom-layout.viewport_height();
    Scroll(0);
  }
  void Scroll(float delta) {
    layout.scroll=std::clamp(layout.scroll+delta,0.f,layout.max_scroll());
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,int(layout.content_height()),UINT(layout.viewport_height()),int(layout.scroll),0};
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
      surface->BeginDraw();renderer.Draw(surface.Get(),layout,rows,selected,hover,busy,message,active_game,false,focus);
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
    focus=0;
    InvalidateRect(hwnd,nullptr,FALSE);
    if(Inside(layout.config(),x,y)) {OpenPath(hwnd,EnsureConfig(options.root));return;}
    if(Inside(layout.logs(),x,y)) {const auto logs=LogDirectory(options.root);if(!fs::exists(logs))fs::create_directories(logs);OpenPath(hwnd,logs);return;}
    if(Inside(layout.data(),x,y)) {Choose();return;}
    if(Inside(layout.refresh(),x,y)) {Refresh();return;}
    if(y<layout.header() || y>layout.height-62)return;
    for(size_t index=0;index<rows.size();++index) if(Inside(layout.card(index),x,y)) {
      selected=index;InvalidateRect(hwnd,nullptr,FALSE);
      if(Inside(layout.action(index),x,y) && !busy && !child) {
        if(CanPlay(rows[index].inspection))Refresh(true);
        else if(CanChoose(Catalogue()[index],rows[index].inspection))Choose();
        else if(!Catalogue()[index].coming_soon)Error(hwnd,Details(rows[index].inspection));
      }return;
    }
  }
  static LRESULT CALLBACK Window(HWND window,UINT message,WPARAM first,LPARAM second) {
    App* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(second)->lpCreateParams);app->hwnd=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(window,message,first,second);
    try {
      switch(message) {
        case WM_CREATE:app->Refresh();return 0;
        case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(second);const auto scale=float(GetDpiForWindow(window))/96;info->ptMinTrackSize={LONG(720*scale),LONG(520*scale)};return 0;}
        case WM_SIZE:{const auto scale=float(GetDpiForWindow(window))/96;app->layout.width=LOWORD(second)/scale;app->layout.height=HIWORD(second)/scale;
          app->Scroll(0);return 0;}
        case WM_DPICHANGED:{const auto rectangle=reinterpret_cast<RECT*>(second);SetWindowPos(window,nullptr,rectangle->left,rectangle->top,rectangle->right-rectangle->left,rectangle->bottom-rectangle->top,SWP_NOACTIVATE|SWP_NOZORDER);return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:app->Paint();return 0;
        case WM_LBUTTONUP:{const auto scale=float(GetDpiForWindow(window))/96;app->Click(GET_X_LPARAM(second)/scale,GET_Y_LPARAM(second)/scale);return 0;}
        case WM_MOUSEMOVE:{const auto scale=float(GetDpiForWindow(window))/96;const float x=GET_X_LPARAM(second)/scale,y=GET_Y_LPARAM(second)/scale;
          int hover=-1;if(y>=app->layout.header() && y<app->layout.height-62)for(size_t index=0;index<app->rows.size();++index)if(Inside(app->layout.card(index),x,y))hover=int(index);
          if(hover!=app->hover){app->hover=hover;InvalidateRect(window,nullptr,FALSE);}return 0;}
        case WM_MOUSEWHEEL:app->Scroll(-float(GET_WHEEL_DELTA_WPARAM(first))/WHEEL_DELTA*90);return 0;
        case WM_VSCROLL:{SCROLLINFO info{};info.cbSize=sizeof(info);info.fMask=SIF_ALL;GetScrollInfo(window,SB_VERT,&info);float value=app->layout.scroll;
          switch(LOWORD(first)){case SB_THUMBTRACK:value=float(info.nTrackPos);break;case SB_LINEUP:value-=30;break;case SB_LINEDOWN:value+=30;break;case SB_PAGEUP:value-=app->layout.viewport_height();break;case SB_PAGEDOWN:value+=app->layout.viewport_height();break;}app->Scroll(value-app->layout.scroll);return 0;}
        case WM_KEYDOWN:
          if(first==VK_TAB){app->focus=(app->focus+((GetKeyState(VK_SHIFT)&0x8000)?4:1))%5;InvalidateRect(window,nullptr,FALSE);}
          else if((first==VK_UP || first==VK_NUMPAD8) && app->selected){app->focus=0;--app->selected;app->EnsureVisible();}
          else if((first==VK_DOWN || first==VK_NUMPAD2) && app->selected+1<app->rows.size()){app->focus=0;++app->selected;app->EnsureVisible();}
          else if(first==VK_RETURN || first==VK_SPACE) {
            if(app->focus) {
              const auto rectangle=app->focus==1?app->layout.config():(app->focus==2?app->layout.logs():(app->focus==3?app->layout.data():app->layout.refresh()));
              app->Click((rectangle.left+rectangle.right)/2,(rectangle.top+rectangle.bottom)/2);
            } else if(!app->busy && !app->child) {
              if(CanPlay(app->rows[app->selected].inspection))app->Refresh(true);
              else if(CanChoose(Catalogue()[app->selected],app->rows[app->selected].inspection))app->Choose();
              else Error(window,Details(app->rows[app->selected].inspection));
            }
          }
          else if(first==VK_F5)app->Refresh();
          else if(first==VK_PRIOR)app->Scroll(-app->layout.viewport_height());
          else if(first==VK_NEXT)app->Scroll(app->layout.viewport_height());
          return 0;
        case kWorkDone:app->Complete();return 0;
        case WM_TIMER:app->ChildTick();return 0;
        case WM_CLOSE:if(app->child){ShowWindow(window,SW_HIDE);return 0;}DestroyWindow(window);return 0;
        case WM_DESTROY:if(app->work)app->work->alive=false;PostQuitMessage(0);return 0;
      }
    }catch(const std::exception& error){Log(app->options.root,error.what());Error(window,error.what());return 0;}
    return DefWindowProcW(window,message,first,second);
  }
  Options options;
  HWND hwnd{};
  Renderer renderer;
  Layout layout;
  std::vector<Row> rows;
  size_t selected{1};
  int hover{-1};
  int focus{};
  bool busy{};
  bool paint_recorded{};
  std::wstring message=L"正在检测游戏数据…";
  ComPtr<ID2D1DCRenderTarget> surface;
  std::shared_ptr<Work> work;
  std::thread worker;
  std::optional<ChildProcess> child;
  std::string active_game;
};
void Preview(const Options& options) {
  if(fs::exists(options.preview))throw std::runtime_error("Preserve the existing launcher preview");
  Renderer renderer;
  ComPtr<IWICBitmap> bitmap;Check(renderer.wic->CreateBitmap(1080,900,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap),"Create preview bitmap");
  ComPtr<ID2D1RenderTarget> target;Check(renderer.factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(),&target),"Create preview surface");
  auto rows=InspectRows(options.root);
  rows[0].inspection.state="missing_data";rows[1].inspection.state="ready";rows[2].inspection.state="not_integrated";
  target->BeginDraw();renderer.Draw(target.Get(),Layout{1080,900,0},rows,1,-1,false,L"5 部作品 · 按需安装游戏数据",{},true);Check(target->EndDraw(),"Render preview");
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
    ShowWindow(window,show);UpdateWindow(window);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    return 0;
  }catch(const std::exception& error){Error(nullptr,error.what());return 2;}
}
