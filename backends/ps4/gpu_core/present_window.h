#pragma once
#include <Windows.h>
#include <atomic>
#include <future>
#include <stdexcept>
#include <thread>

namespace Ketsui::GpuCore {
class PresentWindow {
  static constexpr UINT Finish = WM_APP + 1, ResizeMessage = WM_APP + 2,
                        ShowMessage = WM_APP + 3;
  std::atomic<HWND> window{};
  std::atomic<bool> closed{};
  WINDOWPLACEMENT saved{sizeof(saved)};
  bool fullscreen{};
  std::jthread thread;
  void Toggle(HWND value) {
    if (!fullscreen) {
      MONITORINFO monitor{sizeof(monitor)};
      if (!GetWindowPlacement(value, &saved) ||
          !GetMonitorInfoW(MonitorFromWindow(value, MONITOR_DEFAULTTONEAREST),
                           &monitor)) {
        return;
      }
      SetWindowLongPtrW(value, GWL_STYLE, WS_POPUP | WS_VISIBLE);
      const auto &r = monitor.rcMonitor;
      SetWindowPos(value, nullptr, r.left, r.top, r.right - r.left,
                   r.bottom - r.top, SWP_FRAMECHANGED | SWP_NOZORDER);
    } else {
      SetWindowLongPtrW(value, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
      SetWindowPlacement(value, &saved);
      SetWindowPos(value, nullptr, 0, 0, 0, 0,
                   SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE);
    }
    fullscreen = !fullscreen;
  }
  static LRESULT CALLBACK Procedure(HWND value, UINT message, WPARAM w,
                                    LPARAM l) {
    auto *self = reinterpret_cast<PresentWindow *>(
        GetWindowLongPtrW(value, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<PresentWindow *>(
          reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
      SetWindowLongPtrW(value, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) {
      return DefWindowProcW(value, message, w, l);
    }
    if (message == WM_CLOSE) {
      self->closed = true;
      ShowWindow(value, SW_HIDE);
      return 0;
    }
    if (message == Finish) {
      DestroyWindow(value);
      return 0;
    }
    if (message == WM_DESTROY) {
      PostQuitMessage(0);
      return 0;
    }
    if (message == WM_KEYDOWN && w == VK_F11) {
      self->Toggle(value);
      return 0;
    }
    if (message == ShowMessage) {
      ShowWindow(value, static_cast<int>(w));
      return 0;
    }
    if (message == WM_DPICHANGED) {
      const auto *r = reinterpret_cast<RECT *>(l);
      SetWindowPos(value, nullptr, r->left, r->top, r->right - r->left,
                   r->bottom - r->top, SWP_NOZORDER);
      return 0;
    }
    if (message == ResizeMessage) {
      RECT rect{0, 0, static_cast<LONG>(w), static_cast<LONG>(l)};
      if (!AdjustWindowRectExForDpi(
              &rect, static_cast<DWORD>(GetWindowLongPtrW(value, GWL_STYLE)),
              false, 0, GetDpiForWindow(value))) {
        return 1;
      }
      return SetWindowPos(value, nullptr, 0, 0, rect.right - rect.left,
                          rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER)
                 ? 0
                 : 1;
    }
    return DefWindowProcW(value, message, w, l);
  }

public:
  PresentWindow() {
    std::promise<HWND> created;
    auto result = created.get_future();
    thread = std::jthread([this, &created] {
      WNDCLASSW type{};
      type.lpfnWndProc = Procedure;
      type.hInstance = GetModuleHandleW(nullptr);
      type.lpszClassName = L"KetsuiGpuPresentationProbe";
      type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
      if (!RegisterClassW(&type) &&
          GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        created.set_value(nullptr);
        return;
      }
      const auto value = CreateWindowExW(
          0, type.lpszClassName,
          L"Ketsui GPU presentation test | F11: fullscreen | Close: finish",
          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, nullptr,
          nullptr, type.hInstance, this);
      window = value;
      if (value) {
        ShowWindow(value, SW_SHOWNOACTIVATE);
      }
      created.set_value(value);
      if (!value) {
        return;
      }
      MSG message;
      while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    });
    if (!result.get()) {
      throw std::runtime_error("Could not create presentation test window");
    }
  }
  ~PresentWindow() {
    if (const auto value = window.load()) {
      PostMessageW(value, Finish, 0, 0);
    }
  }
  HWND Handle() const { return window.load(); }
  bool Closed() const { return closed.load(); }
  void Resize(unsigned width, unsigned height) {
    if (SendMessageW(Handle(), ResizeMessage, width, height)) {
      throw std::runtime_error("Test window resize failed");
    }
  }
  void Minimize() { SendMessageW(Handle(), ShowMessage, SW_MINIMIZE, 0); }
  void Restore() { SendMessageW(Handle(), ShowMessage, SW_RESTORE, 0); }
};
} // namespace Ketsui::GpuCore
