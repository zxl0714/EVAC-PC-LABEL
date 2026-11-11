#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <wrl/client.h>
#include "extract.h"
#include <memory>
#include <thread>
#include <stdexcept>
#include <vector>

using namespace xboxextract;
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT progress_message = WM_APP + 1, done_message = WM_APP + 2;
constexpr int input_id = 101, output_id = 102, input_browse = 103, output_browse = 104,
              start_id = 105, open_id = 106, cancel_id = 107;
HWND window, input_box, output_box, status_box, bar, open_button, start_button, cancel_button;
HFONT font;
std::atomic_bool cancel_requested{false};
std::thread worker;
bool running = false, close_pending = false;
fs::path completed;
std::wstring contents(HWND control) {
  std::wstring text(GetWindowTextLengthW(control) + 1, L'\0');
  text.resize(GetWindowTextW(control, text.data(), static_cast<int>(text.size())));
  return text;
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
  dialog->SetTitle(folder ? L"Choose the parent folder" : L"Select an Xbox 360 ISO");
  COMDLG_FILTERSPEC filters[] = {{L"Xbox 360 ISO", L"*.iso"}, {L"All files", L"*.*"}};
  if (!folder) dialog->SetFileTypes(2, filters);
  if (FAILED(dialog->Show(window))) return {};
  ComPtr<IShellItem> item; PWSTR path = nullptr;
  if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
  fs::path result(path); CoTaskMemFree(path); return result;
}
fs::path new_output(const fs::path& parent, const fs::path& input) {
  auto name = input.empty() ? L"xbox360-extracted" : input.stem().wstring() + L"-extracted";
  auto result = parent / name;
  for (unsigned index = 2; fs::exists(result); ++index) result = parent / (name + L"-" + std::to_wstring(index));
  return result;
}
void set_running(bool value) {
  running = value;
  for (int id : {input_id, output_id, input_browse, output_browse, start_id}) EnableWindow(GetDlgItem(window, id), !value);
  EnableWindow(cancel_button, value);
  EnableWindow(open_button, !value && !completed.empty());
  if (value) SendMessageW(bar, PBM_SETPOS, 0, 0);
}
struct Result { fs::path game; std::wstring error; };
void begin() {
  Options options{contents(input_box), contents(output_box)};
  if (options.input.empty() || options.output.empty()) {
    SetWindowTextW(status_box, L"Choose an ISO file and output folder."); return;
  }
  completed.clear(); cancel_requested = false; set_running(true);
  SetWindowTextW(status_box, L"Reading disc directory…");
  try {
    worker = std::thread([options] {
    auto result = std::make_unique<Result>();
    try {
      result->game = Extract(options, [](const Progress& state) {
        auto message = std::make_unique<Progress>(state);
        if (PostMessageW(window, progress_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
      }, cancel_requested);
    } catch (const std::exception& error) { result->error = widen(error.what()); }
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
      control(L"STATIC", L"Xbox 360 ISO extraction", 0, 0, 24, 20, 600, 26);
      control(L"STATIC", L"ISO file", 0, 0, 24, 61, 110, 24);
      input_box = control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, input_id, 24, 88, 530, 28);
      control(L"BUTTON", L"Browse…", WS_TABSTOP, input_browse, 566, 87, 100, 30);
      control(L"STATIC", L"Output folder (created for this extraction)", 0, 0, 24, 133, 620, 24);
      output_box = control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, output_id, 24, 160, 530, 28);
      control(L"BUTTON", L"Location…", WS_TABSTOP, output_browse, 566, 159, 100, 30);
      bar = control(PROGRESS_CLASSW, L"", 0, 0, 24, 215, 642, 10);
      status_box = control(L"STATIC", L"Choose an Xbox 360 game ISO to begin.", 0, 0, 24, 245, 642, 66);
      open_button = control(L"BUTTON", L"Open game files", WS_TABSTOP, open_id, 24, 326, 160, 36);
      cancel_button = control(L"BUTTON", L"Cancel", WS_TABSTOP, cancel_id, 434, 326, 106, 36);
      start_button = control(L"BUTTON", L"Extract", BS_DEFPUSHBUTTON | WS_TABSTOP, start_id, 552, 326, 114, 36);
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
      std::unique_ptr<Progress> state(reinterpret_cast<Progress*>(lp));
      const auto percent = state->total ? static_cast<int>(state->done * 100 / state->total) : 0;
      SendMessageW(bar, PBM_SETPOS, percent, 0);
      const auto text = std::to_wstring(percent) + L"%  —  " + state->file;
      SetWindowTextW(status_box, text.c_str()); return 0;
    }
    case done_message: {
      std::unique_ptr<Result> result(reinterpret_cast<Result*>(lp));
      if (worker.joinable()) worker.join();
      completed = result->game; set_running(false);
      std::wstring text = result->error.empty()
        ? std::wstring(L"Complete. Open game files to copy them into game-data.")
        : result->error;
      SetWindowTextW(status_box, text.c_str());
      if (close_pending) DestroyWindow(window);
      return 0;
    }
    case WM_CLOSE:
      if (running) {
        if (MessageBoxW(window,L"Cancel extraction and close?",L"Extract Xbox 360",MB_YESNO | MB_ICONQUESTION) == IDYES) {
          close_pending = true; cancel_requested = true; EnableWindow(cancel_button, FALSE);
        }
      } else DestroyWindow(window);
      return 0;
    case WM_CTLCOLORSTATIC:
      SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    case WM_DESTROY: DeleteObject(font); PostQuitMessage(0); return 0;
    }
  } catch (const std::exception& error) { MessageBoxW(hwnd, widen(error.what()).c_str(), L"Extract Xbox 360", MB_ICONERROR); }
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
    Options options;
    if (args.size() != 5) return 2;
    for (size_t i = 1; i + 1 < args.size(); i += 2) {
      if (args[i] == L"--input") options.input = args[i+1];
      else if (args[i] == L"--output") options.output = args[i+1];
      else return 2;
    }
    try { Extract(options, {}, cancel_requested); return 0; } catch (...) { return 1; }
  }
  if (!args.empty() && args != std::vector<std::wstring>{L"--english"}) return 2;
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS}; InitCommonControlsEx(&controls);
  WNDCLASSW cls{}; cls.hInstance=instance; cls.lpfnWndProc=procedure; cls.lpszClassName=L"EvacXbox360Extract";
  cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
  cls.hIcon=LoadIconW(nullptr,IDI_APPLICATION); RegisterClassW(&cls);
  RECT bounds{0,0,690,388}; AdjustWindowRectEx(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
  window=CreateWindowExW(0,cls.lpszClassName,L"Extract Xbox 360 — EVAC PC LABEL",WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
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
