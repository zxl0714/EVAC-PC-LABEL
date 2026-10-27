#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <wrl/client.h>
#include "extract.h"
#include <memory>
#include <thread>

using namespace ps4extract;
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT progress_message = WM_APP + 1, done_message = WM_APP + 2;
constexpr int input_id = 101, output_id = 102, input_browse = 103, output_browse = 104,
              start_id = 105, open_id = 106, cancel_id = 107, pass_id = 108;
bool chinese = false;
HWND window, input_box, output_box, pass_box, status_box, bar, open_button, start_button, cancel_button;
HFONT font;
std::atomic_bool cancel_requested{false};
std::thread worker;
bool running = false, close_pending = false;
fs::path completed;
const wchar_t* tr(const wchar_t* en, const wchar_t* cn) { return chinese ? cn : en; }
std::wstring localize(const std::wstring& text) {
  const auto split = text.find(L" / ");
  return split == std::wstring::npos ? text : (chinese ? text.substr(split + 3) : text.substr(0, split));
}
std::wstring contents(HWND control) {
  std::wstring text(GetWindowTextLengthW(control) + 1, L'\0');
  text.resize(GetWindowTextW(control, text.data(), static_cast<int>(text.size())));
  return text;
}
fs::path executable() {
  std::wstring path(32768, L'\0');
  const auto count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (!count || count == path.size()) throw Error{L"Cannot find executable path / 无法定位程序目录"};
  path.resize(count); return path;
}
std::wstring widen(const std::string& text) {
  int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (!length) return L"Unexpected error / 未知错误";
  std::wstring result(length, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), length);
  result.resize(length - 1); return result;
}
fs::path choose(bool folder) {
  ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return {};
  DWORD flags{}; dialog->GetOptions(&flags);
  dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
  dialog->SetTitle(folder ? tr(L"Choose the parent folder", L"选择输出位置") : tr(L"Select a PS4 PKG", L"选择 PS4 PKG"));
  COMDLG_FILTERSPEC filters[] = {{L"PS4 PKG", L"*.pkg"}, {tr(L"All files", L"所有文件"), L"*.*"}};
  if (!folder) dialog->SetFileTypes(2, filters);
  if (FAILED(dialog->Show(window))) return {};
  ComPtr<IShellItem> item; PWSTR path = nullptr;
  if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
  fs::path result(path); CoTaskMemFree(path); return result;
}
fs::path new_output(const fs::path& parent, const fs::path& input) {
  auto name = input.empty() ? L"ps4-extracted" : input.stem().wstring() + L"-extracted";
  auto result = parent / name;
  for (unsigned index = 2; fs::exists(result); ++index) result = parent / (name + L"-" + std::to_wstring(index));
  return result;
}
void set_running(bool value) {
  running = value;
  for (int id : {input_id, output_id, input_browse, output_browse, pass_id, start_id}) EnableWindow(GetDlgItem(window, id), !value);
  EnableWindow(cancel_button, value);
  EnableWindow(open_button, !value && !completed.empty());
  SendMessageW(bar, PBM_SETMARQUEE, value, 40);
}
struct Result { fs::path game; std::wstring error; };
void begin() {
  Options options{contents(input_box), contents(output_box), executable().parent_path() / L"pkgtool" / L"PkgTool.exe", contents(pass_box)};
  if (options.input.empty() || options.output.empty()) {
    SetWindowTextW(status_box, tr(L"Choose a PKG file and output folder.", L"请选择 PKG 文件和输出目录。")); return;
  }
  completed.clear(); cancel_requested = false; set_running(true);
  SetWindowTextW(status_box, tr(L"Checking package…", L"正在检查文件…"));
  try {
    worker = std::thread([options] {
    auto result = std::make_unique<Result>();
    try {
      result->game = extract(options, [](const std::wstring& text) {
        auto message = std::make_unique<std::wstring>(localize(text));
        if (PostMessageW(window, progress_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
      }, cancel_requested);
    } catch (const Error& error) { result->error = localize(error.message); }
      catch (const std::exception& error) { result->error = widen(error.what()); }
    if (PostMessageW(window, done_message, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
    });
  } catch (...) {
    set_running(false);
    throw;
  }
}
HWND control(const wchar_t* kind, const wchar_t* label, DWORD style, int id, int x, int y, int w, int h) {
  auto child = CreateWindowExW(kind == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0,
    kind, label, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window, reinterpret_cast<HMENU>(INT_PTR(id)), nullptr, nullptr);
  SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  return child;
}
LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  try {
    switch (message) {
    case WM_CREATE: {
      window = hwnd;
      font = CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
      control(L"STATIC", tr(L"PS4 PKG extraction", L"PS4 PKG 解包"), 0, 0, 24, 20, 600, 26);
      control(L"STATIC", tr(L"PKG file", L"PKG 文件"), 0, 0, 24, 61, 110, 24);
      input_box = control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, input_id, 24, 88, 530, 28);
      control(L"BUTTON", tr(L"Browse…", L"选择…"), WS_TABSTOP, input_browse, 566, 87, 100, 30);
      control(L"STATIC", tr(L"Output folder (created for this extraction)", L"输出目录（解包时新建）"), 0, 0, 24, 133, 620, 24);
      output_box = control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, output_id, 24, 160, 530, 28);
      control(L"BUTTON", tr(L"Location…", L"位置…"), WS_TABSTOP, output_browse, 566, 159, 100, 30);
      control(L"STATIC", tr(L"PKG passcode", L"PKG passcode"), 0, 0, 24, 207, 120, 24);
      pass_box = control(L"EDIT", L"00000000000000000000000000000000", ES_AUTOHSCROLL | WS_TABSTOP, pass_id, 152, 203, 402, 28);
      SendMessageW(pass_box, EM_SETLIMITTEXT, 32, 0);
      control(L"STATIC", tr(L"Use the default unless your PKG has a different passcode.", L"通常保留默认值；它不是 ZIP / RAR 的解压密码。"), 0, 0, 24, 245, 640, 25);
      bar = control(PROGRESS_CLASSW, L"", PBS_MARQUEE, 0, 24, 290, 642, 10);
      status_box = control(L"STATIC", tr(L"Choose a base or update PKG to begin.", L"选择本体或更新 PKG 开始解包。"), 0, 0, 24, 316, 642, 66);
      open_button = control(L"BUTTON", tr(L"Open game files", L"打开游戏文件"), WS_TABSTOP, open_id, 24, 402, 160, 36);
      cancel_button = control(L"BUTTON", tr(L"Cancel", L"取消"), WS_TABSTOP, cancel_id, 434, 402, 106, 36);
      start_button = control(L"BUTTON", tr(L"Extract", L"开始解包"), BS_DEFPUSHBUTTON | WS_TABSTOP, start_id, 552, 402, 114, 36);
      set_running(false); SetFocus(GetDlgItem(window, input_browse)); return 0;
    }
    case WM_COMMAND:
      if (HIWORD(wp) != BN_CLICKED) break;
      switch (LOWORD(wp)) {
      case input_browse: {
        auto input = choose(false);
        if (!input.empty()) {
          SetWindowTextW(input_box, input.c_str());
          SetWindowTextW(output_box, new_output(input.parent_path(), input).c_str());
        } break;
      }
      case output_browse: {
        auto parent = choose(true);
        if (!parent.empty()) SetWindowTextW(output_box, new_output(parent, contents(input_box)).c_str());
        break;
      }
      case start_id: if (!running) begin(); break;
      case cancel_id: cancel_requested = true; EnableWindow(cancel_button, FALSE); break;
      case open_id: if (!completed.empty()) ShellExecuteW(window,L"open",completed.c_str(),nullptr,nullptr,SW_SHOWNORMAL); break;
      } return 0;
    case progress_message: {
      std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
      SetWindowTextW(status_box, text->c_str()); return 0;
    }
    case done_message: {
      std::unique_ptr<Result> result(reinterpret_cast<Result*>(lp));
      if (worker.joinable()) worker.join();
      completed = result->game; set_running(false);
      std::wstring text = result->error.empty()
        ? std::wstring(tr(L"Complete. Open game files to copy the extracted contents.", L"解包完成。点击“打开游戏文件”复制提取的内容。"))
        : result->error;
      SetWindowTextW(status_box, text.c_str());
      if (close_pending) DestroyWindow(window);
      return 0;
    }
    case WM_CLOSE:
      if (running) {
        if (MessageBoxW(window,tr(L"Cancel extraction and close?",L"取消解包并关闭？"),L"Extract PS4",MB_YESNO | MB_ICONQUESTION) == IDYES) {
          close_pending = true; cancel_requested = true; EnableWindow(cancel_button, FALSE);
        }
      } else DestroyWindow(window);
      return 0;
    case WM_CTLCOLORSTATIC:
      SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    case WM_DESTROY: DeleteObject(font); PostQuitMessage(0); return 0;
    }
  } catch (const Error& error) { MessageBoxW(hwnd, localize(error.message).c_str(), L"Extract PS4", MB_ICONERROR); }
    catch (const std::exception& error) { MessageBoxW(hwnd, widen(error.what()).c_str(), L"Extract PS4", MB_ICONERROR); }
  return DefWindowProcW(hwnd,message,wp,lp);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  // The optional command mode exercises the exact same extraction core without a GUI.
  int argc = 0;
  auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::vector<std::wstring> args;
  for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
  LocalFree(argv);
  if (!args.empty() && args[0] == L"--extract") {
    Options options; options.tool = executable().parent_path() / L"pkgtool" / L"PkgTool.exe";
    for (size_t i = 1; i + 1 < args.size(); i += 2) {
      if (args[i] == L"--input") options.input = args[i+1];
      else if (args[i] == L"--output") options.output = args[i+1];
      else if (args[i] == L"--pkgtool") options.tool = args[i+1];
      else if (args[i] == L"--passcode") options.passcode = args[i+1];
      else return 2;
    }
    try { extract(options, [](const auto&) {}, cancel_requested); return 0; } catch (...) { return 1; }
  }
  if (args == std::vector<std::wstring>{L"--english"}) chinese = false;
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS}; InitCommonControlsEx(&controls);
  WNDCLASSW cls{}; cls.hInstance=instance; cls.lpfnWndProc=procedure; cls.lpszClassName=L"EvacPs4Extract";
  cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
  cls.hIcon=LoadIconW(nullptr,IDI_APPLICATION); RegisterClassW(&cls);
  RECT bounds{0,0,690,464}; AdjustWindowRectEx(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
  window=CreateWindowExW(0,cls.lpszClassName,L"Extract PS4 — EVAC PC LABEL",WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
    CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
  if (!window) { CoUninitialize(); return 1; }
  ShowWindow(window,show); UpdateWindow(window);
  MSG message{};
  while (GetMessageW(&message,nullptr,0,0)>0) {
    if (!IsDialogMessageW(window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
  }
  if (worker.joinable()) { cancel_requested=true; worker.join(); }
  CoUninitialize(); return 0;
}
