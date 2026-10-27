#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "extract.h"

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
namespace {
constexpr int select_id=101, open_id=102;
constexpr UINT done_message=WM_APP+1;
HWND window{}, status{}, select_button{}, open_button{}, bar{};
HFONT font{};
std::thread worker;
bool running{}, close_pending{};
fs::path completed;
struct Result { fs::path output; std::wstring error; };
std::wstring Widen(const std::string& text) {
  const int length=MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),nullptr,0);
  if(!length)return L"Extraction failed.";
  std::wstring result(length,L'\0');
  MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),result.data(),length);
  return result;
}
fs::path Choose(bool folder) {
  ComPtr<IFileOpenDialog> dialog;
  if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))
    throw std::runtime_error("Cannot open file picker.");
  DWORD flags{};dialog->GetOptions(&flags);
  dialog->SetOptions(flags|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|(folder?FOS_PICKFOLDERS:FOS_FILEMUSTEXIST));
  dialog->SetTitle(folder?L"Choose where to create the extracted folder":L"Choose the XBLA package (unpack ZIP/RAR first)");
  COMDLG_FILTERSPEC filter{L"All files",L"*.*"};
  if(!folder)dialog->SetFileTypes(1,&filter);
  if(FAILED(dialog->Show(window)))return {};
  ComPtr<IShellItem> item;PWSTR text{};
  if(FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&text)))return {};
  fs::path result(text);CoTaskMemFree(text);return result;
}
void Begin() {
  auto input=Choose(false);if(input.empty())return;
  auto parent=Choose(true);if(parent.empty())return;
  auto output=parent/L"xbla-extracted";
  for(unsigned n=2;fs::exists(output);++n)output=parent/(L"xbla-extracted-"+std::to_wstring(n));
  completed.clear();EnableWindow(open_button,FALSE);EnableWindow(select_button,FALSE);
  SetWindowTextW(status,L"Verifying and extracting the package...");
  SendMessageW(bar,PBM_SETMARQUEE,TRUE,40);running=true;
  try {
    worker=std::thread([input,output] {
      auto result=std::make_unique<Result>();
      try { xblaextract::Extract(input,output);result->output=output; }
      catch(const std::exception& error) { result->error=Widen(error.what()); }
      if(PostMessageW(window,done_message,0,reinterpret_cast<LPARAM>(result.get())))result.release();
    });
  } catch(...) {
    running=false;EnableWindow(select_button,TRUE);SendMessageW(bar,PBM_SETMARQUEE,FALSE,0);throw;
  }
}
HWND Control(const wchar_t* type,const wchar_t* text,DWORD style,int id,int x,int y,int w,int h) {
  auto child=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(INT_PTR(id)),nullptr,nullptr);
  SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return child;
}
LRESULT CALLBACK Procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
  try {
    switch(message) {
    case WM_CREATE:
      window=hwnd;
      font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
      Control(L"STATIC",L"XBLA package extraction",0,0,24,20,570,26);
      status=Control(L"STATIC",L"Select the content package inside your ZIP/RAR, then choose an output location.",0,0,24,60,570,76);
      bar=Control(PROGRESS_CLASSW,L"",PBS_MARQUEE,0,24,144,570,10);
      select_button=Control(L"BUTTON",L"Select package...",BS_DEFPUSHBUTTON|WS_TABSTOP,select_id,24,180,180,36);
      open_button=Control(L"BUTTON",L"Open game files",WS_TABSTOP,open_id,414,180,180,36);
      EnableWindow(open_button,FALSE);SetFocus(select_button);return 0;
    case WM_COMMAND:
      if(HIWORD(wp)==BN_CLICKED && LOWORD(wp)==select_id && !running)Begin();
      if(HIWORD(wp)==BN_CLICKED && LOWORD(wp)==open_id && !completed.empty())
        ShellExecuteW(window,L"open",completed.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
      return 0;
    case done_message: {
      std::unique_ptr<Result> result(reinterpret_cast<Result*>(lp));
      if(worker.joinable())worker.join();running=false;
      completed=result->output;EnableWindow(select_button,TRUE);EnableWindow(open_button,!completed.empty());
      SendMessageW(bar,PBM_SETMARQUEE,FALSE,0);
      const auto text=result->error.empty()?L"Complete. Copy the contents of this folder into game-data/guwange/.":result->error.c_str();
      SetWindowTextW(status,text);
      if(close_pending)DestroyWindow(window);return 0;
    }
    case WM_CLOSE:
      if(running) {close_pending=true;SetWindowTextW(status,L"Finishing extraction before closing...");}
      else DestroyWindow(window);
      return 0;
    case WM_CTLCOLORSTATIC:
      SetBkColor(reinterpret_cast<HDC>(wp),GetSysColor(COLOR_WINDOW));
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    case WM_DESTROY:DeleteObject(font);PostQuitMessage(0);return 0;
    }
  } catch(const std::exception& error) {MessageBoxW(hwnd,Widen(error.what()).c_str(),L"Extract XBLA",MB_ICONERROR);}
  return DefWindowProcW(hwnd,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
  int argc{};auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
  std::vector<std::wstring> args;
  if(!argv)return 2;
  for(int i=1;i<argc;++i)args.emplace_back(argv[i]);LocalFree(argv);
  if(!args.empty()) {
    if(args.size()!=5 || args[0]!=L"--extract" || args[1]!=L"--input" || args[3]!=L"--output")return 2;
    try {xblaextract::Extract(args[2],args[4]);return 0;}catch(...){return 1;}
  }
  if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 1;
  INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_PROGRESS_CLASS};InitCommonControlsEx(&controls);
  WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=Procedure;cls.lpszClassName=L"EvacXblaExtract";
  cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
  cls.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassW(&cls);
  constexpr DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
  RECT bounds{0,0,618,244};AdjustWindowRectEx(&bounds,style,FALSE,0);
  window=CreateWindowExW(0,cls.lpszClassName,L"Extract XBLA — EVAC PC LABEL",style,CW_USEDEFAULT,CW_USEDEFAULT,
    bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
  if(!window){CoUninitialize();return 1;}
  ShowWindow(window,show);UpdateWindow(window);
  MSG message{};
  while(GetMessageW(&message,nullptr,0,0)>0)
    if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
  if(worker.joinable())worker.join();CoUninitialize();return 0;
}
